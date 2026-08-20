/* USB descriptors for the emulated devices.
 *
 * Every report descriptor here is generated out of ../descriptors.h by
 * gen_desc.py at build time, so the rig and the harness corpus cannot drift.
 * Which device this binary is depends on EMU_BITDO or EMU_GAMEBALL, set by
 * CMake, which builds both.
 *
 * The interface classes are chosen deliberately, not copied from a template.
 * deskhop routes on bInterfaceProtocol before it looks at anything else, so
 * getting these wrong moves the device onto a different code path and quietly
 * tests something other than what is intended. Each one is justified below.
 */
#include "tusb.h"
#include "emu_desc.h"

#if defined(EMU_BITDO)
#  define EMU_VID 0x2DC8   /* 8BitDo */
#  define EMU_PID 0x5201
#  define EMU_PRODUCT "8BitDo Retro emulator"
#  define EMU_SERIAL  "COLLAPSE-1"
#elif defined(EMU_GAMEBALL)
#  define EMU_VID 0x0782   /* Gameball */
#  define EMU_PID 0x001B
#  define EMU_PRODUCT "Gameball emulator"
#  define EMU_SERIAL  "USAGES-1"
#else
#  error "define EMU_BITDO or EMU_GAMEBALL"
#endif

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

/*============================================================================*/
#if defined(EMU_BITDO)

/* One interface, deliberately bInterfaceSubClass 0 and bInterfaceProtocol 0.
   That is what a six collection composite interface really looks like, and it
   keeps deskhop on the report ID path where the collection collapse lives.
   Declaring a boot keyboard would risk the host negotiating boot protocol,
   where reports carry no ID and the bug is unreachable. */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return bitdo_desc;
}

enum { ITF_NUM_HID, ITF_NUM_TOTAL };
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, BITDO_DESC_LEN,
                       0x81, CFG_TUD_HID_EP_BUFSIZE, 10),
};

/*============================================================================*/
#elif defined(EMU_GAMEBALL)

/* Three interfaces, in the order the real device presents them.
 *
 *   0  trackball  bInterfaceProtocol = MOUSE. This one matters. The descriptor
 *                 declares no report ID, so if the interface came up as NONE
 *                 then report_carries_id() is false and pick_receiver() falls
 *                 into the report_handler[report[0]] branch, where report[0] is
 *                 the button byte. The pointer would then be routed by which
 *                 buttons were held. Declaring MOUSE takes the direct branch
 *                 instead, which is both correct and what a real trackball does.
 *   1  gesture    Vendor page 0xFFE0, no boot anything, so subclass 0 and
 *                 protocol 0. This is the interface that carries the bug: its
 *                 first report declares Report Count 0x3FC8, which is 16328,
 *                 against a 128 entry usages[] array. Nothing is ever sent on
 *                 it. Enumerating is the whole test.
 *   2  keyboard   Boot keyboard. Note it declares eight modifier bits and 48
 *                 bits of padding and no key array at all, so it can report
 *                 modifiers and nothing else. That is the device's own doing,
 *                 not a simplification here.
 */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    switch (instance) {
        case 0:  return gameball_trackball_desc;
        case 1:  return gameball_gesture_desc;
        default: return gameball_keyboard_desc;
    }
}

enum { ITF_NUM_TRACKBALL, ITF_NUM_GESTURE, ITF_NUM_KEYBOARD, ITF_NUM_TOTAL };
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 3 * TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    TUD_HID_DESCRIPTOR(ITF_NUM_TRACKBALL, 0, HID_ITF_PROTOCOL_MOUSE,
                       GAMEBALL_TRACKBALL_DESC_LEN, 0x81, CFG_TUD_HID_EP_BUFSIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_NUM_GESTURE, 0, HID_ITF_PROTOCOL_NONE,
                       GAMEBALL_GESTURE_DESC_LEN, 0x82, CFG_TUD_HID_EP_BUFSIZE, 10),
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       GAMEBALL_KEYBOARD_DESC_LEN, 0x83, CFG_TUD_HID_EP_BUFSIZE, 10),
};

#endif
/*============================================================================*/

/* A config descriptor whose wTotalLength disagrees with the bytes that follow is
   accepted by the host right up until it tries to open the interfaces, which
   looks exactly like a device that enumerates and then does nothing. */
TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN,
                 "config descriptor length does not match its contents");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* Named so nobody mistakes the emulator for the real hardware on a bus scan. */
static char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "deskhop-hidtests",
    EMU_PRODUCT,
    EMU_SERIAL,
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
