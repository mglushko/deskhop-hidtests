/* One way to read an interface's handler table, whichever of three shapes the target
 * gives it. On main at 59577cc it is `process_report_f report_handler[MAX_REPORTS]`,
 * indexed by the report ID itself, so an ID of MAX_REPORTS or more is never bound and
 * its reports are dropped: the sculpt_rx_mouse finding. The fix keys the table by value
 * behind get_report_handler(), which the Makefile detects as HARNESS_HANDLER_LOOKUP;
 * upstream ce8abb6 instead resolves a 256-entry map of receiver ids per interface
 * through report_receivers[], HARNESS_HANDLER_MAP. Every read goes through hid_handler(),
 * so a change of shape lands here once and the same tests measure all three. */
#pragma once

#include "main.h"

#if defined(HARNESS_HANDLER_MAP)

static inline process_report_f hid_handler(const hid_interface_t *iface, unsigned report_id) {
    return report_id < REPORT_ID_MAP_SIZE ? report_receivers[iface->report_handler[report_id]] : NULL;
}

#elif defined(HARNESS_HANDLER_LOOKUP)

static inline process_report_f hid_handler(const hid_interface_t *iface, unsigned report_id) {
    return get_report_handler(iface, (uint8_t)report_id);
}

#else

static inline process_report_f hid_handler(const hid_interface_t *iface, unsigned report_id) {
    return report_id < MAX_REPORTS ? iface->report_handler[report_id] : NULL;
}

#endif
