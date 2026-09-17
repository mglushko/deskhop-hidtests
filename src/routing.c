/* The harness's half of tuh_hid_report_received_cb's world, so usb.c's report callback
 * can be lifted whole by tools/lift.py rather than modelled. The callback reaches three
 * things outside the files under test: tuh_hid_interface_protocol(), which it asks for
 * the interface's bInterfaceProtocol; global_state, where it finds the interface by
 * device address and instance; and tuh_hid_receive_report(), which re-arms the transfer.
 * All three are here. What it then does is call one of the four receivers, or none, and
 * src/stubs.c records which in harness_reached.
 *
 * Why lift the callback rather than model its routing: a model reports what it was
 * written to say. The boot-protocol misrouting (hrvach/deskhop#363) lived on inside such
 * a copy, display-only and documented as able to go stale, which kept printing the old
 * answer. The callback itself cannot go stale.
 */
#include "main.h"

device_t global_state;

static uint8_t harness_itf_protocol;

uint8_t tuh_hid_interface_protocol(uint8_t dev_addr, uint8_t idx) {
    (void)dev_addr; (void)idx;
    return harness_itf_protocol;
}

bool tuh_hid_receive_report(uint8_t dev_addr, uint8_t idx) {
    (void)dev_addr; (void)idx;
    return true;
}

/* Device address 1, instance 0: the first slot of the table, in bounds on every tree.
   The callback's own guards on both values are part of what is lifted, so they run. The
   interface is copied in rather than parsed in place so callers keep their own copy;
   nothing in hid_interface_t points back into itself. */
process_report_f hid_route(const hid_interface_t *iface, uint8_t itf_protocol,
                           const uint8_t *report, int len) {
    global_state.iface[0][0] = *iface;
    harness_itf_protocol     = itf_protocol;
    harness_reached          = NULL;

    tuh_hid_report_received_cb(1, 0, report, (uint16_t)len);

    return harness_reached;
}
