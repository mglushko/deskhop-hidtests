/* USB descriptors for the 8BitDo Retro Mechanical Keyboard emulator.
 *
 * The report descriptor is the real device's 245 bytes, generated straight out
 * of the harness corpus by gen_desc.py. The interface around it is deliberately
 * bInterfaceSubClass = 0, bInterfaceProtocol = 0 (NONE), which is both what a
 * six-collection composite interface really looks like and what keeps deskhop
 * on the report-ID path: HID_ITF_PROTOCOL_NONE routes through
 * iface->report_handler[report_id], which is exactly the code the collection
 * collapse breaks. Declaring it as a boot keyboard instead would risk the host
 * putting it in boot protocol, where reports carry no ID and the bug is
 * unreachable.
 */
#include "tusb.h"
#include "bitdo_desc.h"

/* The real device's IDs, so deskhop sees byte-for-byte what the reporters of
   hrvach/deskhop#57 and #295 plugged in. Nothing in deskhop keys off VID/PID;
   change these freely if a cloned ID upsets a host's device cache. */
#define EMU_VID 0x2DC8
#define EMU_PID 0x5201

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = EMU_VID,
    .idProduct          = EMU_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return bitdo_report_desc;
}

enum { ITF_NUM_HID, ITF_NUM_TOTAL };

#define EPNUM_HID     0x81
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* boot_protocol = 0 gives bInterfaceSubClass 0 and bInterfaceProtocol 0.
       10 ms polling matches an ordinary keyboard. */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, BITDO_DESC_LEN,
                       EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* Named so nobody mistakes the emulator for the real hardware on a bus scan. */
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "deskhop-hidtests",
    "8BitDo Retro emulator",
    "COLLAPSE-1",
};

static uint16_t _desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))
            return NULL;

        const char *str = string_desc_arr[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++)
            _desc_str[1 + i] = str[i];
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
