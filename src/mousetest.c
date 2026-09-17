/* End to end mouse decode.
 *
 * Parses a pointing device's descriptor, then pushes real reports through
 * extract_report_values(), which tools/lift.py pulls verbatim out of the target's
 * mouse.c. Expected values are worked out by hand from the descriptor, so this fails if
 * either the parser or the extraction changes meaning.
 *
 * The devices are chosen for the shapes they exercise: 8-bit fields with no report ID,
 * 12-bit X/Y split across two report IDs or packed so Y never lands on a byte boundary,
 * vendor bytes between the buttons and the axes, two mouse collections in one
 * descriptor, and 16-bit axes on report ID 0x1A, which is 26. cases_mouse.h says which
 * device is which. cherry_mw8c_mouse runs the Kensington's cases because its pointer
 * section is byte for byte the same; if that ever stops being true, this notices.
 *
 * Each report is decoded from an exact-size allocation, as in truncate.c, so ASan's
 * redzone catches a read past its end rather than letting it pass as a plausible number.
 */
#include "main.h"
#include "dispatch.h"
#include "support.h"

/* Routing comes from src/dispatch.h, shared with dispatchtest, so the two cannot disagree
   about what usb.c does. Display only, never asserted on: a mouse test should fail on
   decode, not on routing, and src/dispatchtest.c is what asserts on it. */
static void print_dispatch(const hid_interface_t *iface, uint8_t itf_protocol,
                           const uint8_t *report, int len) {
    process_report_f got = hid_route(iface, itf_protocol, report, len);

    printf("%s%s\n", hid_receiver_name(got),
           got == process_mouse_report || got == NULL ? "" : " - WRONG RECEIVER");
}

#include "cases_mouse.h"

static int run_device(const mouse_device_t *dev) {
    static hid_interface_t iface;
    device_t state = {0};

    parse_iface(&iface, dev->desc, dev->desc_len, dev->protocol);

    printf("%s (%d bytes)%s\n", dev->name, dev->desc_len,
           dev->protocol == HID_PROTOCOL_BOOT ? ", boot protocol" : "");
    printf("  mouse.is_found = %d, mouse report id = %u\n", iface.mouse.is_found,
           iface.mouse.move_x.report_id);
    printf("  num_keyboards  = %u\n", iface.num_keyboards);
    printf("  uses_report_id = %d\n\n", iface.uses_report_id);

    if (dev->count) {
        printf("  Dispatch (src/usb.c), both ways this interface can present itself:\n");
        printf("    bInterfaceProtocol = MOUSE : ");
        print_dispatch(&iface, HID_ITF_PROTOCOL_MOUSE, dev->cases[0].report, dev->cases[0].len);
        printf("    bInterfaceProtocol = NONE  : ");
        print_dispatch(&iface, HID_ITF_PROTOCOL_NONE, dev->cases[0].report, dev->cases[0].len);
        printf("\n");
    }

    printf("  %-34s %-24s %6s %6s %6s %6s %4s\n", "movement", "raw report", "X", "Y", "wheel",
           "pan", "btn");
    print_rule(99);

    int failures = 0;
    for (unsigned i = 0; i < dev->count; i++) {
        const mouse_case_t  *c = &dev->cases[i];
        mouse_values_t v = {0};

        /* A len past the end of the array would overread the struct here, in the one
           file whose job is catching that; one below the receiver's floor would assert
           an input the firmware never delivers. */
        if (!case_len_ok(c->len, MOUSE_MIN_LEN, sizeof(c->report))) {
            printf("  %-34s len %d outside %d..%zu - fix the case\n", c->what, c->len,
                   MOUSE_MIN_LEN, sizeof(c->report));
            failures++;
            continue;
        }

        /* exact-size allocation: an overread lands in ASan's redzone, not in the
           next case's bytes */
        uint8_t *report = dup_exact("mousetest", c->report, c->len);

        extract_report_values(report, c->len, &state, &v, &iface);

        free(report);

        int ok = v.move_x == c->x && v.move_y == c->y && v.wheel == c->wheel && v.pan == c->pan &&
                 v.buttons == c->buttons;
        if (!ok)
            failures++;

        printf("  %-34s ", c->what);
        print_hex(c->report, c->len, 24);
        printf(" %6d %6d %6d %6d %4d   %s\n", v.move_x, v.move_y, v.wheel, v.pan, v.buttons,
               ok ? "ok" : "MISMATCH");
    }

    printf("\n  %u/%u cases decoded as the descriptor specifies\n\n",
           dev->count - failures, dev->count);
    return failures;
}

#ifdef HARNESS_IFACE_MOUSE_BUTTONS
/* Where a movement report's buttons come from when the report has none. With two pointing
   devices attached (a keyboard's mouse keys plus a trackball, the setup upstream #287 is
   about) the host is told the union, held in state->mouse_buttons, and that is the wrong
   thing for one device's decode to read back: a device declaring its buttons under a
   report ID of its own sends movement reports with no button field, and the union would
   leave another device's buttons held in this one's stored state after it let go. So the
   fallback reads the interface it was handed. Three real descriptors, each with a
   different stored value and union, so reaching for the wrong one or a hardcoded zero
   fails here. Only built where the target has the per-interface field; without it the
   fallback is state->mouse_buttons and these cases would assert the bug. */
typedef struct {
    const char    *name;
    const uint8_t *desc;
    int            desc_len;
    uint8_t        report[8];
    int            len;
    uint8_t        stored;   /* what this interface was last seen holding */
    int16_t        union_;   /* what every device together is holding */
    int32_t        want;
} fallback_case_t;

static const fallback_case_t fallback_cases[] = {
    /* Buttons on report 1, X/Y on report 2: the movement report carries no button field,
       so the answer can only come from what this interface last held. */
    {"kensington, r2 ball right", d_kensington_expert_mouse, sizeof(d_kensington_expert_mouse),
     {0x02, 0x01, 0x00, 0x00}, 4, 0x01, 0x07, 0x01},
    {"kensington, r2 nothing held", d_kensington_expert_mouse, sizeof(d_kensington_expert_mouse),
     {0x02, 0x01, 0x00, 0x00}, 4, 0x00, 0x07, 0x00},

    /* These two carry buttons in every report, so the wire wins and neither the stored
       state nor the union may leak in. */
    {"gameball, moving with none held", d_gameball_trackball, sizeof(d_gameball_trackball),
     {0x00, 0x14, 0x00, 0x00, 0x00}, 5, 0x01, 0x07, 0x00},
    {"ultralink, moving with none held", d_ultralink_mouse, sizeof(d_ultralink_mouse),
     {0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0x01, 0x07, 0x00},
};

static int run_button_fallback(void) {
    int failures = 0;

    printf("buttons on a movement report that does not carry them\n\n");
    printf("  %-34s %6s %6s %6s %6s\n", "case", "stored", "union", "got", "want");
    print_rule(62);

    for (unsigned i = 0; i < ARRAY_SIZE(fallback_cases); i++) {
        const fallback_case_t *c = &fallback_cases[i];
        static hid_interface_t iface;
        device_t       state = {0};
        mouse_values_t v     = {0};

        parse_iface(&iface, c->desc, c->desc_len, HID_PROTOCOL_REPORT);

        iface.mouse_buttons = c->stored;
        state.mouse_buttons = c->union_;

        /* the same bounds as the main loop, for the same reasons */
        if (!case_len_ok(c->len, MOUSE_MIN_LEN, sizeof(c->report))) {
            printf("  %-34s len %d outside %d..%zu - fix the case\n", c->name, c->len,
                   MOUSE_MIN_LEN, sizeof(c->report));
            failures++;
            continue;
        }

        /* exact-size allocation, as above */
        uint8_t *report = dup_exact("mousetest", c->report, c->len);
        extract_report_values(report, c->len, &state, &v, &iface);
        free(report);

        int ok = v.buttons == c->want;
        if (!ok)
            failures++;

        printf("  %-34s %6u %6d %6d %6d   %s\n", c->name, c->stored, c->union_, v.buttons,
               c->want, ok ? "ok" : "MISMATCH");
    }

    printf("\n  %u/%u fell back to the interface that sent the report\n\n",
           (unsigned)ARRAY_SIZE(fallback_cases) - failures, (unsigned)ARRAY_SIZE(fallback_cases));
    return failures;
}
#endif

int main(void) {
    int failures = 0, total = 0;

    for (unsigned i = 0; i < ARRAY_SIZE(mouse_devices); i++) {
        failures += run_device(&mouse_devices[i]);
        total += mouse_devices[i].count;
    }

    printf("%d/%d cases across %u devices\n\n", total - failures, total,
           (unsigned)ARRAY_SIZE(mouse_devices));

#ifdef HARNESS_IFACE_MOUSE_BUTTONS
    failures += run_button_fallback();
#else
    printf("target has no per-interface mouse buttons, button fallback cases skipped\n");
#endif

    print_kept_out(mouse_kept_out, "");

    return failures ? 1 : 0;
}
