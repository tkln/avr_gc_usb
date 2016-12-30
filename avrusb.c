#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

#define LED1_BASE PIND
#define LED1_PIN (1<<5)

#define LED2_BASE PINB
#define LED2_PIN (1<<0)

#define BAUD 9600

#define PORT(base) (*(unsigned char *)(&base + 2))
#define DDR(base) (*(unsigned char *)(&base + 1))
#define PIN(base) (*(unsigned char *)(&base))

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int usart_putchar(char c, FILE *stream)
{
    if (c == '\n')
        usart_putchar('\r', stream);

    while (!(UCSR1A & (1<<UDRE1)))
        ;

    UDR1 = c;

    return 0;
}

static char usart_getchar(FILE *stream)
{
    while (!(UCSR1A & (1<<RXC1)))
        ;
    return UDR1;
}

static FILE mystdout = FDEV_SETUP_STREAM(usart_putchar, NULL, _FDEV_SETUP_WRITE);
static FILE mystdin = FDEV_SETUP_STREAM(NULL, usart_getchar, _FDEV_SETUP_READ);

static void usart_init(void)
{
    const uint16_t ubbr = 2000000UL / (2 * BAUD) - 1;

    UBRR1H = ubbr >> 8;
    UBRR1L = ubbr;
    UCSR1B = (1<<RXEN1) | (1<<TXEN1);
    /* 8 data bits, 1 stop bit */
    UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);

    DDRD |= 1<<1;

    usart_putchar('A', NULL);
    usart_putchar('\n', NULL);
}

static void stdio_init(void)
{
    stdout = &mystdout;
    stdin = &mystdin;
}

static inline void halt(void)
{
    cli();
    for (;;)
        ;
}

static volatile uint8_t usb_configuration = 0;
static volatile uint8_t idle_config = 125;
static volatile uint8_t idle_count = 0;

static void usb_init(void)
{
    /* hw config */
    UHWCON = (1<<UVREGE); /* enable usb pad regulator */
    /* usb freeze */
    USBCON = (1<<USBE) | (1<<FRZCLK); /* enable usb controller, freeze usb clock */
    /* pll config */
    PLLCSR = (1<<PINDIV) | (1<<PLLE); /* set pll prescaler, enable the pll */

    /* wait for pll to lock */
    while (!(PLLCSR) & (1<<PLOCK))
        ;

    /* usb config */
    USBCON = (1<<USBE) | (1<<OTGPADE);
    /* attach */
    UDCON &= ~(1<<DETACH);

    usb_configuration = 0;

    /* enable interrupts */
    UDIEN = (1<<EORSTE) | (1<<SOFE);
    sei();
}

#define EP_TYPE_CONTROL 0x00
#define ENDPOINT_TYPE_INTERRUPT_IN 0xc1

enum {
    USB_EP_TYPE_CONTROL = 0,
    USB_EP_TYPE_ISOCHRONOUS = 1,
    USB_EP_TYPE_BULK = 2,
    USB_EP_TYPE_INTERRUPT = 3,
};

enum usb_request_codes {
    USB_REQ_GET_STATUS =        0,
    USB_REQ_CLEAR_FEATURE =     1,
    USB_REQ_SET_FEATURE =       3,
    USB_REQ_SET_ADDRESS =       5,
    USB_REQ_GET_DESCRIPTOR =    6,
    USB_REQ_SET_DESCRIPTOR =    7,
    USB_REQ_GET_CONFIGURATION = 8,
    USB_REQ_SET_CONFIGURATION = 9,
    USB_REQ_GET_INTERFACE =     10,
    USB_REQ_SET_INTERFACE =     11,
    USB_REQ_SYNCH_FRAME =       12,
};

enum usb_descriptor_type {
    USB_DESC_TYPE_DEVICE =        1,
    USB_DESC_TYPE_CONFIGURATION = 2,
    USB_DESC_TYPE_STRING =        3,
    USB_DESC_TYPE_INTERFACE =     4,
    USB_DESC_TYPE_ENDPOINT =      5,
};

/* XXX */
#define JOYPAD_INTERFACE 0
#define JOYPAD_EP_SIZE 8
#define JOYPAD_EP 3

static const struct {
    uint8_t ueconx;
    uint8_t uecfg0x;
    uint8_t uecfg1x;
} endpoint_cfgs[] = {
    { .ueconx = 0, .uecfg0x = 0, .uecfg1x = 0 },
    { .ueconx = 0, .uecfg0x = 0, .uecfg1x = 0 },
    { .ueconx = 0, .uecfg0x = 0, .uecfg1x = 0 },
    [JOYPAD_EP] = {
        .ueconx     = 1<<EPEN,
        .uecfg0x    = (1<<EPTYPE0) | (1<<EPTYPE1) | (1<<EPDIR),
        /* The endpoint size is 8, so the EPSIZE bits are zero and omited */
        .uecfg1x    = 1<<EPBK0 | 1<<ALLOC
    },
    { .ueconx = 0, .uecfg0x = 0, .uecfg1x = 0 },
};

struct usb_device_descriptor {
    uint8_t length;
    uint8_t descriptor_type;
    uint16_t bcd_usb;
    uint8_t device_class;
    uint8_t device_sub_class;
    uint8_t device_protocol;
    uint8_t max_packet_size_0;
    uint16_t id_vendor;
    uint16_t id_product;
    uint16_t bcd_device;
    uint8_t manufacturer_idx;
    uint8_t product_idx;
    uint8_t serial_number_idx;
    uint8_t num_configurations;
} __attribute__((packed));

struct usb_string_descriptor {
    uint8_t length;
    uint8_t descriptor_type;
    int16_t string[];
}__attribute__((packed));

/* TODO make this progmem */
static const struct usb_device_descriptor device_descriptor = {
    .length             = sizeof(struct usb_device_descriptor),
    .descriptor_type    = USB_DESC_TYPE_DEVICE,
    .bcd_usb            = 0x0002,
    .device_class       = 0,
    .device_sub_class   = 0,
    .device_protocol    = 0,
    .max_packet_size_0  = 32,
    .id_vendor          = 0xdead,
    .id_product         = 0xbeef,
    .bcd_device         = 0x0001,
    .manufacturer_idx   = 1,
    .product_idx        = 2,
    .serial_number_idx  = 0,
    .num_configurations = 1
};

struct usb_config_desc {
    uint8_t length;
    uint8_t descriptor_type;
    uint16_t total_length;
    uint8_t num_interfaces;
    uint8_t configuration_value;
    uint8_t configuration_idx;
    uint8_t attributes;
    uint8_t max_power;
} __attribute__((packed));

struct usb_interface_desc {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t interface_number;
    uint8_t alternate_setting;
    uint8_t num_endpoints;
    uint8_t interface_class;
    uint8_t interface_sub_class;
    uint8_t interface_protocol;
    uint8_t interface_idx;
} __attribute__((packed));

enum usb_config_desc_attrs {
    USB_CFG_ATTR_RESERVED = 7,
    USB_CFG_ATTR_SELF_POWERED = 6,
    USB_CFG_ATTR_REMOTE_WAKEUP = 5,
};

enum usb_base_class {
    USB_HID_DEVICE_CLASS = 0x03
};

struct usb_hid_interface_desc {
    uint8_t length;
    uint8_t descriptor_type;
    uint16_t bcd_hid;
    uint8_t country_code;
    uint8_t num_descriptors;
    uint8_t descriptor_class_type;
    uint16_t descriptor_length;
} __attribute__((packed));

struct usb_endpoint_desc {
    uint8_t length;
    uint8_t descriptor_type;
    uint8_t endpoint_address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
} __attribute__((packed));

enum collections {
    PHYSICAL    = 0x00,
    APPLICATION = 0x01,
};

enum usage_pages {
    GENERIC_DESKTOP = 0x01,
    BUTTON          = 0x09,
};

enum usages {
    GAME_PAD    = 0x05,
    X           = 0x30,
    Y           = 0x31,
    Z           = 0x32,
    RX          = 0x33,
    RY          = 0x34,
    RZ          = 0x35,
};

enum inptu_bits_0 {
    DATA    = 0x00,
    CONST   = 0x01,
};

enum inptu_bits_1 {
    ARRAY    = 0x00<<1,
    VARIABLE   = 0x01<<1,
};

enum inptu_bits_2 {
    ABSOLUTE    = 0x00<<2,
    RELATIVE    = 0x01<<3,
};


#define USAGE(x)            0x09, (x),
#define USAGE_PAGE(x)       0x05, (x),
#define COLLECTION(x)       0xa1, (x),
#define END_COLLECTION      0xc0,
#define USAGE_MINIMUM(x)    0x19, (x),
#define USAGE_MAXIMUM(x)    0x29, (x),
#define LOGICAL_MINIMUM(x)  0x15, (x),
#define LOGICAL_MAXIMUM(x)  0x25, (x),
#define REPORT_COUNT(x)     0x95, (x),
#define REPORT_SIZE(x)      0x75, (x),
#define INPUT(b0, b1, b2)   0x81, ((b0) | (b1) | (b2)),

static const uint8_t joypad_report_desc[] = {
    USAGE_PAGE(GENERIC_DESKTOP)
    USAGE(GAME_PAD)
    COLLECTION(APPLICATION)
        COLLECTION(PHYSICAL)
            /* Buttons in the first byte */
            USAGE_PAGE(BUTTON)
            USAGE_MINIMUM(1)
            USAGE_MAXIMUM(5)
            LOGICAL_MINIMUM(0)
            LOGICAL_MAXIMUM(1)
            REPORT_COUNT(5)
            REPORT_SIZE(1)
            INPUT(DATA, VARIABLE, ABSOLUTE)

            /* Padding bits */
            REPORT_SIZE(3)
            REPORT_COUNT(1)
            INPUT(CONST, VARIABLE, ABSOLUTE)

            /* Buttons from the second byte */
            USAGE_PAGE(BUTTON)
            USAGE_MINIMUM(6)
            USAGE_MAXIMUM(12)
            LOGICAL_MINIMUM(0)
            LOGICAL_MAXIMUM(1)
            REPORT_COUNT(7)
            REPORT_SIZE(1)
            INPUT(DATA, VARIABLE, ABSOLUTE)

            /* Padding */
            REPORT_SIZE(1)
            REPORT_COUNT(1)
            INPUT(CONST, VARIABLE, ABSOLUTE)

            /* Main joystick */
            USAGE_PAGE(GENERIC_DESKTOP)
            USAGE(X)
            USAGE(Y)
            LOGICAL_MINIMUM(-127)
            LOGICAL_MAXIMUM(127)
            REPORT_SIZE(8)
            REPORT_COUNT(2)
            INPUT(DATA, VARIABLE, ABSOLUTE)

            /* The C joystick */
            USAGE_PAGE(GENERIC_DESKTOP)
            USAGE(Z)
            USAGE(RX)
            LOGICAL_MINIMUM(-127)
            LOGICAL_MAXIMUM(127)
            REPORT_SIZE(8)
            REPORT_COUNT(2)
            INPUT(DATA, VARIABLE, ABSOLUTE)

            /* The throtles */
            USAGE_PAGE(GENERIC_DESKTOP)
            USAGE(RY)
            USAGE(RZ)
            LOGICAL_MINIMUM(0)
            LOGICAL_MAXIMUM(255)
            REPORT_SIZE(8)
            REPORT_COUNT(2)
            INPUT(DATA, VARIABLE, ABSOLUTE)
        END_COLLECTION
    END_COLLECTION
};


static volatile struct joypad_report {
    uint8_t buttons_0;
    uint8_t buttons_1;
    uint8_t x;
    uint8_t y;
    uint8_t cx;
    uint8_t cy;
    uint8_t l;
    uint8_t r;
} __attribute__((packed)) joypad_report;

static const struct usb_config_desc_final {
    struct usb_config_desc config; 
    struct usb_interface_desc interface;
    struct usb_hid_interface_desc hid_interface_desc;
    struct usb_endpoint_desc endpoint_desc;
} __attribute__((packed)) config_desc_final = {
    .config = {
        .length                 = sizeof(config_desc_final.config),
        .descriptor_type        = USB_DESC_TYPE_CONFIGURATION,
        .total_length           = sizeof(config_desc_final),
        .num_interfaces         = 1,
        .configuration_value    = 1,
        .configuration_idx      = 0,
        .attributes             = (1<<USB_CFG_ATTR_RESERVED) |
                                  (1<<USB_CFG_ATTR_SELF_POWERED),
        .max_power              = 50
    },
    .interface = {
        .length                 = sizeof(config_desc_final.interface),
        .descriptor_type        = USB_DESC_TYPE_INTERFACE,
        .interface_number       = JOYPAD_INTERFACE,
        .alternate_setting      = 0,
        .num_endpoints          = 1,
        .interface_class        = USB_HID_DEVICE_CLASS,
        .interface_sub_class    = 0,
        .interface_protocol     = 0,
        .interface_idx          = 0,
    },
    .hid_interface_desc = {
        .length                 = sizeof(config_desc_final.hid_interface_desc),
        .descriptor_type        = 33,
        .bcd_hid                = 0x101,
        .country_code           = 0,
        .num_descriptors        = 1,
        .descriptor_class_type  = 34, /* Report? */
        .descriptor_length      = sizeof(joypad_report_desc), 
    },
    .endpoint_desc = {
        .length             = sizeof(config_desc_final.endpoint_desc),
        .descriptor_type    = USB_DESC_TYPE_ENDPOINT,
        .endpoint_address   = JOYPAD_EP | 1<<7, /* 7th bit is set for in ep */
        .attributes         = USB_EP_TYPE_INTERRUPT , /* intr */
        .max_packet_size    = JOYPAD_EP_SIZE,
        .interval           = 1,
    },
};

#define USB_STRING_DESCRIPTOR(name, str) \
    static const struct { \
        uint8_t length; \
        uint8_t descriptor_type; \
        int16_t string[sizeof(str)]; \
    } __attribute__((packed)) name = { \
        .length             = sizeof(name), \
        .descriptor_type    = USB_DESC_TYPE_STRING,\
        .string             = {str},\
    };

USB_STRING_DESCRIPTOR(string0, 0x0409); /* US English language code */
USB_STRING_DESCRIPTOR(string1, L"lörs");
USB_STRING_DESCRIPTOR(string2, L"lärä");

/* TODO make this progmem */
static struct usb_descriptor {
    uint16_t value;
    uint16_t index;
    const unsigned char *data;
    uint8_t data_sz;
} descriptors[] = {
    {
        .value 		= USB_DESC_TYPE_DEVICE<<8, /* 0x0100 */
        .index 		= 0,
        .data 		= (void *)&device_descriptor,
        .data_sz	= sizeof(device_descriptor)
    }, {
        .value 		= USB_DESC_TYPE_CONFIGURATION<<8, /* 0x0200 */
        .index 		= 0,
        .data 		= (void *)&config_desc_final,
        .data_sz 	= sizeof(config_desc_final),
    }, {
		.value 		= 0x2200, /* XXX ?? */
		.index 		= JOYPAD_INTERFACE,
		.data 		= joypad_report_desc,
		.data_sz 	= sizeof(joypad_report_desc),
	}, {
        .value 		= USB_DESC_TYPE_STRING<<8,
        .index 		= 0,
        .data 		= (void *)&string0,
        .data_sz 	= sizeof(string0),
    }, {
        .value 		= USB_DESC_TYPE_STRING<<8 | 0x01,
        .index 		= 0x0409,
        .data 		= (void *)&string1,
        .data_sz 	= sizeof(string1),
    }, {
        .value 		= USB_DESC_TYPE_STRING<<8 | 0x02,
        .index 		= 0x0409,
        .data 		= (void *)&string2,
        .data_sz 	= sizeof(string2),
    }
};

static inline void usb_fifo_read(unsigned char *dest, size_t sz)
{
    while (sz--) 
        *(dest++) = UEDATX;
}

static inline void usb_fifo_write(const unsigned char *src, uint8_t sz)
{
    while (sz--)
        UEDATX = *(src++);
}

static inline void usb_fifo_write_(const unsigned char *src, size_t sz)
{
    uint8_t i;
    while (sz) {
        do {
            i = UEINTX;
        } while (!(i & ((1<<TXINI) | (1<<RXOUTI))));
        if (i & (1<<RXOUTI))
            return;
        usb_fifo_write(src, MIN(sz, 32)); 
        src += MIN(sz, 32);
        i = MIN(sz, 32);
        /* This is for the interrupt handshake */
        UEINTX = ~(1<<TXINI);
        sz -= i;
    }
}

/* Device interrupt */
ISR(USB_GEN_vect)
{
    uint8_t status;
    static uint8_t div4 = 0;

    status = UDINT;
    UDINT = 0;

    /* End of reset interrupt */
    if (status & (1<<EORSTI)) {
        /* Set the endpoint number */
        UENUM = 0;
        /* Enable the endpoint */
        UECONX = 1<<EPEN;

        /* Set endpoint type */
        UECFG0X = USB_EP_TYPE_CONTROL<<6;

        /* Allocate 32 bytes for the endpoint */
        UECFG1X = (1<<EPSIZE1) | (1<<ALLOC);

        /* Enable received setup interrupt */
        UEIENX = (1<<RXSTPE);
        
        usb_configuration = 0;
    }

    PORT(LED1_BASE) ^= LED1_PIN;

    if ((status & (1<<SOFI)) && usb_configuration) {
        /*
        printf("here: %s: %d: usb_config: %d, idle_config: %d, idle_count: %d\n",
                __func__, __LINE__, usb_configuration, idle_config, idle_count); //yep
                */
		if (idle_config && (++div4 & 3) == 0) {
            PORT(LED2_BASE) ^= LED2_PIN;
            UENUM = JOYPAD_EP;
            //printf("here: %s: %d\n", __func__, __LINE__); //yep
            status = UEINTX;
            printf("UEINTX: 0x%02x\n", status);
            if (status & (1<<RWAL)) {
                //printf("here: %s: %d\n", __func__, __LINE__);
                idle_count++;
                if (idle_count == idle_config) {
                    idle_count = 0;
                    usb_fifo_write((void *)&joypad_report, sizeof(joypad_report));
                    UEINTX = 0x3a; // These bits don't make any sense
                    printf("here: %s: %d\n", __func__, __LINE__);
                }
            }
        }
    }
}

struct usb_request {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed));

enum hid_requests {
    USB_HID_GET_REPORT      = 0x01,
    USB_HID_GET_IDLE        = 0x02,
    USB_HID_GET_PROTOCOL    = 0x03,
    USB_HID_SET_REPORT      = 0x09,
    USB_HID_SET_IDLE        = 0x0a,
    USB_HID_SET_PROTOCOL    = 0x0b,
};

static inline void usb_req_clear_feature(struct usb_request *usb_req)
{
    uint8_t i;
    if (usb_req->request_type != 0x02 || usb_req->request_type != 0)
        return;
    i = usb_req->index & 0x7f;
    if (i >= 1 && i <= 4) {
        UEINTX = ~(1<<TXINI);
        UENUM = i;
        UECONX = (1<<STALLRQ) | (1<<EPEN);
        UERST = (1<<i);
        UERST = 0;
    }
}

static inline void usb_req_set_address(struct usb_request *usb_req)
{
    //PORT(LED1_BASE) &= ~LED1_PIN;
    UEINTX = ~(1<<TXINI);
    while (!(UEINTX & (1<<TXINI))) ;
    UDADDR = usb_req->value | (1<<ADDEN);
}

static inline void usb_req_get_descriptor(struct usb_request *usb_req)
{
    /* Look for the matching descriptor */
    uint8_t i;
    struct usb_descriptor *desc;

    for (i = 0; ; ++i) {
        if (i >= ARRAY_LEN(descriptors)) {
            UECONX = (1<<STALLRQ) | (1<<EPEN);
            printf("no descriptor found\n");
            return;
        }
        desc = descriptors + i;
        if (usb_req->value == desc->value && usb_req->index == desc->index)
            break;
    }
    /* Wait for the host to get ready to accept data */
    uint8_t len = MIN(usb_req->length, 255);
    len = MIN(len, desc->data_sz);
    usb_fifo_write_(desc->data, len);
}

static inline void usb_req_set_configuration(struct usb_request *usb_req)
{
    uint8_t i;
    if (usb_req->request_type != 0)
        return;
    usb_configuration = usb_req->value;
    UEINTX = ~(1<<TXINI);
    /* Configure the endpoints */
    for (i = 1; i < 5; ++i) {
        UENUM = i;
        UECONX = endpoint_cfgs[i].ueconx;
        if (endpoint_cfgs[i].ueconx) {
            UECFG0X = endpoint_cfgs[i].uecfg0x;
            UECFG1X = endpoint_cfgs[i].uecfg1x;
        }
    }
    /* Reset all endpoints, except 0 */
    UERST = 0x1e;
    UERST = 0;
    return;
}

static inline void usb_hid_req_set_idle(struct usb_request *usb_req)
{
    /* Upper byte is the value */
    idle_config = (usb_req->value >> 8);
    printf("%s: %d\n", __func__, idle_config);
    idle_count = 0;
    UEINTX = ~(1<<TXINI);
    return;
}

static inline void usb_hid_req_get_report(struct usb_request *usb_req)
{
    while (!(UEINTX & (1<<TXINI))) ;

    usb_fifo_write((void *)&joypad_report, sizeof(joypad_report));
    UEINTX = ~(1<<TXINI);
}

/* Endpoint interrupt */
ISR(USB_COM_vect)
{
    uint8_t status;
    static struct usb_request usb_req;

    UENUM = 0;
    status = UEINTX;

    /* SETUP packet received  */
    if (status & (1<<RXSTPI)) {
        usb_fifo_read((void *)&usb_req, sizeof(usb_req));
		/* ACK the SETUP packet */
        UEINTX = ~((1<<RXSTPI) | (1<<RXOUTI) | (1<<TXINI));

        switch (usb_req.request_type) {
            case 0x00:
            switch (usb_req.request) {
                case USB_REQ_SET_ADDRESS:
                usb_req_set_address(&usb_req);
                return;

                case USB_REQ_SET_CONFIGURATION:
                usb_req_set_configuration(&usb_req);
                return;
            }
            case 0x01:
            case 0x02:
            switch (usb_req.request) {
                case USB_REQ_CLEAR_FEATURE:
                usb_req_clear_feature(&usb_req);
                return;
            }
            case 0x21:
            switch (usb_req.request) {
                case USB_HID_SET_IDLE:
                usb_hid_req_set_idle(&usb_req);
                return;
            }
            case 0x80:
            case 0x81:
            switch (usb_req.request) {
                case USB_REQ_GET_DESCRIPTOR:
                usb_req_get_descriptor(&usb_req);
                return;
            }
            case 0xa1:
            switch (usb_req.request) {
                case USB_HID_GET_REPORT:
                usb_hid_req_get_report(&usb_req);
                return;
            }
        }
        printf("%d: unhandled request\n", __LINE__);
        printf("request_type: 0x%02x, request: 0x%02x, value: 0x%04x, index: 0x%04x, len: 0x%04x\n",
               usb_req.request_type, usb_req.request, usb_req.value,
               usb_req.index, usb_req.length);
        PORT(LED2_BASE) &= ~LED2_PIN;
        halt();
    }

    /* This happens */
}

int8_t usb_joypad_send(void)
{
	uint8_t status, timeout;

	if (!usb_configuration) return -1;
	status = SREG;
	cli();
	UENUM = JOYPAD_EP;
	timeout = UDFNUML + 50;
	while (1) {
		/* Wait for ready */
		if (UEINTX & (1<<RWAL))
		    break;
		SREG = status;
		if (!usb_configuration)
		    return -1;
		if (UDFNUML == timeout)
            return -1;
		status = SREG;
		cli();
		UENUM = JOYPAD_EP;
	}
    usb_fifo_write((void *)&joypad_report, sizeof(joypad_report));
	UEINTX = (1<<RWAL) | (1<<NAKOUTI) | (1<<RXSTPI) | (1<<STALLEDI);
	idle_count = 0;
	SREG = status;
	return 0;

}

#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))

extern void controller_probe(void);
extern void controller_poll(uint16_t addr);
extern void func_test(uint16_t addr);

struct gc_state {
    uint8_t buttons_0;
    uint8_t buttons_1;
    uint8_t joy_x;
    uint8_t joy_y;
    uint8_t c_x;
    uint8_t c_y;
    uint8_t l;
    uint8_t r;
} __attribute__((packed));

static uint8_t popcnt4(uint8_t v)
{
    uint8_t r = (v & 1);
    r += (v>>1) & 1;
    r += (v>>2) & 1;
    r += (v>>3) & 1;
    return r;
}

static inline void process_gc_data(uint8_t *buf, uint8_t *state)
{
    uint8_t byte;

#define THRESH 2
    for (uint16_t i = 0; i < sizeof(struct gc_state); ++i) {
        byte = 0;
        byte |= (popcnt4(buf[i * 4] >> 4) > THRESH) << 7;
        byte |= (popcnt4(buf[i * 4]) > THRESH) << 6;

        byte |= (popcnt4(buf[i * 4 + 1] >> 4) > THRESH) << 5;
        byte |= (popcnt4(buf[i * 4 + 1]) > THRESH) << 4;

        byte |= (popcnt4(buf[i * 4 + 2] >> 4) > THRESH) << 3;
        byte |= (popcnt4(buf[i * 4 + 2]) > THRESH) << 2;

        byte |= (popcnt4(buf[i * 4 + 3] >> 4) > THRESH) << 1;
        byte |= (popcnt4(buf[i * 4 + 3]) > THRESH) << 0;

        state[i] = byte;
    }
}

int main(void)
{
    static uint8_t controller_buffer[(sizeof(struct gc_state)) * 4] = {0};
    static struct gc_state gc_state; 

    DDR(LED1_BASE) |= LED1_PIN;
    DDR(LED2_BASE) |= LED2_PIN;

    CPU_PRESCALE(0);

    usart_init();
    stdio_init();

    usb_init();

    /* gc data: d3: PD0 */
    PORTD &= ~(1<<0);

    controller_probe();

    for (;;) {
        controller_poll((uint16_t)&controller_buffer);

        if (controller_buffer[0] != 0x11) {
            _delay_us(100);
            continue;
        }

        process_gc_data(controller_buffer, (void *)&gc_state);

        joypad_report.buttons_0 = gc_state.buttons_0;
        joypad_report.buttons_1 = gc_state.buttons_1;
        joypad_report.x = 127 + gc_state.joy_x;
        joypad_report.y = 127 - gc_state.joy_y;
        joypad_report.cx = - 127 + gc_state.c_x;
        joypad_report.cy = 127 - gc_state.c_y;
        joypad_report.l = gc_state.l;
        joypad_report.r = gc_state.r;

        printf("%u\n", joypad_report.x);

        usb_joypad_send();
        _delay_ms(10);
    }
}
