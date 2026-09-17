/* The routing half of usb.c:tuh_hid_report_received_cb: given an interface and the bytes
 * that arrived, which receiver gets called. Two ways, and the harness prefers the first:
 *
 *   LIFTED. The callback itself cannot go through tools/lift.py (it reaches global_state
 *   and the TinyUSB host API, and computes a device_idx nothing here needs), but its
 *   decision is a pure function of iface, the interface protocol and report[0], so a
 *   target that has factored it out as pick_receiver() hands the harness the real thing.
 *   The Makefile detects that and lifts it.
 *
 *   MODELLED. With the routing still inlined in the callback there is nothing to lift, so
 *   the copy below stands in, reproducing upstream at 59577cc. dispatchtest prints which
 *   of the two it used, because a model can only report what it was written to say: the
 *   boot-routing bug survived exactly that way, in a display-only copy of these rules
 *   that mousetest carried and kept printing the old answer from.
 *
 * KEEP THIS IN STEP WITH usb.c. Last checked against upstream e5f8ae8, which keeps this
 * shape and reads the table through a 256-entry map. At 59577cc, in outline:
 *
 *     if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
 *         report_id = iface->uses_report_id ? report[0] : 0;
 *         if (report_id < MAX_REPORTS) receiver = iface->report_handler[report_id];
 *     }
 *     else if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) process_keyboard_report(...);
 *     else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)    process_mouse_report(...);
 *
 * The `report_id < MAX_REPORTS` guard is what dropped sculpt_rx_mouse. A target keyed by
 * value reads the table through get_report_handler(), and upstream since ce8abb6 through
 * report_receivers[]; hid_handler() in src/handlers.h does the same on all three shapes,
 * so the model needs no second copy for either fix. Note what it does NOT depend on:
 * iface->protocol. Boot protocol changes what the device puts on the wire, not which
 * branch runs. That is the whole of the boot-routing finding, see src/dispatchtest.c.
 */
#pragma once

#include "main.h"
#include "handlers.h"

#ifdef HARNESS_LIFT_DISPATCH

/* The target factored its routing into pick_receiver(), so tools/lift.py pulls that
   out of usb.c verbatim and the model below is not used at all. This is the only
   configuration in which the dispatch result describes the firmware rather than the
   harness's reading of it. */
process_report_f pick_receiver(const hid_interface_t *iface, uint8_t itf_protocol,
                               uint8_t const *report);

#define HID_ROUTE_IS_LIFTED 1

static inline process_report_f hid_route(const hid_interface_t *iface, uint8_t itf_protocol,
                                         const uint8_t *report) {
    return pick_receiver(iface, itf_protocol, report);
}

#else

#define HID_ROUTE_IS_LIFTED 0

/* The routing inlined in usb.c at 59577cc, outlined above, for a target with nothing to
   lift. `report` must hold at least one byte, as it does on any real transfer. */
static inline process_report_f hid_route(const hid_interface_t *iface, uint8_t itf_protocol,
                                         const uint8_t *report) {
    if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = 0;

        if (iface->uses_report_id)
            report_id = report[0];

        return hid_handler(iface, report_id);
    }

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
        return process_keyboard_report;

    if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
        return process_mouse_report;

    return NULL;
}

#endif

/* Name of a receiver, for tables. */
static inline const char *hid_receiver_name(process_report_f f) {
    if (f == NULL)                     return "(dropped)";
    if (f == process_mouse_report)     return "mouse";
    if (f == process_keyboard_report)  return "keyboard";
    if (f == process_consumer_report)  return "consumer";
    if (f == process_system_report)    return "system";
    return "(unknown)";
}
