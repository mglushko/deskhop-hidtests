/* Stands in for deskhop's src/include/main.h, which pulls in the whole Pico SDK; since
   hid_parser.h and hid_report.h both include "main.h", this is the harness's entry point.
   The target's src/include is deliberately NOT on the include path: a quoted #include
   searches the including file's own directory first and would find the real main.h.
   Instead the Makefile copies the six headers it needs (COPY_HDRS) verbatim into the
   build dir, which IS on the path, so their quoted includes land back on these shims.
   Never patched, so the harness sees exactly the structs of the branch under test. */
#pragma once

#include "harness.h"

/* the real headers, copied verbatim from the target checkout into the build dir.
   packet.h carries KBD_REPORT_LENGTH, KEYS_IN_USB_REPORT and MODIFIER_BIT_LENGTH,
   which hid_report.c reaches for. usb_descriptors.h carries REPORT_ID_NONE, which the
   lifted report callback names since upstream 4d113ac; it is macros only, and the
   descriptor macros in it are never expanded here. */
#include "constants.h"
#include "packet.h"
#include "usb_descriptors.h"

/* hid_report.c fills harness.h's hand-copied hid_keyboard_report_t by these two
   constants (memcpy and memset of KBD_REPORT_LENGTH bytes, keycode writes bounded by
   KEYS_IN_USB_REPORT), so a target that changed either would overrun the copy on the
   host: asserted here, where both sides are first in view. The mouse struct has no such
   pair: it is the boot protocol's five bytes and the packet constants are the UART's. */
_Static_assert(sizeof(hid_keyboard_report_t) == KBD_REPORT_LENGTH,
               "hid_keyboard_report_t in harness.h must be KBD_REPORT_LENGTH bytes");
_Static_assert(KEYS_IN_USB_REPORT == sizeof(((hid_keyboard_report_t *)0)->keycode),
               "hid_keyboard_report_t.keycode in harness.h must hold KEYS_IN_USB_REPORT keys");

#include "hid_parser.h"
#include "hid_report.h"

/* The parts of device_t the lifted code reads. extract_report_values() names
   mouse_buttons; CURRENT_BOARD_IS_ACTIVE_OUTPUT in process_consumer_report() and
   process_system_report() compares active_output and board_role; and
   tuh_hid_report_received_cb() finds its interface in iface[][] and tells a primary
   keyboard by kbd_dev_addr and kbd_instance. Defined here rather than in harness.h
   because iface[][] needs hid_interface_t, which the include above brings into view.

   Every width is copied from the target's src/include/structs.h and is load bearing:
   where a skipped button field falls back to mouse_buttons, a wider field here would let
   a value survive that the firmware truncates (it was int32_t once, and silently disagreed
   with the device). A tree that keeps buttons per interface reads iface->mouse_buttons
   instead, but the parameter stays, so keep the width right either way.

   Nothing checks this copy: check_constants.py compares macros, not struct fields, so
   re-read structs.h when a decode result looks off. Last checked against upstream
   e5f8ae8: kbd_dev_addr and kbd_instance uint8_t (structs.h:94-95), active_output and
   board_role uint8_t (101-102), mouse_buttons int16_t (110), iface as here (118);
   DeskHop Extended 60605e1 has the same widths (structs.h:115-116, 122-123, 131, 151). */
typedef struct {
    hid_interface_t iface[MAX_DEVICES][MAX_INTERFACES];
    int16_t mouse_buttons;
    uint8_t active_output;
    uint8_t board_role;
    uint8_t kbd_dev_addr;
    uint8_t kbd_instance;
} device_t;

/*==============================================================================
 *  Firmware functions that live outside the two files under test.
 *  Defined in src/stubs.c so every test links.
 *============================================================================*/

void process_mouse_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);
void process_keyboard_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);
void process_consumer_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);
void process_system_report(uint8_t *report, int len, uint8_t itf, hid_interface_t *iface);

keyboard_t *get_keyboard(hid_interface_t *iface, uint8_t report_id);
keyboard_t *get_or_add_keyboard(hid_interface_t *iface, uint8_t report_id);

/*==============================================================================
 *  Defined in the two files under test
 *============================================================================*/

void parse_report_descriptor(hid_interface_t *iface, uint8_t const *report, int desc_len);
void extract_data(hid_interface_t *iface, report_val_t *val);

/* Only on a target whose handler table is keyed by value, where the target declares it
   in mouse.h, which is not copied here. The Makefile sets the macro when it finds the
   function in hid_report.c; src/handlers.h reads the table the same way on both shapes. */
#ifdef HARNESS_HANDLER_LOOKUP
process_report_f get_report_handler(const hid_interface_t *iface, uint8_t report_id);
#endif
int32_t get_report_value(uint8_t *report, int len, report_val_t *val);

/* Also in hid_report.c, though the target declares it in keyboard.h, which is not
   copyable here: it pulls in structs.h and the layout remapping macros. Declared rather
   than copied, and safe to: this signature is the same on main and on the multi-block
   branch; extract_bit_variable's is not, so tests go through this, never that
   directly. */
int32_t extract_kbd_data(uint8_t *raw_report, int len, uint8_t itf, hid_interface_t *iface,
                         hid_keyboard_report_t *report);

/* lifted verbatim out of the target's mouse.c by tools/lift.py */
void extract_report_values(uint8_t *raw_report, int len, device_t *state,
                           mouse_values_t *values, hid_interface_t *iface);

/*==============================================================================
 *  usb.c's report callback, lifted whole for mousetest and dispatchtest
 *  tuh_hid_report_received_cb() reaches two TinyUSB host calls and global_state, all
 *  supplied by src/routing.c, and ends by calling one of the four receivers above, which
 *  src/stubs.c records in harness_reached.
 *============================================================================*/

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report,
                                uint16_t len);
uint8_t tuh_hid_interface_protocol(uint8_t dev_addr, uint8_t idx);
bool tuh_hid_receive_report(uint8_t dev_addr, uint8_t idx);

extern process_report_f harness_reached;

/*==============================================================================
 *  The consumer and system send path
 *  process_consumer_report and process_system_report are lifted out of the target's
 *  keyboard.c for the cctest target only, replacing the empty stubs above (src/stubs.c
 *  guards those two on HARNESS_LIFT_CC). Everything they reach below the receiver level
 *  is recorded rather than implemented, in src/recorders.c.
 *============================================================================*/

extern device_t global_state;

void send_consumer_control(uint8_t *raw_report, device_t *state);
void send_system_control(uint8_t *raw_report, device_t *state);
void queue_packet(const uint8_t *data, enum packet_type_e packet_type, int length);
