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


/*
uint8_t joypad_report_desc[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x04,                    // USAGE (Joystick)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x15, 0x81,                    //   LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //   LOGICAL_MAXIMUM (127)
    0x05, 0x01,                    //   USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    //   USAGE (Pointer)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0x05, 0x09,                    //   USAGE_PAGE (Button)
    0x19, 0x01,                    //   USAGE_MINIMUM (Button 1)
    0x29, 0x08,                    //   USAGE_MAXIMUM (Button 8)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //   REPORT_SIZE (1)
    0x95, 0x08,                    //   REPORT_COUNT (8)
    0x55, 0x00,                    //   UNIT_EXPONENT (0)
    0x65, 0x00,                    //   UNIT (None)
    0x81, 0x02,                    //   INPUT (Data,Var,Abs)
    0xc0                           // END_COLLECTION
};
*/

static const uint8_t joypad_report_desc[] = {
    0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
    0x09, 0x05,                    // USAGE (Game Pad)
    0xa1, 0x01,                    // COLLECTION (Application)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x05, 0x09,                    //     USAGE_PAGE (Button)
    0x19, 0x01,                    //     USAGE_MINIMUM (Button 1)
    0x29, 0x08,                    //     USAGE_MAXIMUM (Button 8)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x95, 0x08,                    //     REPORT_COUNT (8)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x05, 0x01,                    //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30,                    //     USAGE (X)
    0x09, 0x31,                    //     USAGE (Y)
    0x15, 0x81,                    //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f,                    //     LOGICAL_MAXIMUM (127)
    0x75, 0x08,                    //     REPORT_SIZE (8)
    0x95, 0x02,                    //     REPORT_COUNT (2)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0xc0,                          //     END_COLLECTION
    0xc0                           // END_COLLECTION
};


struct joypad_report {
    int8_t x;
    int8_t y;
    uint8_t buttons;
} __attribute__((packed));

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

static inline void usb_fifo_write(const unsigned char *src, size_t sz)
{
#if 0
    while (sz--) 
        UEDATX = *(src++);
#else
    uint8_t i;
    while (sz) {
        do {
            i = UEINTX;
        } while (!(i & ((1<<TXINI) | (1<<RXOUTI))));
        if (i & (1<<RXOUTI))
            return;
        for (i = 0; i < MIN(sz, 32); ++i)
            UEDATX = *(src++);
        /* This is for the interrupt handshake */
        UEINTX = ~(1<<TXINI);
        sz -= i;
    }
#endif
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

    if ((status & (1<<SOFI)) && usb_configuration) {
		if (idle_config && (++div4 & 3) == 0) {
            UENUM = JOYPAD_EP;
            if (UEINTX & (1<<RWAL)) {
                idle_count++;
                if (idle_count == idle_config) {
                    idle_count = 0;
                    for (int i = 0; i < 3; ++i) {
                        UEDATX = 0x55 ^ i;
                    }
                    UEINTX = 0x3a;
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
    usb_fifo_write(desc->data, len);
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
    idle_count = 0;
    UEINTX = ~(1<<TXINI);
    return;
}

static inline void usb_hid_req_get_report(struct usb_request *usb_req)
{
    int i;

    while (!(UEINTX & (1<<TXINI))) ;

    for (int i = 0; i < 3; ++i) {
        UEDATX = 0x55 ^ i;
    }
    UEINTX = ~(1<<TXINI);
}

/* Endpoint interrupt */
ISR(USB_COM_vect)
{
    uint8_t status;
    static struct usb_request usb_req;

    UENUM = 0;
    status = UEINTX;

    /* Setup packet received  */
    if (status & (1<<RXSTPI)) {
        usb_fifo_read((void *)&usb_req, sizeof(usb_req));
        UEINTX = ~((1<<RXSTPI) | (1<<RXOUTI) | (1<<TXINI));
        printf("request_type: 0x%02x, request: 0x%02x, value: 0x%04x, index: 0x%04x, len: 0x%04x\n",
               usb_req.request_type, usb_req.request, usb_req.value,
               usb_req.index, usb_req.length);
        //usart_putchar(usb_req.request, NULL);
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
        PORT(LED2_BASE) &= ~LED2_PIN;
        halt();
}

    /* This happens */
}

int main(void)
{
    DDR(LED1_BASE) |= LED1_PIN;
    DDR(LED2_BASE) |= LED2_PIN;

    usart_init();
    stdio_init();

    usb_init();


    printf("hello\n");
    //int32_t val;
    for (;;) {
    }
}
