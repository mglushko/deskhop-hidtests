/* The four report receivers live in mouse.c / keyboard.c and are referenced by the usage
   map in hid_report.c. They play no part in parsing a descriptor, so bodies that do no
   more than note they were called link.
   What the tests use is their addresses: extract_data() binds one to each report ID, and
   dump prints the handlers: line, cctest asserts the report ID under test is bound to the
   receiver it drives, kbdtest's slot check resolves every keyboard binding, and
   dispatchtest compares what the routing returns against them. Aliasing the four, or
   sharing one body, would make those addresses compare equal and every check meaningless.

   -fipa-icf exists to merge identical bodies and is on by default at -O2 and -Os. On GCC
   15 it does not fire here at -O2, even under -flto, with or without noipa below; but
   nothing in the source said the addresses had to stay distinct, and that rested on the
   optimiser choosing not to, and on the -O1 in CFLAGS. noipa says it outright, and costs
   nothing on four empty functions. */
#include "main.h"

/* noipa is GCC 8+ and has no Clang equivalent, so ask rather than assume. */
#if defined(__has_attribute)
#if __has_attribute(noipa)
#define KEEP_DISTINCT __attribute__((noipa))
#endif
#endif

#ifndef KEEP_DISTINCT
#define KEEP_DISTINCT
#endif

/* Which of the four the lifted tuh_hid_report_received_cb() called, read back by
   src/routing.c: NULL when it called none, which is what a dropped report means. */
process_report_f harness_reached;

KEEP_DISTINCT void process_mouse_report(uint8_t *report, int len, uint8_t itf,
                                        hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    harness_reached = process_mouse_report;
}

KEEP_DISTINCT void process_keyboard_report(uint8_t *report, int len, uint8_t itf,
                                           hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    harness_reached = process_keyboard_report;
}

/* cctest lifts these two out of the target's keyboard.c and links the real bodies
   instead, so it defines HARNESS_LIFT_CC to keep them out of here. Guarding rather
   than forking a second stubs file keeps the mouse and keyboard stubs, and the
   address-distinctness they depend on, in one place. */
#ifndef HARNESS_LIFT_CC

KEEP_DISTINCT void process_consumer_report(uint8_t *report, int len, uint8_t itf,
                                           hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    harness_reached = process_consumer_report;
}

KEEP_DISTINCT void process_system_report(uint8_t *report, int len, uint8_t itf,
                                         hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    harness_reached = process_system_report;
}

#endif
