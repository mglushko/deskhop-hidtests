/* The four report receivers live in mouse.c / keyboard.c and are referenced by the
   usage map in hid_report.c. They play no part in parsing a descriptor, so empty
   bodies are enough to link.

   What the tests actually use is their addresses. extract_data() stores one of
   these into iface->report_handler[report_id], and dump.c reads that array back to
   print which receiver each report ID got wired to - the handlers:MMK... line. So
   these have to stay four distinguishable functions: aliasing them, or routing them
   through one shared implementation, would make those addresses compare equal and
   the handler column would quietly become meaningless.

   Four identical empty bodies are the kind of thing -fipa-icf exists to merge, and
   that pass is on by default at -O2 and -Os. Measured on GCC 15 it does not fire
   here - the four keep distinct addresses at -O2 and under -flto, with and without
   the attribute below - so this has never actually been broken. But nothing in the
   source said the addresses had to stay distinct; it was resting on the optimiser
   choosing not to, and on the -O1 in CFLAGS. noipa says it outright, and costs
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

KEEP_DISTINCT void process_mouse_report(uint8_t *report, int len, uint8_t itf,
                                        hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}

KEEP_DISTINCT void process_keyboard_report(uint8_t *report, int len, uint8_t itf,
                                           hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}

KEEP_DISTINCT void process_consumer_report(uint8_t *report, int len, uint8_t itf,
                                           hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}

KEEP_DISTINCT void process_system_report(uint8_t *report, int len, uint8_t itf,
                                         hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}
