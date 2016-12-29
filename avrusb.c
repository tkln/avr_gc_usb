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

static volatile uint8_t usb_configuration = 0;

static void usb_init(void)
{
#if 1
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

#else
    USBCON |= 1<<USBE;
    UDCON |= (1<<LSM);
    UDCON &= ~(1<<DETACH);

    UDIEN |= 1<<SOFE;

    UECONX |= 1<<EPEN;
#endif
}

#define EP_TYPE_CONTROL 0x00

enum {
    ENDPOINT_TYPE_CONTROL = 0,
    ENDPOINT_TYPE_ISOCHRONOUS = 1,
    ENDPOINT_TYPE_BULK = 2,
    ENDPOINT_TYPE_INTERRUPT = 3,
};

/* Device interrupt */
ISR(USB_GEN_vect)
{
    uint8_t status;

    status = UDINT;
    UDINT = 0;

    /* End of reset interrupt */
    if (status & (1<<EORSTI)) {
        /* Set the endpoint number */
        UENUM = 0;
        /* Enable the endpoint */
        UECONX = 1<<EPEN;

        /* Set endpoint type */
        UECFG0X = ENDPOINT_TYPE_CONTROL<<6;

        /* Allocate 32 bytes for the endpoint */
        UECFG1X = (1<<EPSIZE1) | (1<<ALLOC);

        /* Enable received setup interrupt */
        UEIENX = (1<<RXSTPE);
        
        usb_configuration = 0;
    }

    if ((status & (1<<SOFI)) && usb_configuration) {
        /* TODO */
        //PORT(LED2_BASE) &= ~LED2_PIN;
    }
}

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

static const struct usb_config_desc_final {
    struct usb_config_desc config; 
    struct usb_interface_desc interfaces[1];
} __attribute__((packed)) config_desc_final = {
    .config = {
        .length                 = sizeof(config_desc_final.config),
        .descriptor_type        = USB_DESC_TYPE_CONFIGURATION,
        .total_length           = sizeof(config_desc_final),
        .num_interfaces         = ARRAY_LEN(config_desc_final.interfaces),
        .configuration_value    = 1,
        .configuration_idx      = 0,
        .attributes             = (1<<USB_CFG_ATTR_RESERVED) |
                                  (1<<USB_CFG_ATTR_SELF_POWERED),
        .max_power              = 50
    },
    .interfaces = {
        {
            .length                 = sizeof(struct usb_interface_desc),
            .descriptor_type        = USB_DESC_TYPE_INTERFACE,
            .interface_number       = 0,
            .alternate_setting      = 0,
            .num_endpoints          = 0,
            .interface_class        = 0,
            .interface_sub_class    = 0,
            .interface_protocol     = 0,
            .interface_idx          = 0,
        },
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
USB_STRING_DESCRIPTOR(string1, L"lors");
USB_STRING_DESCRIPTOR(string2, L"lara");

/* TODO make this progmem */
static struct usb_descriptor {
    uint16_t value;
    uint16_t index;
    unsigned char *data;
    uint8_t data_sz;
} descriptors[] = {
    {
        .value = USB_DESC_TYPE_DEVICE<<8,
        .index = 0,
        .data = (void *)&device_descriptor,
        .data_sz = sizeof(device_descriptor)
    }, {
        .value = USB_DESC_TYPE_CONFIGURATION<<8,
        .index = 0,
        .data = (void *)&config_desc_final,
        .data_sz = sizeof(config_desc_final),
    }, {
        .value = USB_DESC_TYPE_STRING<<8,
        .index = 0,
        .data = (void *)&string0,
        .data_sz = sizeof(string0),
    }, {
        .value = USB_DESC_TYPE_STRING<<8 | 0x01,
        .index = 0x0409,
        .data = (void *)&string1,
        .data_sz = sizeof(string1),
    }, {
        .value = USB_DESC_TYPE_STRING<<8 | 0x02,
        .index = 0x0409,
        .data = (void *)&string2,
        .data_sz = sizeof(string2),
    }
};

static struct usb_request {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} __attribute__((packed)) usb_req;

static inline void usb_fifo_read(unsigned char *dest, size_t sz)
{
    while (sz--) 
        *(dest++) = UEDATX;
}

static inline void usb_fifo_write(unsigned char *src, size_t sz)
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

static inline void halt(void)
{
    cli();
    for (;;)
        ;
}

/* Endpoint interrupt */
ISR(USB_COM_vect)
{
    uint8_t status;
    uint8_t i;
    struct usb_descriptor *desc;

    UENUM = 0;
    status = UEINTX;

    /* Setup packet received  */
    if (status & (1<<RXSTPI)) {
        usb_fifo_read((void *)&usb_req, sizeof(usb_req));
        UEINTX = ~((1<<RXSTPI) | (1<<RXOUTI) | (1<<TXINI));
        printf("request: 0x%02x, value: 0x%04x, index: 0x%04x, len: 0x%04x\n",
               usb_req.request, usb_req.value, usb_req.index, usb_req.length);
        //usart_putchar(usb_req.request, NULL);
        switch (usb_req.request) {
            case USB_REQ_SET_ADDRESS:
                PORT(LED1_BASE) &= ~LED1_PIN;
                UEINTX = ~(1<<TXINI);
                while (!(UEINTX & (1<<TXINI))) ;
                UDADDR = usb_req.value | (1<<ADDEN);
                return;

            case USB_REQ_GET_DESCRIPTOR:
                /* Look for the matching descriptor */
                for (i = 0; ; ++i) {
                    if (i >= ARRAY_LEN(descriptors)) {
                        UECONX = (1<<STALLRQ) | (1<<EPEN);
                        printf("no descriptor found\n");
                        return;
                    }
                    desc = descriptors + i;
                    if (usb_req.value == desc->value&&
                        usb_req.index == desc->index)
                        break;
                }
                /* Wait for the host to get ready to accept data */
                uint8_t len = MIN(usb_req.length, 255);
                len = MIN(len, desc->data_sz);
                usb_fifo_write(desc->data, len);
                return;
                
            case USB_REQ_SET_CONFIGURATION:
                usb_configuration = usb_req.value;
                UEINTX = ~(1<<TXINI);
                /* TODO do the configuration of the other endpoints */
                break;
            default:
                printf("unhandled request\n");
                PORT(LED2_BASE) &= ~LED2_PIN;
                halt();
        }
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
