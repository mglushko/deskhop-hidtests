/* Consumer and system control decode cases. They drive process_consumer_report() and
 * process_system_report(), lifted verbatim out of the target's keyboard.c, and assert
 * what those functions hand to the send path: `compare` diffs the parse, so a change
 * confined to keyboard.c, which PR [#358] is, stays invisible to it.
 *
 * #358 adds no macro, so there is nothing to #ifdef on. Each case carries BOTH answers,
 * pre-#358 main's and #358's; src/cctest.c reports which one the tree under test matched,
 * failing only if it matches neither, as "behaves like pre-#358 main" or "behaves like
 * #358" on the verdict line.
 *
 * Every cc_array value below was read out of `make dump D=<device>`, not derived by
 * hand. dump printed that column inside the keyboard loop until this work, so the one
 * real separating device, an interface with no keyboard collection, never showed it.
 */
#pragma once

#include "descriptors.h"

typedef enum { CC_CONSUMER, CC_SYSTEM } cc_path_e;

typedef struct {
    const char *what;
    uint8_t     report[8];
    int         len;

    /* Expected on main before #358, and once it is applied. `sent` is whether anything
       reached the send path at all: process_system_report can drop a report
       outright, and before #358 it did. */
    bool    sent_main;
    uint8_t want_main[4];
    bool    sent_fixed;
    uint8_t want_fixed[4];
} cc_case_t;

/* cctest compares payload_len() bytes out of those arrays. A tree that widened either
   constant would turn every comparison into a read past the case rather than a compile
   error, so say it here. */
_Static_assert(CONSUMER_CONTROL_LENGTH <= 4 && SYSTEM_CONTROL_LENGTH <= 4,
               "want_main and want_fixed hold four bytes");

typedef struct {
    const char     *name;
    const uint8_t  *desc;
    int             desc_len;
    cc_path_e       path;
    uint8_t         expect_report_id; /* which report ID must be bound to the receiver */
    const cc_case_t *cases;
    unsigned        count;
} cc_device_t;

/* Cherry KC6000 Slim, [#117] "Media Keys not working". THE separating device, and a real
   one: consumer control with no report ID anywhere on the interface, so its reports carry
   no leading ID byte and main's unconditional skip reads one byte too far. Nine 1-bit
   usages then seven bits of padding, two bytes on the wire; cc_array from `make dump
   D=cherry_kc6000_consumer`:
     [0]=00CD play/pause  [1]=00B5 next  [2]=00B6 prev  [3]=00B8 eject
     [4]=00E2 mute  [5]=00EA vol-  [6]=00E9 vol+  [7]=0223 home  [8]=0192 calculator
   Bit i of byte 0 is usage i; bit 0 of byte 1 is usage 8. main reads byte 1 for byte 0,
   so it sees the padding byte for usages 0-7 and the first data byte for usage 8: the
   calculator row is the sharpest, main reporting a different key rather than none. main
   still sends on every row, process_consumer_report having no early return, so an
   undecoded press goes out as a zero payload; only the system receiver below can drop a
   report outright. */
static const cc_case_t kc6000_cases[] = {
    /*                                                     ---- main ----      ---- #358 ----   */
    {"play/pause (bit 0)",   {0x01, 0x00}, 2, true,  {0x00, 0x00},      true,  {0xCD, 0x00}},
    {"volume up (bit 6)",    {0x40, 0x00}, 2, true,  {0x00, 0x00},      true,  {0xE9, 0x00}},
    /* main answers Play/Pause to a Calculator press: it reads byte 1, finds bit 0
       set, and looks up cc_array[0] instead of cc_array[8]. */
    {"calculator (bit 8)",   {0x00, 0x01}, 2, true,  {0xCD, 0x00},      true,  {0x92, 0x01}},
    {"nothing held",         {0x00, 0x00}, 2, true,  {0},               true,  {0}},
};

/* Synthetic consumer block on report ID 3, variable path. Control: it uses report
   IDs, so #358's conditional skip and main's unconditional one agree, and every row
   here must be identical on both branches.
   cc_array: [0]=00B5 [1]=00B6 [2]=00B7 [3]=00CD [4]=00E2 [5]=00E9 [6]=00EA [7]=0223 */
static const cc_case_t consumer_rid_cases[] = {
    {"next track (bit 0)",   {0x03, 0x01}, 2, true, {0xB5, 0x00},       true,  {0xB5, 0x00}},
    {"volume up (bit 5)",    {0x03, 0x20}, 2, true, {0xE9, 0x00},       true,  {0xE9, 0x00}},
    {"AC home (bit 7)",      {0x03, 0x80}, 2, true, {0x23, 0x02},       true,  {0x23, 0x02}},
    {"nothing held",         {0x03, 0x00}, 2, true, {0},                true,  {0}},
};

/* Logi Bolt receiver consumer collection, report ID 3. is_variable is false here, so
   this is the *other* branch of process_consumer_report - the else that copies usage
   codes straight through rather than looking anything up in cc_array. Two 16-bit
   array slots, so a five-byte report. Control again: it uses report IDs. */
static const cc_case_t bolt_consumer_cases[] = {
    {"volume up",            {0x03, 0xE9, 0x00, 0x00, 0x00}, 5, true, {0xE9, 0x00, 0x00, 0x00},
                                                                 true, {0xE9, 0x00, 0x00, 0x00}},
    {"two usages at once",   {0x03, 0xE9, 0x00, 0xB5, 0x00}, 5, true, {0xE9, 0x00, 0xB5, 0x00},
                                                                 true, {0xE9, 0x00, 0xB5, 0x00}},
    {"nothing held",         {0x03, 0x00, 0x00, 0x00, 0x00}, 5, true, {0}, true, {0}},
};

/* Microsoft Wired Keyboard 600, [#297], system control on report ID 3. The system
   receiver ignores is_variable and takes one byte, so only the interface's report IDs
   matter: main requires length > 1 and reads raw_report[1]; #358 requires data_len >= 1,
   the same length, and reads data[0], the same byte. Identical on both branches, and the
   control for the synthetic below. */
static const cc_case_t ms600_system_cases[] = {
    {"power down",           {0x03, 0x81}, 2, true, {0x81},             true,  {0x81}},
    {"sleep",                {0x03, 0x82}, 2, true, {0x82},             true,  {0x82}},
    {"nothing held",         {0x03, 0x00}, 2, true, {0x00},             true,  {0x00}},
};

/* SYNTHETIC. The system half's separating case, and the only one that exists: the survey
   on d_system_no_report_id in descriptors.h finds no real device that reaches it. One
   byte on the wire, no report ID. main's guard is `length <= SYSTEM_CONTROL_LENGTH` with
   SYSTEM_CONTROL_LENGTH == 1, so a one-byte report is rejected unread and NOTHING is
   sent; #358 computes data_len == 1, passes `data_len < 1`, and delivers data[0]. The
   difference is the whole report, not a wrong byte. */
static const cc_case_t system_no_rid_cases[] = {
    {"power down",           {0x01}, 1, false, {0},                     true,  {0x01}},
    {"sleep",                {0x02}, 1, false, {0},                     true,  {0x02}},
    {"wake up",              {0x04}, 1, false, {0},                     true,  {0x04}},
    /* even an empty report differs: dropped before #358, delivered as zero with it */
    {"nothing held",         {0x00}, 1, false, {0},                     true,  {0x00}},
};


/* Microsoft Sculpt receiver, interface 2 (issue #367). Consumer control on report ID 7:
   one 16-bit array slot, then a keyboard-page array byte, padding and vendor bits, eight
   bytes on the wire; is_variable is false, so the copy-through branch, as on the Bolt.
   System control on report ID 3, one byte. Both declare their own IDs, so main and #358
   must agree on every row; the entries measure the receiver's third interface next to the
   mouse half that is not delivered at all. */
static const cc_case_t sculpt_consumer_cases[] = {
    {"volume up",            {0x07, 0xE9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, true, {0xE9, 0x00, 0x00, 0x00},
                                                                                  true, {0xE9, 0x00, 0x00, 0x00}},
    {"AC home",              {0x07, 0x23, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, true, {0x23, 0x02, 0x00, 0x00},
                                                                                  true, {0x23, 0x02, 0x00, 0x00}},
    {"nothing held",         {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, true, {0}, true, {0}},
};

static const cc_case_t sculpt_system_cases[] = {
    {"sleep",                {0x03, 0x82}, 2, true, {0x82},             true,  {0x82}},
    {"nothing held",         {0x03, 0x00}, 2, true, {0x00},             true,  {0x00}},
};

/* Apple A2520 media keys (issue #157): five variable bits on report 0x52, which is 82. A
   table indexed by the ID binds nothing for it, so these rows would fail on routing, not
   decode; they enter only on a tree binding an ID of 24 or more, keyed by value or
   through the 256-entry map, as the 8BitDo rows enter only on a tree that bounds the
   bitmap walk.
   cc_array from dump: [0]=00CD play/pause [1]=00B3 fast forward [2]=00B4 rewind
   [3]=00B5 scan next [4]=00B6 scan previous */
#if defined(HARNESS_HANDLER_LOOKUP) || defined(HARNESS_HANDLER_MAP)
static const cc_case_t apple_a2520_cc_cases[] = {
    {"play/pause (bit 0)",   {0x52, 0x01}, 2, true, {0xCD, 0x00},       true,  {0xCD, 0x00}},
    {"scan next (bit 3)",    {0x52, 0x08}, 2, true, {0xB5, 0x00},       true,  {0xB5, 0x00}},
    {"nothing held",         {0x52, 0x00}, 2, true, {0},                true,  {0}},
};
#endif

#define CCDEV(d, path, rid, c) \
    {#d, d_##d, (int)sizeof(d_##d), path, rid, c, (unsigned)ARRAY_SIZE(c)}

/* Exactly the reports sent by the BOOTSEL rig, including the release. */
static const cc_case_t sleepwake_emu_cases[] = {
    {"BOOTSEL hold: sleep", {0x03, 0x82}, 2, true, {0x82}, true, {0x82}},
    {"BOOTSEL tap: wake",   {0x03, 0x83}, 2, true, {0x83}, true, {0x83}},
    {"release",             {0x03, 0x00}, 2, true, {0x00}, true, {0x00}},
};

static const cc_device_t cc_devices[] = {
    CCDEV(sleepwake_emu,          CC_SYSTEM,   3, sleepwake_emu_cases),
    CCDEV(cherry_kc6000_consumer, CC_CONSUMER, 0, kc6000_cases),
    CCDEV(consumer,               CC_CONSUMER, 3, consumer_rid_cases),
    CCDEV(bolt_rx_consumer,       CC_CONSUMER, 3, bolt_consumer_cases),
    CCDEV(ms600_consumer,         CC_SYSTEM,   3, ms600_system_cases),
    CCDEV(system_no_report_id,    CC_SYSTEM,   0, system_no_rid_cases),
    CCDEV(sculpt_rx_consumer,     CC_CONSUMER, 7, sculpt_consumer_cases),
    CCDEV(sculpt_rx_consumer,     CC_SYSTEM,   3, sculpt_system_cases),
#if defined(HARNESS_HANDLER_LOOKUP) || defined(HARNESS_HANDLER_MAP)
    CCDEV(apple_a2520_iface1,     CC_CONSUMER, 0x52, apple_a2520_cc_cases),
#endif
};

#undef CCDEV
