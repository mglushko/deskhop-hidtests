/* Stand-ins for the TinyUSB and Pico SDK bits that hid_parser.c and hid_report.c
   reach for, so both can be compiled and run on the host.

   Every constant here is copied from the vendored
   pico-sdk/lib/tinyusb/src/class/hid/hid.h. `make check-constants` compares the
   two mechanically, so this no longer has to be taken on trust - run it if a
   result looks wrong, before suspecting the firmware. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TU_ATTR_PACKED __attribute__((packed))

/* ARRAY_SIZE deliberately not defined here: the target's constants.h provides it,
   and main.h includes that before anything needs it. */

static inline uint16_t tu_u16(uint8_t hi, uint8_t lo) {
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static inline uint32_t tu_u32(uint8_t b3, uint8_t b2, uint8_t b1, uint8_t b0) {
    return ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | b0;
}

/*==============================================================================
 *  Report item prefixes
 *============================================================================*/

enum { RI_TYPE_MAIN = 0, RI_TYPE_GLOBAL = 1, RI_TYPE_LOCAL = 2 };

enum {
    RI_MAIN_INPUT          = 8,
    RI_MAIN_OUTPUT         = 9,
    RI_MAIN_COLLECTION     = 10,
    RI_MAIN_FEATURE        = 11,
    RI_MAIN_COLLECTION_END = 12,
};

enum {
    RI_GLOBAL_USAGE_PAGE    = 0,
    RI_GLOBAL_LOGICAL_MIN   = 1,
    RI_GLOBAL_LOGICAL_MAX   = 2,
    RI_GLOBAL_PHYSICAL_MIN  = 3,
    RI_GLOBAL_PHYSICAL_MAX  = 4,
    RI_GLOBAL_UNIT_EXPONENT = 5,
    RI_GLOBAL_UNIT          = 6,
    RI_GLOBAL_REPORT_SIZE   = 7,
    RI_GLOBAL_REPORT_ID     = 8,
    RI_GLOBAL_REPORT_COUNT  = 9,
    RI_GLOBAL_PUSH          = 10,
    RI_GLOBAL_POP           = 11,
};

enum {
    RI_LOCAL_USAGE            = 0,
    RI_LOCAL_USAGE_MIN        = 1,
    RI_LOCAL_USAGE_MAX        = 2,
    RI_LOCAL_DESIGNATOR_INDEX = 3,
    RI_LOCAL_DESIGNATOR_MIN   = 4,
    RI_LOCAL_DESIGNATOR_MAX   = 5,
    RI_LOCAL_STRING_INDEX     = 7,
    RI_LOCAL_STRING_MIN       = 8,
    RI_LOCAL_STRING_MAX       = 9,
    RI_LOCAL_DELIMITER        = 10,
};

/*==============================================================================
 *  Usage pages and usages
 *============================================================================*/

enum {
    HID_USAGE_PAGE_DESKTOP  = 0x01,
    HID_USAGE_PAGE_KEYBOARD = 0x07,
    HID_USAGE_PAGE_BUTTON   = 0x09,
    HID_USAGE_PAGE_CONSUMER = 0x0c,
};

enum {
    HID_USAGE_DESKTOP_MOUSE          = 0x02,
    HID_USAGE_DESKTOP_KEYBOARD       = 0x06,
    HID_USAGE_DESKTOP_X              = 0x30,
    HID_USAGE_DESKTOP_Y              = 0x31,
    HID_USAGE_DESKTOP_WHEEL          = 0x38,
    HID_USAGE_DESKTOP_SYSTEM_CONTROL = 0x80,
};

enum {
    HID_USAGE_CONSUMER_CONTROL = 0x0001,
    HID_USAGE_CONSUMER_AC_PAN  = 0x0238,
};

enum { HID_PROTOCOL_BOOT = 0, HID_PROTOCOL_REPORT = 1 };

enum { HID_ITF_PROTOCOL_NONE = 0, HID_ITF_PROTOCOL_KEYBOARD = 1, HID_ITF_PROTOCOL_MOUSE = 2 };

/*==============================================================================
 *  Report shapes
 *============================================================================*/

typedef struct TU_ATTR_PACKED {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} hid_keyboard_report_t;

typedef struct TU_ATTR_PACKED {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
    int8_t  pan;
} hid_mouse_report_t;

/* The parts of device_t the lifted code actually reads. extract_report_values()
   wants mouse_buttons; process_consumer_report() and process_system_report() want
   the two fields CURRENT_BOARD_IS_ACTIVE_OUTPUT compares.

   Every width here is copied from the target's src/include/structs.h and each is
   load bearing. mouse_buttons is the one to be careful with:
   extract_report_values() falls back to state->mouse_buttons when the button field
   is skipped, so a wider field here would let a value survive that the firmware
   truncates - it was int32_t once, and silently disagreed with the device.

   Nothing checks this copy the way `make check-constants` checks the constants
   above; check_constants.py compares macros, not struct fields. Re-read structs.h
   when a decode result looks off. Last checked against main at 59577cc:
   mouse_buttons int16_t (structs.h:110), active_output and board_role uint8_t
   (structs.h:101-102). */
typedef struct {
    int16_t mouse_buttons;
    uint8_t active_output;
    uint8_t board_role;
} device_t;

/*==============================================================================
 *  Recording stand-ins for the send path
 *
 *  process_consumer_report() and process_system_report() end by handing a payload
 *  to one of three functions, depending on CURRENT_BOARD_IS_ACTIVE_OUTPUT. All
 *  three are recorded rather than implemented (src/recorders.c), because what a
 *  test wants to know is exactly what they were handed. See src/cctest.c.
 *============================================================================*/

typedef enum {
    SENT_NOTHING = 0,
    SENT_CONSUMER_LOCAL,  /* send_consumer_control(), board is the active output */
    SENT_SYSTEM_LOCAL,    /* send_system_control(),   board is the active output */
    SENT_QUEUED,          /* queue_packet(),          goes to the other board    */
} sent_via_e;

typedef struct {
    sent_via_e via;
    int        calls;      /* more than one send from a single report is itself a bug */
    uint8_t    payload[8];
    int        len;        /* bytes the sender was told to send, -1 if it does not say */
    int        packet_type;/* the enum packet_type_e queue_packet got, else -1 */
} sent_t;

extern sent_t harness_sent;

void harness_sent_reset(void);
