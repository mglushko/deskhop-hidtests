/* Which receiver does usb.c hand a report to? Answered by the firmware's own
 * tuh_hid_report_received_cb, lifted whole out of the target's usb.c by tools/lift.py and
 * driven by src/routing.c, which supplies the two TinyUSB host calls and the global it
 * reaches. The receivers it calls are the recording stubs in src/stubs.c, so what comes
 * back is the address of the one it chose, or NULL when it chose none.
 *
 * This file used to hold a model of that routing, kept in step with usb.c by hand, with
 * the real function lifted only on a tree that had factored the decision out. A model
 * reports what it was written to say, and the boot-protocol misrouting
 * (hrvach/deskhop#363) lived on inside one; nothing here is modelled any more.
 */
#pragma once

#include "main.h"
#include "handlers.h"

/* `report` must hold at least one byte, as it does on any real transfer. `len` is what
   the receiver is told, and the stubs do not read it. */
process_report_f hid_route(const hid_interface_t *iface, uint8_t itf_protocol,
                           const uint8_t *report, int len);

/* Name of a receiver, for tables. */
static inline const char *hid_receiver_name(process_report_f f) {
    if (f == NULL)                     return "(dropped)";
    if (f == process_mouse_report)     return "mouse";
    if (f == process_keyboard_report)  return "keyboard";
    if (f == process_consumer_report)  return "consumer";
    if (f == process_system_report)    return "system";
    return "(unknown)";
}
