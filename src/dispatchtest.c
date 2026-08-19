/* Which receiver does a report actually reach?
 *
 *   ./dispatchtest          every routing case, print a table
 *
 * Every other target here starts after the report has arrived somewhere. This one
 * asks the question before that: given an interface and the bytes on the wire,
 * which of the four process_*_report functions does usb.c hand them to? The
 * routing is modelled in src/dispatch.h, shared with mousetest.
 *
 * WHY THIS EXISTS
 *
 * usb.c decides between two branches on iface->uses_report_id, which the parser
 * sets from the *descriptor* at enumeration and nothing ever revises. It does not
 * look at iface->protocol. So when a keyboard is put into boot protocol - which is
 * what force_kbd_boot_protocol does - the device stops sending a report ID, but
 * dispatch keeps reading report[0] as one. report[0] is now the modifier byte, and
 * the report goes to report_handler[modifier].
 *
 * On a keyboard whose descriptor declares no report ID that is harmless: the
 * else-if branch runs and the receiver is picked from the interface protocol. On
 * one that does declare a report ID, the result ranges from the keystroke being
 * dropped to it being handed to the consumer or system receiver. The rows below
 * measure that on the real devices in the corpus rather than arguing it.
 *
 * This target FAILS on firmware that has the bug, which is every branch measured
 * so far including deskhop-extended - usb.c is byte-identical to upstream there.
 * It belongs in `make findings`, not `make test`, for the same reason truncate and
 * shortreport do: its exit status is the finding.
 */
#include "main.h"
#include "descriptors.h"
#include "dispatch.h"

typedef struct {
    const char      *what;
    const uint8_t   *desc;
    int              desc_len;
    uint8_t          itf_protocol;  /* what bInterfaceProtocol says */
    uint8_t          protocol;      /* iface->protocol: REPORT or BOOT */
    uint8_t          report[8];
    int              len;
    process_report_f want;
    const char      *note;          /* set when a row passes for the wrong reason */
} route_case_t;

#define R HID_PROTOCOL_REPORT
#define B HID_PROTOCOL_BOOT
#define KBD HID_ITF_PROTOCOL_KEYBOARD
#define MSE HID_ITF_PROTOCOL_MOUSE
#define NON HID_ITF_PROTOCOL_NONE
#define D(x) d_##x, (int)sizeof(d_##x)

static const route_case_t cases[] = {
/* ---- report protocol: the control group, all of this must keep working ------ */
{"boot_keyboard, no report ID",        D(boot_keyboard),        KBD, R, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"nkro_keyboard on report ID 1",       D(nkro_keyboard),        KBD, R, {0x01,0x00,0x10}, 32,
                                       process_keyboard_report, NULL},
{"superlight2 rx on report ID 1",      D(superlight2_rx_keyboard), KBD, R, {0x01,0x00,0x01}, 17,
                                       process_keyboard_report, NULL},
{"gameball trackball, no report ID",   D(gameball_trackball),   MSE, R, {0x00,0x14,0x00}, 5,
                                       process_mouse_report, NULL},
{"hires_mouse on report ID 1",         D(hires_mouse),          MSE, R, {0x01,0x00,0x00}, 8,
                                       process_mouse_report, NULL},
{"bolt iface1, mouse on ID 2",         D(bolt_rx_iface1),       NON, R, {0x02,0x00,0x00}, 9,
                                       process_mouse_report, NULL},
{"bolt iface1, consumer on ID 3",      D(bolt_rx_iface1),       NON, R, {0x03,0xE9,0x00}, 5,
                                       process_consumer_report, NULL},
{"bolt iface1, system on ID 4",        D(bolt_rx_iface1),       NON, R, {0x04,0x01}, 2,
                                       process_system_report, NULL},
{"KC6000 consumer, no report ID",      D(cherry_kc6000_consumer), NON, R, {0x01,0x00}, 2,
                                       process_consumer_report, NULL},

/* ---- boot protocol: the device sends [modifier][reserved][6 keys], no ID ----- */
{"boot_keyboard in boot protocol",     D(boot_keyboard),        KBD, B, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report,
                                       "descriptor declares no report ID, so the else-if branch runs"},

/* the same three real devices, now in boot protocol. report[0] is the modifier. */
{"gameball kbd, boot, no modifier",    D(gameball_keyboard),    KBD, B, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"nkro_keyboard, boot, no modifier",   D(nkro_keyboard),        KBD, B, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"nkro_keyboard, boot, Left Ctrl",     D(nkro_keyboard),        KBD, B, {0x01,0x00,0x04}, 8,
                                       process_keyboard_report,
                                       "arrives only because modifier 0x01 equals a bound report ID"},
{"ultralink kbd, boot, no modifier",   D(ultralink_keyboard),   KBD, B, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"ultralink kbd, boot, Ctrl+Shift+Alt",D(ultralink_keyboard),   KBD, B, {0x07,0x00,0x04}, 8,
                                       process_keyboard_report,
                                       "arrives only because modifier 0x07 equals its report ID"},
{"superlight2 rx, boot, no modifier",  D(superlight2_rx_keyboard), KBD, B, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"superlight2 rx, boot, Ctrl+Shift",   D(superlight2_rx_keyboard), KBD, B, {0x03,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"superlight2 rx, boot, Alt",          D(superlight2_rx_keyboard), KBD, B, {0x04,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
};

#undef D
#undef R
#undef B
#undef KBD
#undef MSE
#undef NON

static const char *itf_name(uint8_t p) {
    return p == HID_ITF_PROTOCOL_KEYBOARD ? "kbd"
         : p == HID_ITF_PROTOCOL_MOUSE    ? "mouse" : "none";
}

int main(void) {
    static hid_interface_t iface;
    int failures = 0, accidents = 0;

    printf("  %-38s %-6s %-6s %-9s %-9s %s\n", "scenario", "itf", "proto", "reached", "wanted", "");
    printf("  ");
    for (int i = 0; i < 92; i++)
        printf("-");
    printf("\n");

    for (unsigned i = 0; i < ARRAY_SIZE(cases); i++) {
        const route_case_t *c = &cases[i];

        memset(&iface, 0, sizeof(iface));
        iface.protocol = c->protocol;
        parse_report_descriptor(&iface, c->desc, c->desc_len);

        /* iface->protocol is what the firmware would hold after
           tuh_hid_set_protocol_complete_cb; uses_report_id stays as the parser set
           it, because nothing in usb.c revises it. That pairing is the bug. */
        process_report_f got = hid_route(&iface, c->itf_protocol, c->report);

        bool ok = (got == c->want);
        if (!ok)
            failures++;
        else if (c->note)
            accidents++;

        printf("  %-38s %-6s %-6s %-9s %-9s %s\n", c->what, itf_name(c->itf_protocol),
               c->protocol == HID_PROTOCOL_BOOT ? "boot" : "report",
               hid_receiver_name(got), hid_receiver_name(c->want),
               ok ? (c->note ? "ok, by luck" : "ok") : "MISROUTED");

        if (c->note)
            printf("  %-38s %s\n", "", c->note);
    }

    unsigned total = (unsigned)ARRAY_SIZE(cases);
    printf("\n  %u/%u reports reached the receiver they should\n", total - failures, total);

    if (accidents)
        printf("  %d more reached it only because the modifier happened to match a report ID\n",
               accidents);

    if (failures) {
        printf("\n  %d MISROUTED. A keyboard in boot protocol sends no report ID, but usb.c still\n",
               failures);
        printf("  reads report[0] as one, because iface->uses_report_id comes from the descriptor\n");
        printf("  and is never revised. extract_kbd_data's HID_PROTOCOL_BOOT branch is therefore\n");
        printf("  unreachable for every keyboard that declares a report ID.\n");
        return 1;
    }

    printf("  every report reached its receiver\n");
    return 0;
}
