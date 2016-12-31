#ifndef USB_H
#define USB_H

enum usb_endpoint_types {
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
    USB_DESC_TYPE_DEVICE        = 0x01,
    USB_DESC_TYPE_CONFIGURATION = 0x02,
    USB_DESC_TYPE_STRING        = 0x03,
    USB_DESC_TYPE_INTERFACE     = 0x04,
    USB_DESC_TYPE_ENDPOINT      = 0x05,
    USB_DESC_TYPE_HID           = 0x21,
    USB_DESC_TYPE_REPORT        = 0x22,
    USB_DESC_TYPE_PHYSICAL      = 0x23,
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


#endif
