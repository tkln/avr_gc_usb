#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

#include "debug.h"

#include "usb.h"

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static volatile uint8_t usb_configuration = 0;
static volatile uint8_t idle_config = 125;
static volatile uint8_t idle_count = 0;

static void usb_init(void)
{
    /* HW config */
    UHWCON = (1<<UVREGE); /* enable usb pad regulator */
    /* USB freeze */
    USBCON = (1<<USBE) | (1<<FRZCLK); /* enable usb controller, freeze usb clock */
    /* PLL config */
    PLLCSR = (1<<PINDIV) | (1<<PLLE); /* set pll prescaler, enable the pll */

    /* Wait for PLL to lock */
    while (!(PLLCSR) & (1<<PLOCK))
        ;

    /* USB config */
    USBCON = (1<<USBE) | (1<<OTGPADE);
    /* Attach */
    UDCON &= ~(1<<DETACH);

    usb_configuration = 0;

    /* Enable interrupts */
    UDIEN = (1<<EORSTE) | (1<<SOFE);
    sei();
}

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
    uint8_t joy_x;
    uint8_t joy_y;
    uint8_t c_x;
    uint8_t c_y;
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
        .descriptor_type        = USB_DESC_TYPE_HID,
        .bcd_hid                = 0x101,
        .country_code           = 0,
        .num_descriptors        = 1,
        .descriptor_class_type  = 34, /* Report? */
        .descriptor_length      = sizeof(joypad_report_desc), 
    },
    .endpoint_desc = {
        .length             = sizeof(config_desc_final.endpoint_desc),
        .descriptor_type    = USB_DESC_TYPE_ENDPOINT,
        .endpoint_address   = JOYPAD_EP | 1<<7, /* 7th bit is set for IN EP */
        .attributes         = USB_EP_TYPE_INTERRUPT,
        .max_packet_size    = JOYPAD_EP_SIZE,
        .interval           = 1,
    },
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
		.value 		= 0x2200, /* XXX */
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

    PORT(LED1_BASE) ^= 1<<LED1_PIN;

    if ((status & (1<<SOFI)) && usb_configuration) {
		if (idle_config && (++div4 & 3) == 0) {
            PORT(LED2_BASE) ^= 1<<LED2_PIN;
            UENUM = JOYPAD_EP;
            status = UEINTX;
            if (status & (1<<RWAL)) {
                idle_count++;
                if (idle_count == idle_config) {
                    idle_count = 0;
                    usb_fifo_write((void *)&joypad_report, sizeof(joypad_report));
                    UEINTX = 0x3a;
                }
            }
        }
    }
}

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
        PORT(LED2_BASE) &= ~(1<<LED2_PIN);
        halt();
    }
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

extern void controller_probe(void);
extern void controller_poll(uint16_t addr);

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
    for (uint16_t i = 0; i < sizeof(struct joypad_report); ++i) {
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

#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))

int main(void)
{
    static uint8_t controller_buffer[(sizeof(struct joypad_report)) * 4] = {0};

    CPU_PRESCALE(0);

    led_init();

    usart_init();
    stdio_init();

    usb_init();

    PORTD &= ~(1<<0);

    controller_probe();

    for (;;) {
        controller_poll((uint16_t)&controller_buffer);

        if (controller_buffer[0] != 0x11) {
            _delay_us(100);
            continue;
        }

        process_gc_data(controller_buffer, (void *)&joypad_report);

        joypad_report.buttons_0 = joypad_report.buttons_0;
        joypad_report.buttons_1 = joypad_report.buttons_1;
        joypad_report.joy_x += 127;
        joypad_report.joy_y = 127 - joypad_report.joy_y;
        joypad_report.c_x += 127;
        joypad_report.c_y = 127 - joypad_report.c_y;
        joypad_report.l = joypad_report.l;
        joypad_report.r = joypad_report.r;

        printf("%u\n", joypad_report.joy_x);

        usb_joypad_send();
        _delay_ms(5);
    }
}
