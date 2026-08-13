/* End to end mouse decode.
 *
 * Parses the Gameball trackball interface, then pushes real 5-byte reports through
 * extract_report_values(), which tools/lift.py pulls verbatim out of the target's
 * mouse.c. Expected values are worked out by hand from the descriptor, so this
 * fails if either the parser or the extraction changes meaning.
 */
#include "main.h"
#include "descriptors.h"

/* mirrors the dispatch in usb.c:tuh_hid_report_received_cb */
static const char *dispatch(hid_interface_t *iface, uint8_t itf_protocol, uint8_t *report) {
    if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = iface->uses_report_id ? report[0] : 0;

        if (report_id < MAX_REPORTS && iface->report_handler[report_id])
            return iface->report_handler[report_id] == process_mouse_report
                       ? "report_handler[0] -> process_mouse_report"
                       : "report_handler -> WRONG RECEIVER";
        return "DROPPED (no handler)";
    }
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
        return "process_keyboard_report  <-- WRONG";
    if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
        return "process_mouse_report (direct)";
    return "DROPPED";
}

typedef struct {
    const char *what;
    uint8_t     report[5];
    int32_t     x, y, wheel, pan, buttons;
} case_t;

static const case_t cases[] = {
    {"ball right (X +20)",         {0x00, 0x14, 0x00, 0x00, 0x00},   20,    0,  0,  0,  0},
    {"ball left  (X -20)",         {0x00, 0xEC, 0x00, 0x00, 0x00},  -20,    0,  0,  0,  0},
    {"ball down  (Y +20)",         {0x00, 0x00, 0x14, 0x00, 0x00},    0,   20,  0,  0,  0},
    {"ball up    (Y -20)",         {0x00, 0x00, 0xEC, 0x00, 0x00},    0,  -20,  0,  0,  0},
    {"ball diagonal up-left",      {0x00, 0xF6, 0xF6, 0x00, 0x00},  -10,  -10,  0,  0,  0},
    {"ball fast (X +127)",         {0x00, 0x7F, 0x00, 0x00, 0x00},  127,    0,  0,  0,  0},
    {"ball fast (X -128)",         {0x00, 0x80, 0x00, 0x00, 0x00}, -128,    0,  0,  0,  0},

    {"side pad: scroll up",        {0x00, 0x00, 0x00, 0x01, 0x00},    0,    0,  1,  0,  0},
    {"side pad: scroll down",      {0x00, 0x00, 0x00, 0xFF, 0x00},    0,    0, -1,  0,  0},
    {"side pad: scroll down fast", {0x00, 0x00, 0x00, 0xFB, 0x00},    0,    0, -5,  0,  0},
    {"side pad: pan right",        {0x00, 0x00, 0x00, 0x00, 0x01},    0,    0,  0,  1,  0},
    {"side pad: pan left",         {0x00, 0x00, 0x00, 0x00, 0xFF},    0,    0,  0, -1,  0},
    {"both pads at once",          {0x00, 0x00, 0x00, 0x02, 0xFE},    0,    0,  2, -2,  0},

    {"button 1 (left)",            {0x01, 0x00, 0x00, 0x00, 0x00},    0,    0,  0,  0,  1},
    {"button 2 (right)",           {0x02, 0x00, 0x00, 0x00, 0x00},    0,    0,  0,  0,  2},
    {"button 3 (middle)",          {0x04, 0x00, 0x00, 0x00, 0x00},    0,    0,  0,  0,  4},
    {"button 4",                   {0x08, 0x00, 0x00, 0x00, 0x00},    0,    0,  0,  0,  8},
    {"button 5",                   {0x10, 0x00, 0x00, 0x00, 0x00},    0,    0,  0,  0, 16},
    {"all five buttons",           {0x1F, 0x00, 0x00, 0x00, 0x00},    0,    0,  0,  0, 31},

    {"drag: btn1 + move",          {0x01, 0x0A, 0xF6, 0x00, 0x00},   10,  -10,  0,  0,  1},
    {"btn3 + scroll",              {0x04, 0x00, 0x00, 0x03, 0x00},    0,    0,  3,  0,  4},
    {"everything at once",         {0x1F, 0x7F, 0x81, 0x02, 0xFE},  127, -127,  2, -2, 31},
};

int main(void) {
    static hid_interface_t iface;
    device_t state = {0};

    memset(&iface, 0, sizeof(iface));
    iface.protocol = HID_PROTOCOL_REPORT;
    parse_report_descriptor(&iface, d_gameball_trackball, sizeof(d_gameball_trackball));

    printf("Gameball trackball interface (mi_00)\n");
    printf("  mouse.is_found = %d\n", iface.mouse.is_found);
    printf("  num_keyboards  = %u\n", iface.num_keyboards);
    printf("  uses_report_id = %d\n\n", iface.uses_report_id);

    uint8_t probe[5] = {0};
    printf("Dispatch (src/usb.c), both ways this interface can present itself:\n");
    printf("  bInterfaceProtocol = MOUSE : %s\n", dispatch(&iface, HID_ITF_PROTOCOL_MOUSE, probe));
    printf("  bInterfaceProtocol = NONE  : %s\n\n", dispatch(&iface, HID_ITF_PROTOCOL_NONE, probe));

    printf("  %-26s %-18s %6s %6s %6s %6s %4s\n", "movement", "raw report", "X", "Y", "wheel",
           "pan", "btn");
    printf("  ");
    for (int i = 0; i < 88; i++)
        printf("-");
    printf("\n");

    int failures = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
        mouse_values_t v = {0};
        uint8_t        report[5];

        memcpy(report, cases[i].report, sizeof(report));
        extract_report_values(report, sizeof(report), &state, &v, &iface);

        int ok = v.move_x == cases[i].x && v.move_y == cases[i].y && v.wheel == cases[i].wheel &&
                 v.pan == cases[i].pan && v.buttons == cases[i].buttons;
        if (!ok)
            failures++;

        printf("  %-26s %02X %02X %02X %02X %02X    %6d %6d %6d %6d %4d   %s\n", cases[i].what,
               report[0], report[1], report[2], report[3], report[4], v.move_x, v.move_y, v.wheel,
               v.pan, v.buttons, ok ? "ok" : "MISMATCH");
    }

    printf("\n  %u/%u cases decoded as the descriptor specifies\n",
           (unsigned)ARRAY_SIZE(cases) - failures, (unsigned)ARRAY_SIZE(cases));
    return failures ? 1 : 0;
}
