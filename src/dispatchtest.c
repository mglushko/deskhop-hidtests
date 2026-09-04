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
 * A second cause, found later: on main the table is indexed by the report ID and has
 * MAX_REPORTS slots, so an ID of 24 or more is never bound and its reports are
 * dropped in report protocol too. The sculpt rows measure that one.
 *
 * This target FAILS on firmware that has either bug, which is every tree measured
 * so far; DeskHop Extended fixed the routing and not the table.
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

/* the same mistake on the mouse side, where report[0] is the button byte. Reached
   through force_mouse_boot_mode rather than force_kbd_boot_protocol. */
{"hires_mouse, boot, no button",       D(hires_mouse),          MSE, B, {0x00,0x0A,0x00}, 5,
                                       process_mouse_report, NULL},
{"hires_mouse, boot, button 1",        D(hires_mouse),          MSE, B, {0x01,0x0A,0x00}, 5,
                                       process_mouse_report,
                                       "arrives only because button 1 sets bit 0, matching report ID 1"},

/* Keychron Ultra-Link 8K 3434:D028 interface 0, which Windows reports as
   Class_03 SubClass_01 Prot_02 - a boot-capable mouse - so force_mouse_boot_mode
   (config field 71) reaches it on real hardware. Its mouse sits on report ID 1, so
   in boot protocol the pointer is dead unless the left button is held. */
{"ultralink mouse, boot, no button",   D(ultralink_mouse),      MSE, B, {0x00,0x14,0x00}, 4,
                                       process_mouse_report, NULL},
{"ultralink mouse, boot, left held",   D(ultralink_mouse),      MSE, B, {0x01,0x14,0x00}, 4,
                                       process_mouse_report,
                                       "arrives only because the left button sets bit 0, matching report ID 1"},

/* ---- report IDs above MAX_REPORTS: report protocol, nothing exotic on the wire -- */
/* Microsoft Sculpt receiver 045e:07a5 (issue #367). Its mouse lives on report ID
   0x1A, which is 26; on main the handler table has MAX_REPORTS (24) slots indexed by
   the ID itself, so no receiver is ever bound and the report is dropped whichever
   bInterfaceProtocol the interface carries. The keyboard and the consumer/system
   interfaces of the same receiver use IDs 0, 7 and 3 and route normally. */
{"sculpt rx mouse on ID 0x1A, itf mouse", D(sculpt_rx_mouse),   MSE, R, {0x1A,0x00,0x01,0x00}, 10,
                                       process_mouse_report, NULL},
{"sculpt rx mouse on ID 0x1A, itf none",  D(sculpt_rx_mouse),   NON, R, {0x1A,0x00,0x01,0x00}, 10,
                                       process_mouse_report, NULL},
{"sculpt rx keyboard, no report ID",   D(sculpt_rx_keyboard),   KBD, R, {0x00,0x00,0x04}, 8,
                                       process_keyboard_report, NULL},
{"sculpt rx consumer on ID 7",         D(sculpt_rx_consumer),   NON, R, {0x07,0xE9,0x00}, 8,
                                       process_consumer_report, NULL},
{"sculpt rx system on ID 3",           D(sculpt_rx_consumer),   NON, R, {0x03,0x82}, 2,
                                       process_system_report, NULL},

/* The same receiver after force_mouse_boot_mode: the wire now carries [buttons][x][y]
   with no ID, so report[0] is the button byte. On a tree that reads it as a report
   ID anyway, no button means slot 0 and the left button means slot 1, neither bound;
   on a tree that routes boot protocol by the interface the pointer arrives. */
{"sculpt rx mouse, boot, no button",   D(sculpt_rx_mouse),      MSE, B, {0x00,0x01,0x00}, 3,
                                       process_mouse_report, NULL},
{"sculpt rx mouse, boot, left held",   D(sculpt_rx_mouse),      MSE, B, {0x01,0x01,0x00}, 3,
                                       process_mouse_report, NULL},
};

#undef D
#undef R
#undef B
#undef KBD
#undef MSE
#undef NON

/* In boot protocol report[0] is data - a modifier byte on a keyboard, a button byte
   on a mouse - and never a report ID, so routing must not depend on it. If perturbing
   that byte moves the receiver, this row only landed on the right one because the
   value happened to match a bound report ID. Checked by behaviour rather than by
   knowing which routing is in use, so it stays true either way. */
static bool routed_by_luck(const hid_interface_t *iface, const route_case_t *c,
                           process_report_f got) {
    uint8_t probe[sizeof c->report];

    if (c->protocol != HID_PROTOCOL_BOOT || got == NULL)
        return false;

    memcpy(probe, c->report, sizeof probe);
    probe[0] = (uint8_t)(probe[0] ^ 0xA5);

    return hid_route(iface, c->itf_protocol, probe) != got;
}

static const char *itf_name(uint8_t p) {
    return p == HID_ITF_PROTOCOL_KEYBOARD ? "kbd"
         : p == HID_ITF_PROTOCOL_MOUSE    ? "mouse" : "none";
}

int main(void) {
    static hid_interface_t iface;
    int failures = 0, accidents = 0;

    printf("  routing: %s\n\n", HID_ROUTE_IS_LIFTED
           ? "lifted from the target's usb.c (pick_receiver)"
           : "MODELLED in src/dispatch.h - the target has no pick_receiver() to lift, so "
             "this\n           describes the model, not the firmware");

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

        bool ok   = (got == c->want);
        bool luck = ok && routed_by_luck(&iface, c, got);

        if (!ok)
            failures++;
        else if (luck)
            accidents++;

        printf("  %-38s %-6s %-6s %-9s %-9s %s\n", c->what, itf_name(c->itf_protocol),
               c->protocol == HID_PROTOCOL_BOOT ? "boot" : "report",
               hid_receiver_name(got), hid_receiver_name(c->want),
               ok ? (luck ? "ok, by luck" : "ok") : "MISROUTED");

        if (luck && c->note)
            printf("  %-38s %s\n", "", c->note);
    }

    unsigned total = (unsigned)ARRAY_SIZE(cases);
    printf("\n  %u/%u reports reached the receiver they should\n", total - failures, total);

    if (accidents)
        printf("  %d of those only because report[0] happened to match a bound report ID -\n"
               "  change the modifier or the button held and they stop arriving\n", accidents);

    if (failures) {
        printf("\n  %d MISROUTED. Two causes are known. A device in boot protocol sends no report\n",
               failures);
        printf("  ID, but usb.c still reads report[0] as one, because iface->uses_report_id comes\n");
        printf("  from the descriptor and is never revised; a keyboard's modifier byte and a\n");
        printf("  mouse's button byte are looked up as report IDs. And a table indexed by the\n");
        printf("  report ID never binds an ID of MAX_REPORTS or more, so those reports are\n");
        printf("  dropped in report protocol as well.\n");
        return 1;
    }

    if (HID_ROUTE_IS_LIFTED)
        printf("  every report reached its receiver, measured against the target's own routing\n");
    else
        printf("  every report reached its receiver - but see the routing note above\n");
    return 0;
}
