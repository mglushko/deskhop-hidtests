/* The routing half of usb.c:tuh_hid_report_received_cb: given an interface and the
 * bytes that arrived, which receiver gets called.
 *
 * Two ways to get that answer, and the harness prefers the first:
 *
 *   LIFTED. tuh_hid_report_received_cb itself cannot go through tools/lift.py - it
 *   reaches global_state and the TinyUSB host API, and computes a device_idx nothing
 *   here needs. But the decision inside it is a pure function of iface, the interface
 *   protocol and report[0], and a target that has factored it out as pick_receiver()
 *   hands the harness the real thing. The Makefile detects that and lifts it.
 *
 *   MODELLED. On a target that still has the routing inlined in the callback there is
 *   nothing to lift, so the copy below stands in. It reproduces upstream at 59577cc.
 *   dispatchtest prints which of the two it used, because a model can only report what
 *   it was written to say - and that is exactly how the boot-protocol routing bug
 *   survived: mousetest carried a copy of these rules, display-only and documented as
 *   able to go stale, and it duly kept printing the old answer.
 *
 * KEEP THIS IN STEP WITH usb.c. Last checked against 59577cc, where the function
 * reads:
 *
 *     if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
 *         uint8_t report_id = 0;
 *         if (iface->uses_report_id)
 *             report_id = report[0];
 *         if (report_id < MAX_REPORTS) {
 *             process_report_f receiver = iface->report_handler[report_id];
 *             if (receiver != NULL)
 *                 receiver((uint8_t *)report, len, device_idx, iface);
 *         }
 *     }
 *     else if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) process_keyboard_report(...);
 *     else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)    process_mouse_report(...);
 *
 * DeskHop Extended carries this verbatim too - `git diff 59577cc..HEAD -- src/usb.c`
 * is empty there.
 *
 * Note what this does NOT depend on: iface->protocol. Boot protocol changes what
 * the device puts on the wire, not which branch runs. That is the whole of the
 * boot-routing finding - see src/dispatchtest.c.
 */
#pragma once

#include "main.h"

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

/* Stand-in for a target whose usb.c still has the routing inlined in the callback,
   where there is no function to lift. It reproduces the upstream logic at 59577cc.
   A model can only ever report what it was written to say, so dispatchtest prints
   which of the two it used: a modelled result is not a measurement of the firmware.

   `report` must hold at least one byte, as it does on any real transfer. */
static inline process_report_f hid_route(const hid_interface_t *iface, uint8_t itf_protocol,
                                         const uint8_t *report) {
    if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = 0;

        if (iface->uses_report_id)
            report_id = report[0];

        if (report_id < MAX_REPORTS)
            return iface->report_handler[report_id];

        return NULL;
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
