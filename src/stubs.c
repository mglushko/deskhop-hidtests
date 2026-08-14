/* The four report receivers live in mouse.c / keyboard.c and are referenced by the
   usage map in hid_report.c. They play no part in parsing a descriptor, so empty
   bodies are enough to link.

   What the tests actually use is their addresses. extract_data() stores one of
   these into iface->report_handler[report_id], and dump.c reads that array back to
   print which receiver each report ID got wired to - the handlers:MMK... line. So
   these have to stay four distinguishable functions: aliasing them, or routing them
   through one shared implementation, would make those addresses compare equal and
   the handler column would quietly become meaningless. Four identical empty bodies
   are fine as they stand, since taking their addresses stops the compiler folding
   them together. */
#include "main.h"

void process_mouse_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}

void process_keyboard_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}

void process_consumer_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}

void process_system_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
}
