/* The four report receivers live in mouse.c / keyboard.c and are referenced by the
   usage map in hid_report.c. They play no part in parsing a descriptor, so counting
   stubs are enough. Tests compare against these addresses to check which receiver a
   report ID got wired to. */
#include "main.h"

int stub_mouse_reports, stub_keyboard_reports;
int stub_consumer_reports, stub_system_reports;

void process_mouse_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    stub_mouse_reports++;
}

void process_keyboard_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    stub_keyboard_reports++;
}

void process_consumer_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    stub_consumer_reports++;
}

void process_system_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface) {
    (void)report; (void)len; (void)itf; (void)iface;
    stub_system_reports++;
}
