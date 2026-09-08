#include "tusb.h"
#include "emu_desc.h"
#include "control.h"

/* Lab identity, separate from the real devices emulated by the other targets. */
static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCAFE, .idProduct = 0x4025, .bcdDevice = 0x0100,
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};

enum { CONFIG_LENGTH = TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN };
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_LENGTH, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    /* System Control is a non-boot collection, routed by report ID in DeskHop. */
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, SLEEPWAKE_DESC_LEN,
                       0x81, CFG_TUD_HID_EP_BUFSIZE, 5),
};
TU_VERIFY_STATIC(sizeof(configuration) == CONFIG_LENGTH, "wrong configuration length");

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration;
}
const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return sleepwake_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t reqlen) {
    if (instance != 0 || type != HID_REPORT_TYPE_INPUT || reqlen == 0)
        return 0;
    if (report_id == SLEEPWAKE_REPORT_ID) {
        /* TinyUSB adds the requested report ID before invoking this callback. */
        buffer[0] = sleepwake_current_report();
        return 1;
    }
    if (report_id == 0 && reqlen >= 2) {
        buffer[0] = SLEEPWAKE_REPORT_ID;
        buffer[1] = sleepwake_current_report();
        return 2;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                           const uint8_t *buffer, uint16_t size) {
    (void)instance; (void)report_id; (void)type; (void)buffer; (void)size;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t result[32];
    static const char *const strings[] = {
        "", "deskhop-hidtests", "DeskHop Sleep/Wake emulator", "BOOTSEL-SW-1",
    };
    uint8_t count = 0;
    if (index == 0) {
        result[1] = 0x0409;
        count = 1;
    } else {
        if (index >= sizeof(strings) / sizeof(strings[0]))
            return NULL;
        while (strings[index][count] && count < 31) {
            result[count + 1] = strings[index][count];
            count++;
        }
    }
    result[0] = (TUSB_DESC_STRING << 8) | (2 * count + 2);
    return result;
}
