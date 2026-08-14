/* Stands in for deskhop's src/include/main.h, which pulls in the whole Pico SDK.
   hid_parser.h and hid_report.h both include "main.h", so this is the entry point
   for the whole harness.

   This directory is on the include path ahead of the target's src/include, so
   this file and tusb.h win, while hid_parser.h and hid_report.h below resolve to
   the target checkout's real, unmodified headers. Nothing is copied or patched,
   so the harness always sees exactly the structs of the branch under test. */
#pragma once

#include "harness.h"

/* the real headers, copied verbatim from the target checkout into the build dir.
   packet.h carries KBD_REPORT_LENGTH, KEYS_IN_USB_REPORT and MODIFIER_BIT_LENGTH,
   which hid_report.c reaches for. */
#include "constants.h"
#include "packet.h"

#include "hid_parser.h"
#include "hid_report.h"

/*==============================================================================
 *  Firmware functions that live outside the two files under test.
 *  Defined in src/stubs.c so every test links.
 *============================================================================*/

void process_mouse_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);
void process_keyboard_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);
void process_consumer_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);
void process_system_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);

keyboard_t *get_keyboard(hid_interface_t *iface, uint8_t report_id);

/* how many times each receiver was handed a report, for tests that care */
extern int stub_mouse_reports, stub_keyboard_reports;
extern int stub_consumer_reports, stub_system_reports;

/*==============================================================================
 *  Defined in the two files under test
 *============================================================================*/

void parse_report_descriptor(hid_interface_t *iface, uint8_t const *report, int desc_len);
void extract_data(hid_interface_t *iface, report_val_t *val);
int32_t get_report_value(uint8_t *report, int len, report_val_t *val);

/* Also in hid_report.c, though the target declares it over in keyboard.h, which
   is not copyable here - it pulls in structs.h and the layout remapping macros.
   Declared rather than copied, and safe to: this signature is the same on main
   and on the multi-block branch. extract_bit_variable's is not, which is why
   tests go through this and never call that directly. */
int32_t extract_kbd_data(uint8_t *raw_report, int len, uint8_t itf, hid_interface_t *iface,
                         hid_keyboard_report_t *report);

/* lifted verbatim out of the target's mouse.c by tools/lift_mouse.py */
void extract_report_values(uint8_t *raw_report, int len, device_t *state,
                           mouse_values_t *values, hid_interface_t *iface);
