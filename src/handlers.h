/* One way to read an interface's handler table, whichever shape the target gives it.
 *
 * On main at 59577cc the table is `process_report_f report_handler[MAX_REPORTS]`, indexed
 * by the report ID itself, so an ID of MAX_REPORTS or more is never bound and every report
 * carrying one is dropped: the finding behind sculpt_rx_mouse. The fix keys the table by
 * value and reads it through get_report_handler(), which the Makefile detects and names
 * HARNESS_HANDLER_LOOKUP. Every place the harness reads the table goes through
 * hid_handler(), so a change of shape lands here once rather than in five files, and the
 * same tests measure both. */
#pragma once

#include "main.h"

#ifdef HARNESS_HANDLER_LOOKUP

static inline process_report_f hid_handler(const hid_interface_t *iface, unsigned report_id) {
    return get_report_handler(iface, (uint8_t)report_id);
}

#else

static inline process_report_f hid_handler(const hid_interface_t *iface, unsigned report_id) {
    return report_id < MAX_REPORTS ? iface->report_handler[report_id] : NULL;
}

#endif
