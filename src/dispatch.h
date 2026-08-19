/* The routing half of usb.c:tuh_hid_report_received_cb, as a pure function.
 *
 * This is the one piece of firmware logic the harness reimplements rather than
 * lifts. tuh_hid_report_received_cb cannot go through tools/lift.py: it reaches
 * global_state and the TinyUSB host API, and computes a device_idx nothing here
 * needs. What it can do is answer the only question that matters - given an
 * interface and the bytes that arrived, which receiver gets called - and that is
 * a decision made from iface->uses_report_id, the interface protocol, and
 * report[0], all of which the harness already has.
 *
 * Shared rather than copied. mousetest.c used to carry its own version of this,
 * display-only and documented as able to go stale; src/dispatchtest.c now asserts
 * on it, so both read the same lines and a drift shows up in one place.
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
 * deskhop-extended carries this verbatim too - `git diff 59577cc..HEAD -- src/usb.c`
 * is empty there.
 *
 * Note what this does NOT depend on: iface->protocol. Boot protocol changes what
 * the device puts on the wire, not which branch runs. That is the whole of the
 * boot-routing finding - see src/dispatchtest.c.
 */
#pragma once

#include "main.h"

/* Returns the receiver usb.c would invoke, or NULL if the report is dropped.
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

/* Name of a receiver, for tables. */
static inline const char *hid_receiver_name(process_report_f f) {
    if (f == NULL)                     return "(dropped)";
    if (f == process_mouse_report)     return "mouse";
    if (f == process_keyboard_report)  return "keyboard";
    if (f == process_consumer_report)  return "consumer";
    if (f == process_system_report)    return "system";
    return "(unknown)";
}
