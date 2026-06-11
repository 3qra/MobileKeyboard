#include <cstring>

#include "tusb.h"

enum {
    ITF_NUM_HID = 0,
    ITF_NUM_TOTAL
};

enum {
    EPNUM_HID = 0x81
};

static uint8_t const hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return hid_report_descriptor;
}

static tusb_desc_device_t const device_descriptor = {
    sizeof(tusb_desc_device_t),
    TUSB_DESC_DEVICE,
    0x0200,
    0x00,
    0x00,
    0x00,
    CFG_TUD_ENDPOINT0_SIZE,
    0xCafe,
    0x4020,
    0x0100,
    0x01,
    0x02,
    0x03,
    0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return reinterpret_cast<uint8_t const *>(&device_descriptor);
}

static uint8_t const config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(hid_report_descriptor),
                       EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return config_descriptor;
}

static char const *const string_descriptors[] = {
    nullptr,
    "MobileKeyboard",
    "WiFi HID Keyboard",
    "000001"
};

static uint16_t string_descriptor[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;

    uint8_t chr_count;
    if (index == 0) {
        string_descriptor[1] = 0x0409;
        chr_count = 1;
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) {
            return nullptr;
        }

        const char *str = string_descriptors[index];
        chr_count = static_cast<uint8_t>(strlen(str));
        if (chr_count > 31) {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; i++) {
            string_descriptor[1 + i] = str[i];
        }
    }

    string_descriptor[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return string_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}
