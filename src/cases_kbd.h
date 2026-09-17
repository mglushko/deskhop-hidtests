/* Keyboard decode cases, shared by kbdtest and shortreport so the two cannot disagree
 * about what a device sends; shortreport replays each row at every truncated length.
 * Each case carries up to four expectations, one per tree: `keys` is main before #359,
 * `keys_fixed` a parser keeping every NKRO block, `keys_multi` one holding one
 * keyboard_t per collection, `keys_wide` one keeping a usage range wider than its block.
 * The Makefile picks the column from what it finds in the target, so one unmodified file
 * asserts correctly against every tree.
 */
#pragma once

#include "descriptors.h"
#include "kept_out.h"

/* Most cases have no third or fourth answer, so they stop at keys_fixed and leave the
   later columns off the end. That is the normal shape here, not an oversight: an omitted
   has_multi or has_wide is false, which is exactly "this row's answer does not move
   again". Scoped off for this table only, so the warning keeps working everywhere else. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#ifdef MAX_NKRO_BLOCKS
#define KEEPS_EVERY_BLOCK 1
#else
#define KEEPS_EVERY_BLOCK 0
#endif

/* Set by the Makefile when the target has get_or_add_keyboard, meaning an interface can
   hold one keyboard_t per collection instead of collapsing them onto the first. */
#ifdef HARNESS_MULTI_KEYBOARD
#define ONE_KEYBOARD_PER_COLLECTION 1
#else
#define ONE_KEYBOARD_PER_COLLECTION 0
#endif

/* Set by the Makefile when the target records an NKRO block whose usage range is wider
   than the block has bits for, instead of requiring one usage per bit exactly. */
#ifdef HARNESS_WIDE_USAGE_RANGE
#define ACCEPTS_WIDE_USAGE_RANGE 1
#else
#define ACCEPTS_WIDE_USAGE_RANGE 0
#endif

#define REPORT_MAX 40

/* The shortest report the firmware hands to the decoder: process_keyboard_report returns
   on length < KBD_REPORT_LENGTH before extract_kbd_data runs, so kbdtest refuses a row
   below this and shortreport starts its truncations here rather than assert an input the
   receiver drops. A hand copy of firmware logic: re-check against keyboard.c when
   touching either; last checked against upstream e5f8ae8 and DeskHop Extended 60605e1. */
#define KBD_MIN_LEN KBD_REPORT_LENGTH

typedef struct {
    const char *what;
    uint8_t     report[REPORT_MAX];
    int         len;
    uint8_t     modifier;
    uint8_t     keys[KEYS_IN_USB_REPORT];       /* expected on main before #359 */
    uint8_t     keys_fixed[KEYS_IN_USB_REPORT]; /* expected once every NKRO block is kept */

    /* A third state, for the devices whose answer moves again once an interface can
       hold more than one keyboard_t. MAX_NKRO_BLOCKS stopped separating the trees on
       its own: [#359] defines it and so does everything built on top, including trees
       without the multi-keyboard fix. Opt in per case, because an omitted array would
       otherwise read as "expects no keys" rather than "no third answer". */
    bool        has_multi;
    uint8_t     keys_multi[KEYS_IN_USB_REPORT];

    /* A fourth, for bitmaps kept only once a usage range wider than its block is
       accepted. A row may carry either flag, both or neither; the Keychron's NKRO
       collection parsed alone needs only this one, having nothing for a second keyboard_t
       to hold. Every width arm so far sits on a tree with one keyboard_t per collection,
       which the columns model; kbdtest's banner says what a tree off that lineage would
       do. */
    bool        has_wide;
    uint8_t     keys_wide[KEYS_IN_USB_REPORT];
} kbd_case_t;

typedef struct {
    const char    *name;
    const uint8_t *desc;
    int            desc_len;
    uint8_t        protocol;
    const kbd_case_t  *cases;
    unsigned       count;
} kbd_device_t;

/* Plain boot layout: [modifier][reserved][6 keycodes], no report ID, and
   is_nkro clear, so extract_kbd_data takes the len == 8 shortcut. */
static const kbd_case_t k_boot_cases[] = {
    {"a",                  {0x00, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"shift + a",          {0x02, 0x00, 0x04}, 8,                   0x02, {4}, {4}},
    {"six keys at once",   {0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"every modifier",     {0xFF, 0x00, 0x00}, 8,                   0xFF, {0}, {0}},
    {"nothing held",       {0x00, 0x00, 0x00}, 8,                   0x00, {0}, {0}},
};

/* Same path, but the second case is nine bytes: a keyboard that ships a report
   ID byte on an interface whose descriptor never declared one. _extract_kbd_boot
   drops the leading byte and reads the eight that follow. */
static const kbd_case_t k_rpi_cases[] = {
    {"a",                  {0x00, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"ctrl + alt + delete",{0x05, 0x00, 0x4C}, 8,                   0x05, {0x4C}, {0x4C}},
    {"stray leading id byte", {0x01, 0x02, 0x00, 0x04}, 9,          0x02, {4}, {4}},
};

/* HID_PROTOCOL_BOOT returns before the descriptor is consulted at all, so the
   bytes are taken at face value whatever d_boot_keyboard happens to declare. */
static const kbd_case_t k_boot_protocol_cases[] = {
    {"shift + a, boot protocol", {0x02, 0x00, 0x04}, 8,             0x02, {4}, {4}},
};

/* Keyboard on report ID 1 of a three-collection dongle. uses_report_id is set,
   so the boot shortcut is skipped and _extract_kbd_other runs: it drops the ID
   byte, takes the modifier from modifier.offset_idx, and picks keycodes out of
   the byte positions key_array marked - here bytes 1 through 6. */
static const kbd_case_t k_composite_cases[] = {
    {"a",                  {0x01, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"ctrl + alt + delete",{0x01, 0x05, 0x4C}, 8,                   0x05, {0x4C}, {0x4C}},
    {"six keys at once",   {0x01, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
};

/* One 240-bit block on report ID 1: [id][modifier][30 bytes of bitmap]. The
   block starts at bit 8 and usage_min is 0, so usage u is simply bit u of the
   bitmap, counting from bit 0 of byte 2. */
static const kbd_case_t k_nkro_cases[] = {
    {"usage 4 (a)",        {0x01, 0x00, 0x10}, 32,                  0x00, {4}, {4}},
    {"shift + a + b",      {0x01, 0x02, 0x30}, 32,                  0x02, {4, 5}, {4, 5}},
    /* nine keys down, but a boot report only carries six */
    {"more keys than fit", {0x01, 0x00, 0xF0, 0x1F}, 32,            0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"highest usage, 239", {[31] = 0x80, [0] = 0x01}, 32,           0x00, {239}, {239}},
};

/* Keyboardio Model 100. The bitmap starts at bit 68 - byte 8, four bits in -
   which [#216] blames for shifted keys. It decodes correctly: extract_bit_variable
   seeds its bit cursor with offset & 7 (four) and its usage cursor with usage_min
   (also four), so usage u lands on absolute bit 64 + u, which is where the
   descriptor puts it. The two happening to be equal is what saves it. */
static const kbd_case_t k_keyboardio_cases[] = {
    {"usage 4 (a)",        {[8] = 0x10}, 36,                        0x00, {4}, {4}},
    {"usage 5 and 6",      {[8] = 0x60}, 36,                        0x00, {5, 6}, {5, 6}},
    {"usage 12, next byte",{[9] = 0x10}, 36,                        0x00, {12}, {12}},
    {"shift + a",          {[0] = 0x02, [8] = 0x10}, 36,            0x02, {4}, {4}},
};

/* Logitech G Pro Superlight 2 receiver, [#215]. Three blocks; main before #359 kept
   only the first, because only that one cleared the size > 32 filter. Usages 4 to 115
   work either way. Usage 135 is the first of the Japanese and Korean IME keys
   that live in the second block, and it is the case that separates the two. */
static const kbd_case_t k_superlight2_cases[] = {
    {"usage 4 (a)",        {0x01, 0x00, 0x01}, 17,                  0x00, {4}, {4}},
    {"usage 115, end of block 0", {[0] = 0x01, [15] = 0x80}, 17,    0x00, {115}, {115}},
    {"usage 135, IME key in block 1", {[0] = 0x01, [16] = 0x01}, 17,
                                                                    0x00, {0}, {135}},
};

/* Wooting Two HE, [#335], "only CTRL, Shift & Win work". Four blocks, and main before
   #359 keeps the last, usages 176 to 221, none of them letters: the modifier byte
   survives and every ordinary key decodes to nothing, exactly the reported symptom. The
   first row is the bug in one line, 'a' held down and nothing out. */
static const kbd_case_t k_wooting_cases[] = {
    {"usage 4 (a), block 0",   {[1] = 0x01}, 28,                    0x00, {0}, {4}},
    {"usage 51, block 1",      {[7] = 0x01}, 28,                    0x00, {0}, {51}},
    {"usage 176, block 3",     {[22] = 0x01}, 28,                   0x00, {176}, {176}},
    {"shift held, no key",     {[0] = 0x02}, 28,                    0x02, {0}, {0}},
    {"shift + a",              {[0] = 0x02, [1] = 0x01}, 28,        0x02, {0}, {4}},
};

/* Logi Bolt receiver, interface 0. No report ID anywhere in the descriptor, and a
   plain boot layout - modifier, an explicit reserved byte, six key slots - so
   extract_kbd_data takes the len == 8 shortcut and never consults key_array. */
static const kbd_case_t k_bolt_rx_cases[] = {
    {"a",                  {0x00, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"shift + a",          {0x02, 0x00, 0x04}, 8,                   0x02, {4}, {4}},
    {"six keys at once",   {0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"every modifier",     {0xFF, 0x00, 0x00}, 8,                   0xFF, {0}, {0}},
};

/* Keychron Ultra-Link 8K, the report ID 7 keyboard collection on its own. Report
   ID is declared, so the boot shortcut is skipped and _extract_kbd_other picks the
   six keycodes out of the byte positions key_array marks - here bytes 1 to 6.
   This is the control for the next block: alone, the keyboard decodes correctly. */
static const kbd_case_t k_ultralink_kbd_cases[] = {
    {"a",                  {0x07, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"shift + a",          {0x07, 0x02, 0x04}, 8,                   0x02, {4}, {4}},
    {"six keys at once",   {0x07, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
};

/* The same reports against the whole of interface 1, which is what deskhop parses: two
   keyboard collections, on report IDs 7 and 0x11. `keys` and `keys_fixed` hold WRONG
   answers on purpose, what a tree without the multi-keyboard fix produces: get_keyboard()
   short-circuited on num_keyboards == 1 to keyboards[PRIMARY_KEYBOARD], so the 0x11
   collection wrote over the ID 7 one. Its NKRO block is VARIABLE at offset_idx 1, and
   handle_keyboard_descriptor_values sets key_array[offset_idx] = (data_type == ARRAY)
   unconditionally, clearing the key_array[1] the ID 7 collection had set: byte 1 is its
   first key slot, so every 6KRO report lost its first keycode. keys_multi, what a tree
   with get_or_add_keyboard produces, is the right answer and matches
   k_ultralink_kbd_cases above, the same collection parsed alone.

   The three report 0x11 rows are the NKRO collection, and a keycode comes out of them on
   the width arm alone: without it the bitmap is rejected for declaring 153 usages over
   152 bits, in its own keyboard_t or the collapsed one; with it the block decodes from
   whichever holds it. So only keys_wide carries a keycode, and has_multi stays false on
   the first two rows since the collapse fix does not move them; the Enter row is the
   exception, the collapse alone turning its bit into ErrorRollOver and the collapse fix
   taking that away again. */
static const kbd_case_t k_ultralink_iface1_cases[] = {
    {"a",                       {0x07, 0x00, 0x04}, 8,              0x00, {0}, {0},
                                                                    true, {4}},
    {"shift + a",               {0x07, 0x02, 0x04}, 8,              0x02, {0}, {0},
                                                                    true, {4}},
    {"six keys at once",        {0x07, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {5, 6, 7, 8, 9}, {5, 6, 7, 8, 9},
                                                                    true, {4, 5, 6, 7, 8, 9}},
    {"usage 4 (a), NKRO collection", {0x11, 0x00, 0x10}, 21,        0x00, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"rig: ,./ on the NKRO collection", {0x11, 0x00, [8] = 0xC0, [9] = 0x01}, 21,
                                                                    0x00, {0}, {0},
                                                                    false, {0}, true, {54, 55, 56}},
    /* Enter is the one rig report whose bit lands inside the six bytes the collapsed
       key_array covers, so a collapsing tree returns ErrorRollOver rather than nothing,
       and in the fifth slot: the collapse clears key_array[1], so _extract_kbd_other
       packs from byte 2 and byte 6, where this bit is, arrives fifth. The 8BitDo's boot
       rows record the same shape by another route. */
    {"rig: enter on the NKRO collection", {0x11, 0x00, [7] = 0x01}, 21,
                                                                    0x00, {0, 0, 0, 0, 1}, {0, 0, 0, 0, 1},
                                                                    true, {0}, true, {40}},
};

/* Keychron Ultra-Link 8K, upstream issue 324, the NKRO collection on report ID 0x11
   alone, so the off-by-one range is read without the key_array interaction on top. The
   bitmap is declared 19 00 2A 98 00 with 95 98: usage minimum 0, maximum 152, over 152
   bits, 153 usages in 152 bits, and it is what the device ships. Three columns are wrong
   on purpose: trees before the width arm throw the block away, pre-#359 main in
   _extract_kbd_nkro's 1:1 recheck and #359 trees in maps_usage_to_bitmap_bits. Then
   nkro_count is 0 at decode time, the report falls to _extract_kbd_other, and with both
   items VARIABLE key_array is empty, so every keycode disappears while the modifier
   decodes: hence the second row's modifier 0x02 and no keys. Upstream keeps the block
   since e5f8ae8 and the fork since 524b61d; keys_wide is what such a tree, accepting a
   range that merely covers its block, produces. The last row is the off-by-one itself:
   usage 151 is the highest bit with a home, and usage 152, declared but with no room, is
   not expected back. */
static const kbd_case_t k_ultralink_nkro_cases[] = {
    {"usage 4 (a)",              {0x11, 0x00, 0x10}, 21,            0x00, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"shift + a",                {0x11, 0x02, 0x10}, 21,            0x02, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"highest usage, 151",       {0x11, 0x00, [20] = 0x80}, 21,     0x00, {0}, {0},
                                                                    false, {0}, true, {151}},
};

/* A 6KRO keyboard carrying a short keyboard-page bit field, [id][mod][F13-F20][6 keys].
   Both columns hold the same correct values: main before #359 ignores the 8-bit field by
   filtering on size > 32, and a parser deciding is_nkro on the summed block width ignores
   it too. Only a parser flagging NKRO per block fails these: it routes the report through
   _extract_kbd_nkro, which never reads key_array, so every keycode disappears while the
   modifiers work. The F13 row is reported on no branch, the field never being treated as
   a key bitmap: the cost of the size rule, stated rather than hidden. */
static const kbd_case_t k_bit_field_cases[] = {
    {"a",                       {0x01, 0x00, 0x00, 0x04}, 9,        0x00, {4}, {4}},
    {"shift + a",               {0x01, 0x02, 0x00, 0x04}, 9,        0x02, {4}, {4}},
    {"six keys at once",        {0x01, 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 9,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"F13 held, key array still read", {0x01, 0x00, 0x01, 0x04}, 9, 0x00, {4}, {4}},
    {"nothing held",            {0x01, 0x00, 0x00, 0x00}, 9,        0x00, {0}, {0}},
};

/* 8BitDo Retro Mechanical Keyboard, [#57], interface 2: a 6KRO keyboard on report ID 1,
   then NKRO bitmaps on 12 and 10. Worse than the Keychron above: both NKRO blocks map
   one usage per bit over 120 bits and pass every test the parser applies, so on a tree
   where they land on keyboards[0] beside the 6KRO key array, is_nkro is set there and
   _extract_kbd_nkro runs for report ID 1 too, decoding a 6KRO report as bitmap bits. The
   keyboard types the wrong keys rather than losing one. `keys` and `keys_fixed` are
   therefore wrong on purpose and keys_multi is the right answer. The doubling in
   keys_fixed is not a typo: both NKRO blocks land on keyboards[0] and are walked over
   the same bytes, so each finds the same bit, on the report ID 12 rows too. `keys`
   mirrors `keys_fixed` because no tree selects it here: every tree without
   MAX_NKRO_BLOCKS also lacks the bitmap bound and is gated out below, so a separate
   value would be untested. */
static const kbd_case_t k_bitdo_cases[] = {
    {"a, on the 6KRO collection", {0x01, 0x00, 0x00, 0x04}, 9,      0x00, {10, 10}, {10, 10},
                                                                    true, {4}},
    {"shift + a",                 {0x01, 0x02, 0x00, 0x04}, 9,      0x02, {10, 10}, {10, 10},
                                                                    true, {4}},
    {"six keys at once",          {0x01, 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 9,
                                                                    0x00, {10, 16, 18, 25, 26, 32},
                                                                          {10, 16, 18, 25, 26, 32},
                                                                    true, {4, 5, 6, 7, 8, 9}},
    {"usage 4 on the NKRO collection", {0x0C, 0x00, 0x10}, 17,      0x00, {4, 4}, {4, 4},
                                                                    true, {4}},

    /* The two bursts emu/main.c pushes through the NKRO collection, the rig's positive
       control and then Enter to break the line, pinned so every report the rig puts on
       the wire has a host-side answer and the script cannot quietly drift from what the
       README says. The control uses usages 54 to 56 rather than letters on purpose: they
       sit in bitmap bytes 6 and 7, wire offsets 8 and 9, past the eight bytes
       _extract_kbd_boot copies, so in boot protocol they produce nothing and the rig's
       output loses its tail. k_bitdo_boot_cases pins that, and it is what stops a
       boot-protocol run reading as a pass: the 8BitDo's 6KRO layout is the boot layout,
       so report ID 1 decodes to abcdef there with or without the fix. */
    {"usages 54-56 (,./), NKRO",  {0x0C, 0x00, [8] = 0xC0, [9] = 0x01}, 17,
                                                                    0x00, {54, 55, 56, 54, 55, 56},
                                                                          {54, 55, 56, 54, 55, 56},
                                                                    true, {54, 55, 56}},
    {"usage 40 (enter), NKRO",    {0x0C, 0x00, [7] = 0x01}, 17,     0x00, {40, 40}, {40, 40},
                                                                    true, {40}},
};

/* The same three reports emu/main.c sends, decoded in boot protocol: extract_kbd_data
   returns _extract_kbd_boot before the descriptor is consulted, so both columns are the
   same on every tree. These rows are why the rig is trustworthy. The 8BitDo's 6KRO layout
   is the boot layout, so the first row decodes to the abcdef a fixed firmware produces
   and a boot-protocol run would otherwise read as a pass; the other two are the tell.
   _extract_kbd_boot copies eight bytes from the front of the 17-byte NKRO report, so the
   control's bits at wire offsets 8 and 9 never arrive and the rig's output loses its
   ",./" tail and line breaks; modifier 0x0C is the report ID read as a modifier, and the
   lone 0x01 in the last row is usage 40's bit at wire offset 7, inside the copy, decoding
   to ErrorRollOver rather than Enter. */
static const kbd_case_t k_bitdo_boot_cases[] = {
    {"6KRO burst, boot layout matches",  {0x01, 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 9,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"control vanishes in boot",         {0x0C, 0x00, [8] = 0xC0, [9] = 0x01}, 17,
                                                                    0x0C, {0}, {0}},
    {"enter becomes ErrorRollOver",      {0x0C, 0x00, [7] = 0x01}, 17,
                                                                    0x0C, {[5] = 1}, {[5] = 1}},
};


/* Microsoft Sculpt receiver, interface 0 (issue #367): a boot keyboard with no report
   ID, LED output block declared ahead of the input. Same len == 8 shortcut as
   d_boot_keyboard; the entry is here so the receiver's keyboard half is measured
   alongside its mouse half rather than assumed from the shape. */
static const kbd_case_t k_sculpt_cases[] = {
    {"a",                  {0x00, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"left GUI alone",     {0x08, 0x00, 0x00}, 8,                   0x08, {0}, {0}},
    {"ctrl + alt + delete",{0x05, 0x00, 0x4C}, 8,                   0x05, {0x4C}, {0x4C}},
};

/* Apple A2520, interface 1 (issue #157). Report 1 as the reporter captured it: ten bytes,
   [id][modifiers][reserved][six keys][Eject, Fn, Menu and one more consumer bit, 4 pad].
   uses_report_id is set, so _extract_kbd_other reads the key slots at bytes 2 to 7 after
   the ID. The trailing byte of bits is declared under the keyboard collection on consumer
   and vendor pages, which no map row claims, so Eject and Fn never reach the host. */
static const kbd_case_t k_apple_a2520_cases[] = {
    {"q",                  {0x01, 0x00, 0x00, 0x14}, 10,             0x00, {0x14}, {0x14}},
    {"shift + q",          {0x01, 0x02, 0x00, 0x14}, 10,             0x02, {0x14}, {0x14}},
    {"Eject bit, no key",  {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 10,
                                                                     0x00, {0}, {0}},
    {"released",           {0x01, 0x00, 0x00, 0x00}, 10,             0x00, {0}, {0}},
};

/* Keyboard on report ID 1 with an explicit reserved byte, [id][modifier][reserved][six
   keys], nine bytes. _extract_kbd_other drops the ID and picks the keycodes out of bytes
   2 to 7. Three devices ship this exact shape: the Logitech G502's keyboard interface
   ([#17]), the ZMK Corne ([#79]) and the G Pro Superlight 2 on its cable ([#215]). */
static const kbd_case_t k_rid1_reserved_cases[] = {
    {"a",                  {0x01, 0x00, 0x00, 0x04}, 9,             0x00, {4}, {4}},
    {"shift + a",          {0x01, 0x02, 0x00, 0x04}, 9,             0x02, {4}, {4}},
    {"six keys at once",   {0x01, 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 9,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"every modifier",     {0x01, 0xFF, 0x00, 0x00}, 9,             0xFF, {0}, {0}},
};

/* Corsair Strafe RGB in BIOS mode, [#45], the mode the reporter says works. The capture
   shows nine-byte reports for a descriptor declaring eight, and the len ==
   KBD_REPORT_LENGTH + 1 rule in _extract_kbd_boot reads the first byte as a stray report
   ID: the modifier is dropped, the reserved byte read as the modifier, and the first key
   slot lands where the reserved byte was. So 'd' with 'e' comes out as 'e' alone and
   right alt with '0' as nothing; the eight-byte rows are the same keys without the
   padding byte. Same on every tree. */
static const kbd_case_t k_strafe_bios_cases[] = {
    {"d + e, nine bytes as captured",   {0x00, 0x00, 0x07, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,
                                                                    0x00, {8}, {8}},
    {"right alt + 0, nine bytes as captured", {0x40, 0x00, 0x27, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9,
                                                                    0x00, {0}, {0}},
    {"d + e in eight bytes",            {0x00, 0x00, 0x07, 0x08}, 8, 0x00, {7, 8}, {7, 8}},
    {"right alt + 0 in eight bytes",    {0x40, 0x00, 0x27}, 8,       0x40, {0x27}, {0x27}},
};

/* Corsair Strafe RGB in its normal mode, [#45]: a 152-bit bitmap over usages 0 to 0x97 on
   report ID 1, [id][modifier][19 bytes]. One usage per bit exactly, so every tree keeps
   it. The first row is a report the reporter captured, 22 bytes with a padding byte on
   the end: bit 7 of the first bitmap byte and bit 1 of the second, usages 7 and 9. */
static const kbd_case_t k_strafe_cases[] = {
    {"d + f, as captured",  {0x01, 0x00, 0x80, 0x02, [21] = 0x00}, 22, 0x00, {7, 9}, {7, 9}},
    {"usage 4 (a)",         {0x01, 0x00, 0x10}, 21,                 0x00, {4}, {4}},
    {"shift + a",           {0x01, 0x02, 0x10}, 21,                 0x02, {4}, {4}},
    {"more keys than fit",  {0x01, 0x00, 0xF0, 0x1F}, 21,           0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"highest usage, 151",  {0x01, 0x00, [20] = 0x80}, 21,          0x00, {151}, {151}},
};

/* Corsair Scimitar RGB Elite, [#45]: the twelve-key side panel is a keyboard collection on
   report ID 0x10 inside the mouse's interface, the Strafe's bitmap shape. The mouse half of
   this interface waits behind HARNESS_FIELD_32 in cases_mouse.h; the keyboard half does
   not touch get_report_value and decodes everywhere. */
static const kbd_case_t k_scimitar_kbd_cases[] = {
    {"usage 4 on the side panel", {0x10, 0x00, 0x10}, 21,          0x00, {4}, {4}},
    {"usage 30 (1) on the side panel", {0x10, 0x00, 0x00, 0x00, 0x00, 0x40}, 21,
                                                                    0x00, {30}, {30}},
    {"highest usage, 151",  {0x10, 0x00, [20] = 0x80}, 21,          0x00, {151}, {151}},
    {"modifier byte, declared but not on the panel", {0x10, 0x02, 0x00}, 21,
                                                                    0x02, {0}, {0}},
};

/* Logi Bolt receiver at bcdDevice 5.01, interface 0, [#47]: the Superlight 2 receiver's
   three key ranges with no report ID, [modifier][112 bits][5 bits][3 bits], sixteen
   bytes. Before #359 main kept the first range only, so usages 4 to 115 worked and the
   IME keys in the other two came out as nothing; a tree keeping every block returns
   them. */
static const kbd_case_t k_bolt_v501_cases[] = {
    {"usage 4 (a)",                   {[1] = 0x01}, 16,             0x00, {4}, {4}},
    {"usage 115, end of block 0",     {[14] = 0x80}, 16,            0x00, {115}, {115}},
    {"usage 135, IME key in block 1", {[15] = 0x01}, 16,            0x00, {0}, {135}},
    {"usage 144, block 2",            {[15] = 0x20}, 16,            0x00, {0}, {144}},
    {"shift + a",                     {[0] = 0x02, [1] = 0x01}, 16, 0x02, {4}, {4}},
};

/* Apple A1243, [#48]: a boot layout whose key array has five slots, followed by one byte
   on vendor page 0xFF usage 3, the Fn key. Eight bytes and no report ID, so the len == 8
   shortcut copies the report as a boot report and the Fn byte arrives as the sixth
   keycode: Fn held reads as ErrorRollOver in slot six. The reporter says 0.51 made the
   keyboard work, and these rows agree that the keys decode; the last two are the cost. */
static const kbd_case_t k_a1243_cases[] = {
    {"a",                       {0x00, 0x00, 0x04}, 8,              0x00, {4}, {4}},
    {"shift + a",               {0x02, 0x00, 0x04}, 8,              0x02, {4}, {4}},
    {"five keys, the whole array", {0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08}, 8,
                                                                    0x00, {4, 5, 6, 7, 8}, {4, 5, 6, 7, 8}},
    {"Fn held, read as key slot six", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 8,
                                                                    0x00, {[5] = 1}, {[5] = 1}},
    {"Fn + a",                  {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x01}, 8,
                                                                    0x00, {4, 0, 0, 0, 0, 1}, {4, 0, 0, 0, 0, 1}},
};

/* Dry Studio Black Diamond 75, interface 0, [#50]: no report ID and no key array at all,
   [modifier][constant byte][224-bit bitmap over usages 0 to 0xDF], thirty bytes. One usage
   per bit, so every tree keeps the block, and the bitmap starts at byte 2. */
static const kbd_case_t k_blackdiamond_cases[] = {
    {"usage 4 (a)",         {[2] = 0x10}, 30,                       0x00, {4}, {4}},
    {"shift + a",           {[0] = 0x02, [2] = 0x10}, 30,           0x02, {4}, {4}},
    {"more keys than fit",  {[2] = 0xF0, [3] = 0x1F}, 30,           0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"highest usage, 223",  {[29] = 0x80}, 30,                      0x00, {223}, {223}},
};

/* Leopold FC660C on TMK, interface 4, [#142]: the second keyboard interface, a 248-bit
   bitmap over usages 0 to 0xF7 straight after the modifier, no report ID, 32 bytes. */
static const kbd_case_t k_fc660c_nkro_cases[] = {
    {"usage 4 (a)",         {[1] = 0x10}, 32,                       0x00, {4}, {4}},
    {"shift + a",           {[0] = 0x02, [1] = 0x10}, 32,           0x02, {4}, {4}},
    {"highest usage, 247",  {[31] = 0x80}, 32,                      0x00, {247}, {247}},
};

/* Kinesis Advantage360 Pro on ZMK, [#160], "F24 is not sent". Report ID 1, then the
   modifier, a constant byte, and a 152-bit bitmap over usages 0 to 0x97, 22 bytes. F24 is
   usage 115, bit 3 of the fifteenth bitmap byte, and it decodes: whatever loses it is not
   the keyboard decode. */
static const kbd_case_t k_adv360_cases[] = {
    {"usage 4 (a)",         {0x01, 0x00, 0x00, 0x10}, 22,           0x00, {4}, {4}},
    {"shift + a",           {0x01, 0x02, 0x00, 0x10}, 22,           0x02, {4}, {4}},
    {"F24, usage 115",      {0x01, 0x00, 0x00, [17] = 0x08}, 22,    0x00, {115}, {115}},
    {"highest usage, 151",  {0x01, 0x00, 0x00, [21] = 0x80}, 22,    0x00, {151}, {151}},
};

/* QMK's shared endpoint, the NKRO keyboard on report ID 6, [#17], [#30], [#61]. The
   d_nkro_keyboard layout on a different ID: [id][modifier][30 bytes], one usage per bit
   over 0 to 0xEF. */
static const kbd_case_t k_qmk_shared_cases[] = {
    {"usage 4 (a)",         {0x06, 0x00, 0x10}, 32,                 0x00, {4}, {4}},
    {"shift + a + b",       {0x06, 0x02, 0x30}, 32,                 0x02, {4, 5}, {4, 5}},
    {"more keys than fit",  {0x06, 0x00, 0xF0, 0x1F}, 32,           0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"highest usage, 239",  {0x06, 0x00, [31] = 0x80}, 32,          0x00, {239}, {239}},
};

/* Keychron 2.4 GHz dongle, interface 2, [#211]: a 6KRO keyboard on report ID 1 and an
   NKRO keyboard on report ID 12 declaring 153 usages over 152 bits, the Ultra-Link's
   off-by-one. The 6KRO rows are reports the reporter typed, ten bytes with a padding byte
   on the end; they decode on every tree, before #359 by luck: the collapsed keyboard_t is
   flagged NKRO, but _extract_kbd_nkro rejects the wide range and falls through to
   _extract_kbd_other, whose key_array the NKRO block never overwrote. The NKRO rows need
   the wide-range acceptance and only that: without it the block is discarded, the report
   falls to _extract_kbd_other, and two VARIABLE items give no key slots, so the modifier
   decodes and no key does. */
static const kbd_case_t k_keychron_dongle_cases[] = {
    {"n + d, as typed",     {0x01, 0x00, 0x00, 0x11, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}, 10,
                                                                    0x00, {0x11, 0, 0, 0x07}, {0x11, 0, 0, 0x07}},
    {"k d f j, as typed",   {0x01, 0x00, 0x00, 0x0E, 0x07, 0x09, 0x0D, 0x00, 0x00, 0x00}, 10,
                                                                    0x00, {0x0E, 0x07, 0x09, 0x0D}, {0x0E, 0x07, 0x09, 0x0D}},
    {"usage 4 on the NKRO collection", {0x0C, 0x00, 0x10}, 21,      0x00, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"shift + a on the NKRO collection", {0x0C, 0x02, 0x10}, 21,    0x02, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"highest usage, 151, NKRO collection", {0x0C, 0x00, [20] = 0x80}, 21,
                                                                    0x00, {0}, {0},
                                                                    false, {0}, true, {151}},
};

/* The Areson trackball's keyboard collection, [#23]: no report ID of its own on an
   interface where every other collection has one. uses_report_id is set, so
   _extract_kbd_other takes the first byte of the eight-byte boot report as the ID and
   reads the rest one byte late: modifier taken for the ID, reserved byte for the
   modifier, first key slot in the reserved position. 'a' alone decodes to nothing, shift
   is lost, six keys come back as five. Same on every tree that reaches the answer;
   without the length guard in _extract_kbd_other the read runs one byte past the eight,
   hence the gate. */
static const kbd_case_t k_areson_kbd_cases[] = {
    {"a, modifier byte taken as the ID",  {0x00, 0x00, 0x04}, 8,    0x00, {0}, {0}},
    {"shift + a, shift byte taken as the ID", {0x02, 0x00, 0x04}, 8, 0x00, {0}, {0}},
    {"six keys, the first lands in the reserved slot", {0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {5, 6, 7, 8, 9}, {5, 6, 7, 8, 9}},
};


#define DEV(d, p, c) {#d, d_##d, (int)sizeof(d_##d), p, c, (unsigned)ARRAY_SIZE(c)}

static const kbd_device_t kbd_devices[] = {
    DEV(boot_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(rpi_keyboard, HID_PROTOCOL_REPORT, k_rpi_cases),
    DEV(boot_keyboard, HID_PROTOCOL_BOOT, k_boot_protocol_cases),
    DEV(composite, HID_PROTOCOL_REPORT, k_composite_cases),
    DEV(nkro_keyboard, HID_PROTOCOL_REPORT, k_nkro_cases),
    DEV(keyboardio_keyboard, HID_PROTOCOL_REPORT, k_keyboardio_cases),
    DEV(superlight2_rx_keyboard, HID_PROTOCOL_REPORT, k_superlight2_cases),
    DEV(wooting_keyboard, HID_PROTOCOL_REPORT, k_wooting_cases),
    DEV(bolt_rx_keyboard, HID_PROTOCOL_REPORT, k_bolt_rx_cases),
    DEV(ultralink_keyboard, HID_PROTOCOL_REPORT, k_ultralink_kbd_cases),
    DEV(ultralink_iface1, HID_PROTOCOL_REPORT, k_ultralink_iface1_cases),
    DEV(ultralink_nkro_keyboard, HID_PROTOCOL_REPORT, k_ultralink_nkro_cases),
#ifdef HARNESS_BOUNDED_BITMAP
    /* Only on a tree that bounds the bitmap walk. Elsewhere the collapse routes this
       device's 9-byte 6KRO report into a 120-bit walk and the read goes off the end -
       a separate finding, and one truncate and shortreport already measure. */
    DEV(bitdo_retro_iface2, HID_PROTOCOL_REPORT, k_bitdo_cases),
#endif
    /* Not gated: boot protocol returns before the bitmap walk, so the unbounded read
       that keeps the entry above behind HARNESS_BOUNDED_BITMAP cannot happen here. */
    DEV(bitdo_retro_iface2, HID_PROTOCOL_BOOT, k_bitdo_boot_cases),
    DEV(kbd_with_bit_field, HID_PROTOCOL_REPORT, k_bit_field_cases),
    DEV(sculpt_rx_keyboard, HID_PROTOCOL_REPORT, k_sculpt_cases),
    DEV(apple_a2520_iface1, HID_PROTOCOL_REPORT, k_apple_a2520_cases),
    /* The September 2026 sweep. The nine keyboards on k_boot_cases are the plain
       eight-byte boot layout with no report ID, and take the same len == 8 shortcut as
       d_boot_keyboard. */
    DEV(unifying_rx_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(unifying_rx_b_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(qmk_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(ps2_converter_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(cherry_mw8_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(cherry_mw8c_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(fc660c_tmk_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(issue71_qmk_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(roccat_kone_keyboard, HID_PROTOCOL_REPORT, k_boot_cases),
    DEV(g502_iface1, HID_PROTOCOL_REPORT, k_rid1_reserved_cases),
    DEV(zmk_corne, HID_PROTOCOL_REPORT, k_rid1_reserved_cases),
    DEV(superlight2_wired_keyboard, HID_PROTOCOL_REPORT, k_rid1_reserved_cases),
    DEV(strafe_bios_keyboard, HID_PROTOCOL_REPORT, k_strafe_bios_cases),
    DEV(strafe_iface0, HID_PROTOCOL_REPORT, k_strafe_cases),
    DEV(scimitar_iface0, HID_PROTOCOL_REPORT, k_scimitar_kbd_cases),
    DEV(bolt_rx_v501_keyboard, HID_PROTOCOL_REPORT, k_bolt_v501_cases),
    DEV(apple_a1243_keyboard, HID_PROTOCOL_REPORT, k_a1243_cases),
    DEV(blackdiamond75_keyboard, HID_PROTOCOL_REPORT, k_blackdiamond_cases),
    DEV(fc660c_tmk_nkro, HID_PROTOCOL_REPORT, k_fc660c_nkro_cases),
    DEV(adv360_pro, HID_PROTOCOL_REPORT, k_adv360_cases),
    DEV(qmk_shared_endpoint, HID_PROTOCOL_REPORT, k_qmk_shared_cases),
    DEV(keychron_dongle_keyboard, HID_PROTOCOL_REPORT, k_keychron_dongle_cases),
#ifdef HARNESS_BOUNDED_KEY_ARRAY
    /* Only on a tree whose _extract_kbd_other stops at the bytes that arrived: elsewhere
       the one-byte-late read of an eight-byte report runs off its end. PR #359 bounds the
       bitmap walk but not this loop, which is why this is its own flag. */
    DEV(areson_trackball, HID_PROTOCOL_REPORT, k_areson_kbd_cases),
#endif
};

#undef DEV

/* The closed form of each gate above, printed as the run's last line. */
static const kept_out_t kbd_kept_out[] = {
#ifndef HARNESS_BOUNDED_BITMAP
    {"bitdo_retro_iface2", "target does not bound the bitmap walk by the report length"},
#endif
#ifndef HARNESS_BOUNDED_KEY_ARRAY
    {"areson_trackball", "target's key array loop does not stop at the bytes that arrived"},
#endif
    {NULL, NULL},
};

#pragma GCC diagnostic pop
