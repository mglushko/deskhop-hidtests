/* Consumer and system control decode cases.
 *
 * These drive process_consumer_report() and process_system_report(), lifted
 * verbatim out of the target's keyboard.c, and assert what those functions hand to
 * the send path. That is the gap the README used to describe: `compare` diffs the
 * parse, so a change confined to keyboard.c is invisible to it, and PR [#358] is
 * exactly such a change.
 *
 * #358 adds no macro, so unlike cases_kbd.h there is nothing to #ifdef on. Each
 * case therefore carries BOTH answers - what main produces and what #358 produces -
 * and src/cctest.c reports which one the branch under test matched, failing only if
 * it matches neither. The verdict line at the end of a run says "behaves like main"
 * or "behaves like #358", which is the question compare answers wrongly.
 *
 * Every cc_array value below was read out of `make dump D=<device>`, not derived by
 * hand. That column of dump's output only became visible as part of this work - it
 * used to print inside the keyboard loop, so an interface with no keyboard
 * collection never showed it, which is precisely the case for the one real device
 * that separates the two branches.
 */
#pragma once

#include "descriptors.h"

typedef enum { CC_CONSUMER, CC_SYSTEM } cc_path_e;

typedef struct {
    const char *what;
    uint8_t     report[8];
    int         len;

    /* Expected on main, and once #358 is applied. `sent` is whether anything
       reached the send path at all: process_system_report can drop a report
       outright, and on main it does. */
    bool    sent_main;
    uint8_t want_main[4];
    bool    sent_fixed;
    uint8_t want_fixed[4];
} cc_case_t;

typedef struct {
    const char     *name;
    const uint8_t  *desc;
    int             desc_len;
    cc_path_e       path;
    uint8_t         expect_report_id; /* which report ID must be bound to the receiver */
    const cc_case_t *cases;
    unsigned        count;
} cc_device_t;

/* Cherry KC6000 Slim, [#117] "Media Keys not working". THE separating device, and a
   real one: consumer control with no report ID anywhere on the interface, so its
   reports carry no leading ID byte and main's unconditional skip reads one byte too
   far. Nine 1-bit usages then seven bits of padding, two bytes on the wire.

   cc_array, from `make dump D=cherry_kc6000_consumer`:
     [0]=00CD play/pause  [1]=00B5 next  [2]=00B6 prev  [3]=00B8 eject
     [4]=00E2 mute  [5]=00EA vol-  [6]=00E9 vol+  [7]=0223 home  [8]=0192 calculator

   Bit i of byte 0 is usage i; bit 0 of byte 1 is usage 8. main reads byte 1 where it
   should read byte 0, so it sees the padding byte for usages 0-7 and the *first*
   data byte for usage 8 - which is why the calculator row below is the sharpest of
   the four: main does not merely lose the key, it reports a different one.

   Note that main still *sends* on every row: process_consumer_report has no early
   return, so a press it fails to decode goes out as a zero payload rather than as
   nothing. Losing the key and sending silence are the same thing downstream, but the
   distinction matters when reading the table - only the system receiver below can
   drop a report outright. */
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
   receiver ignores is_variable entirely - it just takes one byte - so what matters
   here is only that the interface uses report IDs. main requires length > 1 and
   reads raw_report[1]; #358 requires data_len >= 1, which is the same length, and
   reads data[0], which is the same byte. Identical on both branches, and the control
   for the synthetic below. */
static const cc_case_t ms600_system_cases[] = {
    {"power down",           {0x03, 0x81}, 2, true, {0x81},             true,  {0x81}},
    {"sleep",                {0x03, 0x82}, 2, true, {0x82},             true,  {0x82}},
    {"nothing held",         {0x03, 0x00}, 2, true, {0x00},             true,  {0x00}},
};

/* SYNTHETIC. The system half's separating case, and the only one that exists - see
   the comment on d_system_no_report_id in descriptors.h for the survey that
   establishes no real device reaches it.

   One byte on the wire, no report ID. main's guard is `length <= SYSTEM_CONTROL_LENGTH`
   with SYSTEM_CONTROL_LENGTH == 1, so a one-byte report is rejected before anything is
   read and NOTHING is sent. #358 computes data_len == 1, passes `data_len < 1`, and
   delivers data[0]. The difference is not a wrong byte, it is the whole report. */
static const cc_case_t system_no_rid_cases[] = {
    {"power down",           {0x01}, 1, false, {0},                     true,  {0x01}},
    {"sleep",                {0x02}, 1, false, {0},                     true,  {0x02}},
    {"wake up",              {0x04}, 1, false, {0},                     true,  {0x04}},
    /* even an empty report differs: dropped on main, delivered as zero on #358 */
    {"nothing held",         {0x00}, 1, false, {0},                     true,  {0x00}},
};


/* Microsoft Sculpt receiver, interface 2 (issue #367). Consumer control on report ID
   7 as one 16-bit array slot followed by a keyboard-page array byte, padding and
   vendor bits - eight bytes on the wire. is_variable is false, so this is the copy
   through branch, as on the Bolt. System control on report ID 3, one byte. Both
   declare their own IDs, so main and #358 must agree on every row; the entries are
   here so the receiver's third interface is measured next to the mouse half that
   is not delivered at all. */
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

/* Apple A2520 media keys (issue #157): five variable bits on report 0x52, which is 82. On a
   table indexed by the ID nothing is bound for it, so the rows below would fail on routing
   rather than decode; they enter only on a tree that looks the ID up by value, the way the
   8BitDo rows enter only on a tree that bounds the bitmap walk.
   cc_array from dump: [0]=00CD play/pause [1]=00B3 fast forward [2]=00B4 rewind
   [3]=00B5 scan next [4]=00B6 scan previous */
#ifdef HARNESS_HANDLER_LOOKUP
static const cc_case_t apple_a2520_cc_cases[] = {
    {"play/pause (bit 0)",   {0x52, 0x01}, 2, true, {0xCD, 0x00},       true,  {0xCD, 0x00}},
    {"scan next (bit 3)",    {0x52, 0x08}, 2, true, {0xB5, 0x00},       true,  {0xB5, 0x00}},
    {"nothing held",         {0x52, 0x00}, 2, true, {0},                true,  {0}},
};
#endif

#define CCDEV(d, path, rid, c) \
    {#d, d_##d, (int)sizeof(d_##d), path, rid, c, (unsigned)ARRAY_SIZE(c)}

static const cc_device_t cc_devices[] = {
    CCDEV(cherry_kc6000_consumer, CC_CONSUMER, 0, kc6000_cases),
    CCDEV(consumer,               CC_CONSUMER, 3, consumer_rid_cases),
    CCDEV(bolt_rx_consumer,       CC_CONSUMER, 3, bolt_consumer_cases),
    CCDEV(ms600_consumer,         CC_SYSTEM,   3, ms600_system_cases),
    CCDEV(system_no_report_id,    CC_SYSTEM,   0, system_no_rid_cases),
    CCDEV(sculpt_rx_consumer,     CC_CONSUMER, 7, sculpt_consumer_cases),
    CCDEV(sculpt_rx_consumer,     CC_SYSTEM,   3, sculpt_system_cases),
#ifdef HARNESS_HANDLER_LOOKUP
    CCDEV(apple_a2520_iface1,     CC_CONSUMER, 0x52, apple_a2520_cc_cases),
#endif
};

#undef CCDEV
