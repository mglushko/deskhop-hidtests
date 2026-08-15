/* End to end mouse decode.
 *
 * Parses a pointing device's descriptor, then pushes real reports through
 * extract_report_values(), which tools/lift.py pulls verbatim out of the target's
 * mouse.c. Expected values are worked out by hand from the descriptor, so this
 * fails if either the parser or the extraction changes meaning.
 *
 * The devices, chosen for the shapes they exercise:
 *
 *   gameball_trackball      8-bit fields, no report ID at all
 *   kensington_expert_mouse 12-bit X/Y, and the pointer split across two report
 *                           IDs: buttons/wheel/pan on 1, X/Y on 2
 *   cherry_mw8_mouse        12-bit X/Y in one report, so Y starts at bit 20 and
 *                           never lands on a byte boundary
 *   mx518_mouse             two vendor bytes sitting between the buttons and the
 *                           axes, and the wheel declared before X/Y
 *   kernel_multi_collection two mouse collections in one descriptor, so the
 *                           decode runs against whichever one won
 *
 * cherry_mw8c_mouse runs the Kensington's cases because its pointer section is
 * byte for byte the same; if that ever stops being true, this notices.
 *
 * Each report is copied into an exact-size allocation before being decoded, the
 * same trick truncate.c uses, so ASan's redzone catches a read past the end of
 * the report rather than letting it pass as a plausible number.
 */
#include "main.h"

/* A hand copy of the routing in usb.c:tuh_hid_report_received_cb, and the only
   place in this harness that reimplements firmware logic instead of lifting it.
   tuh_hid_report_received_cb cannot be lifted: it reaches global_state and the
   TinyUSB host API, and computes a device_idx this test has no use for.

   The consequence is that it can go stale silently - nothing breaks if usb.c's
   routing changes, this just keeps printing the old answer. It is display only,
   printed in each device's header and never asserted on, so a drift cannot turn a
   failing case into a passing one. Re-check it against usb.c when touching either.
   Last checked against main at 59577cc: same branch structure, uses_report_id or
   PROTOCOL_NONE goes through report_handler[report[0]], otherwise the interface
   protocol picks the receiver directly. */
static const char *dispatch(hid_interface_t *iface, uint8_t itf_protocol, const uint8_t *report) {
    if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = iface->uses_report_id ? report[0] : 0;

        if (report_id < MAX_REPORTS && iface->report_handler[report_id])
            return iface->report_handler[report_id] == process_mouse_report
                       ? "report_handler -> process_mouse_report"
                       : "report_handler -> WRONG RECEIVER";
        return "DROPPED (no handler)";
    }
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
        return "process_keyboard_report  <-- WRONG";
    if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
        return "process_mouse_report (direct)";
    return "DROPPED";
}

#include "cases_mouse.h"

static int run_device(const mouse_device_t *dev) {
    static hid_interface_t iface;
    device_t state = {0};

    memset(&iface, 0, sizeof(iface));
    iface.protocol = dev->protocol;
    parse_report_descriptor(&iface, dev->desc, dev->desc_len);

    printf("%s (%d bytes)%s\n", dev->name, dev->desc_len,
           dev->protocol == HID_PROTOCOL_BOOT ? ", boot protocol" : "");
    printf("  mouse.is_found = %d, mouse report id = %u\n", iface.mouse.is_found,
           iface.mouse.move_x.report_id);
    printf("  num_keyboards  = %u\n", iface.num_keyboards);
    printf("  uses_report_id = %d\n\n", iface.uses_report_id);

    if (dev->count) {
        printf("  Dispatch (src/usb.c), both ways this interface can present itself:\n");
        printf("    bInterfaceProtocol = MOUSE : %s\n",
               dispatch(&iface, HID_ITF_PROTOCOL_MOUSE, dev->cases[0].report));
        printf("    bInterfaceProtocol = NONE  : %s\n\n",
               dispatch(&iface, HID_ITF_PROTOCOL_NONE, dev->cases[0].report));
    }

    printf("  %-26s %-24s %6s %6s %6s %6s %4s\n", "movement", "raw report", "X", "Y", "wheel",
           "pan", "btn");
    printf("  ");
    for (int i = 0; i < 91; i++)
        printf("-");
    printf("\n");

    int failures = 0;
    for (unsigned i = 0; i < dev->count; i++) {
        const mouse_case_t  *c = &dev->cases[i];
        mouse_values_t v = {0};
        char           hex[3 * sizeof(c->report) + 1];
        int            n = 0;

        /* A len past the end of the array would overread the struct here and
           overflow hex[] below, in the one file whose job is catching that. */
        if (c->len < 0 || (size_t)c->len > sizeof(c->report)) {
            printf("  %-26s len %d exceeds report[%zu] - fix the case\n", c->what, c->len,
                   sizeof(c->report));
            failures++;
            continue;
        }

        /* exact-size allocation: an overread lands in ASan's redzone, not in the
           next case's bytes */
        uint8_t *report = malloc(c->len);
        memcpy(report, c->report, c->len);

        extract_report_values(report, c->len, &state, &v, &iface);

        for (int b = 0; b < c->len; b++)
            n += sprintf(hex + n, "%02X ", report[b]);

        free(report);

        int ok = v.move_x == c->x && v.move_y == c->y && v.wheel == c->wheel && v.pan == c->pan &&
                 v.buttons == c->buttons;
        if (!ok)
            failures++;

        printf("  %-26s %-24s %6d %6d %6d %6d %4d   %s\n", c->what, hex, v.move_x, v.move_y,
               v.wheel, v.pan, v.buttons, ok ? "ok" : "MISMATCH");
    }

    printf("\n  %u/%u cases decoded as the descriptor specifies\n\n",
           dev->count - failures, dev->count);
    return failures;
}

int main(void) {
    int failures = 0, total = 0;

    for (unsigned i = 0; i < ARRAY_SIZE(mouse_devices); i++) {
        failures += run_device(&mouse_devices[i]);
        total += mouse_devices[i].count;
    }

    printf("%d/%d cases across %u devices\n", total - failures, total,
           (unsigned)ARRAY_SIZE(mouse_devices));
    return failures ? 1 : 0;
}
