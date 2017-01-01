#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>

#include "debug.h"
#include "usb.h"
#include "controller.h"
#include "iodefs.h"

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static volatile uint8_t usb_configuration = 0;

static void usb_init(void)
{
    /* HW config */
    UHWCON = (1<<UVREGE); /* Enable USB pad regulator */
    /* USB freeze */
    USBCON = (1<<USBE) | (1<<FRZCLK); /* Enable USB controller, freeze USB clock */
    /* PLL config */
    PLLCSR = (1<<PINDIV) | (1<<PLLE); /* Set PLL prescaler, enable the PLL */

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

#define GAMEPAD_INTERFACE 0
#define GAMEPAD_EP_SIZE 8
#define GAMEPAD_EP 3

static const struct usb_ep_cfg {
    uint8_t ueconx;
    uint8_t uecfg0x;
    uint8_t uecfg1x;
} usb_ep_cfgs[] = {
    { 
        .ueconx = 1<<EPEN,
        .uecfg0x = USB_EP_TYPE_CONTROL<<EPTYPE0,
        .uecfg1x = (1<<EPSIZE1) | (1<<ALLOC),
    },
    [GAMEPAD_EP] = {
        .ueconx     = 1<<EPEN,
        .uecfg0x    =  (USB_EP_TYPE_INTERRUPT<<EPTYPE0) | (1<<EPDIR),
        /* The endpoint size is 8, so the EPSIZE bits are zero and omited */
        .uecfg1x    = (1<<EPBK0) | (1<<ALLOC),
    },
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

enum string_descriptors {
    STRING_DESC_IDX_LANG,
    STRING_DESC_IDX_MANUF,
    STRING_DESC_IDX_PROD,
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
    .manufacturer_idx   = STRING_DESC_IDX_MANUF,
    .product_idx        = STRING_DESC_IDX_PROD,
    .serial_number_idx  = 0,
    .num_configurations = 1
};

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
        .interface_number       = GAMEPAD_INTERFACE,
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
        .descriptor_class_type  = USB_DESC_TYPE_REPORT,
        .descriptor_length      = sizeof(joypad_report_desc),
    },
    .endpoint_desc = {
        .length             = sizeof(config_desc_final.endpoint_desc),
        .descriptor_type    = USB_DESC_TYPE_ENDPOINT,
        .endpoint_address   = GAMEPAD_EP | 1<<7, /* 7th bit is set for IN EP */
        .attributes         = USB_EP_TYPE_INTERRUPT,
        .max_packet_size    = GAMEPAD_EP_SIZE,
        .interval           = 0,
    },
};

USB_STRING_DESCRIPTOR(str_desc_lang, 0x0409); /* US English language code */
USB_STRING_DESCRIPTOR(str_desc_manuf, L"lörs");
USB_STRING_DESCRIPTOR(str_desc_prod, L"lärä");

/* TODO make this progmem */
static struct usb_descriptor {
    uint16_t value;
    uint16_t index;
    const void *data;
    uint8_t data_sz;
} descriptors[] = {
    {
        .value      = USB_DESC_TYPE_DEVICE<<8, /* 0x0100 */
        .index      = 0,
        .data       = &device_descriptor,
        .data_sz    = sizeof(device_descriptor)
    }, {
        .value      = USB_DESC_TYPE_CONFIGURATION<<8, /* 0x0200 */
        .index      = 0,
        .data       = &config_desc_final,
        .data_sz    = sizeof(config_desc_final),
    }, {
        .value      = USB_DESC_TYPE_REPORT<<8,
        .index      = GAMEPAD_INTERFACE,
        .data       = joypad_report_desc,
        .data_sz    = sizeof(joypad_report_desc),
    }, {
        .value      = USB_DESC_TYPE_STRING<<8 | STRING_DESC_IDX_LANG,
        .index      = 0,
        .data       = &str_desc_lang,
        .data_sz    = sizeof(str_desc_lang),
    }, {
        .value      = USB_DESC_TYPE_STRING<<8 | STRING_DESC_IDX_PROD,
        .index      = 0x0409,
        .data       = &str_desc_prod,
        .data_sz    = sizeof(str_desc_prod),
    }, {
        .value      = USB_DESC_TYPE_STRING<<8 | STRING_DESC_IDX_MANUF,
        .index      = 0x0409,
        .data       = &str_desc_manuf,
        .data_sz    = sizeof(str_desc_manuf),
    }, {
        .value      = USB_DESC_TYPE_LAST,
        .index      = 0,
        .data       = NULL,
        .data_sz    = 0,
    },
};

static inline void usb_stall(void)
{
    UECONX = (1<<STALLRQ) | (1<<EPEN);
}

static inline void usb_int_ack(void)
{
    UEINTX = ~(1<<TXINI);
}

static inline void usb_wait_in(void)
{
    while (!(UEINTX & (1<<TXINI)))
        ;
}

static inline void usb_reset_endpoint(uint8_t ep)
{
    UERST = 1<<ep;
    UERST = 0;
}

static inline void usb_cfg_ep(uint8_t ep, const struct usb_ep_cfg* cfg)
{
    UENUM = ep;
    UECONX = cfg->ueconx;
    if (cfg->ueconx & (1<<EPEN)) {
        UECFG0X = cfg->uecfg0x;
        UECFG1X = cfg->uecfg1x;
    }
}

static inline void usb_fifo_read(unsigned char *dest, size_t sz)
{
    while (sz--)
        *(dest++) = UEDATX;
}

static inline void usb_fifo_write_raw(const unsigned char *src, uint8_t sz)
{
    while (sz--)
        UEDATX = *(src++);
}

static inline void usb_fifo_write_control(const unsigned char *src, size_t sz)
{
    uint8_t i;
    while (sz) {
        /* Wait for space in the fifo */
        while (!((i = UEINTX) & ((1<<TXINI) | (1<<RXOUTI))))
            ;
        /* Fail if data remains */
        if (i & (1<<RXOUTI))
            return;
        usb_fifo_write_raw(src, MIN(sz, 32));
        src += MIN(sz, 32);
        i = MIN(sz, 32);
        usb_int_ack();
        sz -= i;
    }
}

/* Device interrupt */
ISR(USB_GEN_vect)
{
    uint8_t status;

    status = UDINT;
    UDINT = 0;

    /* End of reset interrupt */
    if (status & (1<<EORSTI)) {
        usb_cfg_ep(0, &usb_ep_cfgs[0]);
        /* Enable received setup interrupt */
        UEIENX = (1<<RXSTPE);
        usb_configuration = 0;
    }
}

static inline void usb_req_clear_feature(struct usb_request *usb_req)
{
    uint8_t i = usb_req->index & 0x7f;

    if (i < 1 || i > 4)
        return;

    usb_int_ack();
    UENUM = i;
    usb_stall();
    usb_reset_endpoint(i);
}

static inline void usb_req_set_address(struct usb_request *usb_req)
{
    usb_int_ack();
    usb_wait_in();
    UDADDR = usb_req->value | (1<<ADDEN);
}

static inline void usb_req_get_descriptor(struct usb_request *usb_req)
{
    uint8_t len;
    struct usb_descriptor *desc;

    /* Look for the matching descriptor */
    for (desc = descriptors; desc->value != USB_DESC_TYPE_LAST; ++desc) {
        if (usb_req->value == desc->value && usb_req->index == desc->index)
            break;
    }

    if (desc->value == USB_DESC_TYPE_LAST) {
        usb_stall();
        printf("no descriptor found\n");
        return;
    }

    len = MIN(usb_req->length, 255);
    len = MIN(len, desc->data_sz);
    usb_fifo_write_control(desc->data, len);
}

static inline void usb_req_set_configuration(struct usb_request *usb_req)
{
    uint8_t i;

    usb_configuration = usb_req->value;
    usb_int_ack();

    /* Configure the endpoints */
    for (i = 1; i < ARRAY_LEN(usb_ep_cfgs); ++i) {
        usb_cfg_ep(i, usb_ep_cfgs + i);
        usb_reset_endpoint(i);
    }
}

static inline void usb_hid_req_set_idle(struct usb_request *usb_req)
{
    /* Idle support is optional for gamepads */
    if (usb_req->value>>8 != 0)
        usb_stall();
    usb_int_ack();
}

static inline void usb_hid_req_get_report(struct usb_request *usb_req)
{
    usb_wait_in();
    usb_fifo_write_raw((void *)&joypad_report, sizeof(joypad_report));
    usb_int_ack();
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
    UENUM = GAMEPAD_EP;
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
        UENUM = GAMEPAD_EP;
    }
    usb_fifo_write_raw((void *)&joypad_report, sizeof(joypad_report));
    UEINTX = (1<<RWAL) | (1<<NAKOUTI) | (1<<RXSTPI) | (1<<STALLEDI);
    SREG = status;
    return 0;
}


/*
 * A bit in the controller state is encoded into four bits. This decodess two
 * original bits from a byte received from the controller.
 */

#define ENC_BIT_THRESHOLD 2 /* This seems to work the best */

static uint8_t controller_decode_byte(uint8_t v)
{
    uint8_t b0, b1;

    /*
     * In the correct case the middle bits should be the most important in
     * determining the value of the encoded bit.
     */
    b0 = (v & 1);
    b0 += ((v>>1) & 1) * 2;
    b0 += ((v>>2) & 1) * 2;
    b0 += (v>>3) & 1;

    b1 = ((v>>4) & 1);
    b1 += ((v>>5) & 1) * 2;
    b1 += ((v>>6) & 1) * 2;
    b1 += (v>>7) & 1;

    return (b0 > ENC_BIT_THRESHOLD) | ((b1 > ENC_BIT_THRESHOLD)<<1);
}

static inline void controller_decode_state(uint8_t *buf,
                                           volatile struct joypad_report *report)
{
    unsigned char byte;

    for (uint16_t i = 0; i < sizeof(*report); ++i) {
        byte = 0;
        byte |= (controller_decode_byte(buf[i * 4]))<<6;
        byte |= (controller_decode_byte(buf[i * 4 + 1]))<<4;
        byte |= (controller_decode_byte(buf[i * 4 + 2]))<<2;
        byte |= (controller_decode_byte(buf[i * 4 + 3]))<<0;
        ((char *)report)[i] = byte;
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

    /* Make sure the pin is down because external pull up resistors are used */
    /* The pin state is changed by pulling it down with DDR reg */
    CONTROLLER_DATA_PORT &= ~(1<<CONTROLLER_DATA_BIT);

    controller_probe();

    for (;;) {
        _delay_ms(8);

        controller_poll((uint16_t)&controller_buffer);

        if (controller_buffer[0] != 0x11) {
            _delay_ms(12);
            controller_probe();
            continue;
        }

        /*
         * The decoded packet from the controller is used as the HID report.
         * The HID report descriptor specifies the field order and padding
         * identitcal to the decoded controller packet. Only minor modifications
         * like offsetting and axis flipping are required to pass the decoded
         * packet as a HID report.
         */
        controller_decode_state(controller_buffer, &joypad_report);

        joypad_report.buttons_0 = joypad_report.buttons_0;
        joypad_report.buttons_1 = joypad_report.buttons_1;
        joypad_report.joy_x += 127;
        joypad_report.joy_y = 127 - joypad_report.joy_y;
        joypad_report.c_x += 127;
        joypad_report.c_y = 127 - joypad_report.c_y;
        joypad_report.l = joypad_report.l;
        joypad_report.r = joypad_report.r;

        usb_joypad_send();
    }
}
