/* Keyboard decode cases, shared by kbdtest and shortreport.
 *
 * Split out of kbdtest.c so the two binaries cannot disagree about what a device
 * sends. shortreport replays each of these at every truncated length, so a case
 * added here is checked for both its decoded values and its behaviour on a short
 * report, without being written twice.
 *
 * Every case carries up to four expectations, each naming a tree: `keys` is what main
 * produces, `keys_fixed` what a parser keeping every NKRO block produces, `keys_multi`
 * what a parser holding one keyboard_t per collection produces, and `keys_wide` what a
 * parser keeping a usage range wider than its block produces. The right one is selected
 * at compile time from what the Makefile finds in the target, so one unmodified file
 * asserts correctly against every one of them.
 */
#pragma once

#include "descriptors.h"

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

typedef struct {
    const char *what;
    uint8_t     report[REPORT_MAX];
    int         len;
    uint8_t     modifier;
    uint8_t     keys[6];       /* expected on main */
    uint8_t     keys_fixed[6]; /* expected once every NKRO block is kept */

    /* A third state, for the devices whose answer moves again once an interface can
       hold more than one keyboard_t. MAX_NKRO_BLOCKS stopped separating the trees on
       its own: [#359] defines it and so does everything built on top, including trees
       without the multi-keyboard fix. Opt in per case, because an omitted array would
       otherwise read as "expects no keys" rather than "no third answer". */
    bool        has_multi;
    uint8_t     keys_multi[6];

    /* A fourth, for the collections whose bitmap is only kept once a usage range wider
       than its block is accepted. Independent of has_multi: a device can need one, the
       other, both or neither, and the Keychron's NKRO collection parsed on its own needs
       only this one, because there is nothing there for a second keyboard_t to hold. */
    bool        has_wide;
    uint8_t     keys_wide[6];
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

/* Logitech G Pro Superlight 2 receiver, [#215]. Three blocks; main keeps only
   the first, because only that one clears the size > 32 filter. Usages 4 to 115
   work either way. Usage 135 is the first of the Japanese and Korean IME keys
   that live in the second block, and it is the case that separates the two. */
static const kbd_case_t k_superlight2_cases[] = {
    {"usage 4 (a)",        {0x01, 0x00, 0x01}, 17,                  0x00, {4}, {4}},
    {"usage 115, end of block 0", {[0] = 0x01, [15] = 0x80}, 17,    0x00, {115}, {115}},
    {"usage 135, IME key in block 1", {[0] = 0x01, [16] = 0x01}, 17,
                                                                    0x00, {0}, {135}},
};

/* Wooting Two HE, [#335], "only CTRL, Shift & Win work". Four blocks, and main
   keeps the last - usages 176 to 221, none of which are letters. So the modifier
   byte survives and every ordinary key decodes to nothing, which is exactly the
   reported symptom. The first row is the bug in one line: 'a' held down, nothing
   comes out. */
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

/* The same reports against the whole of interface 1, which is what deskhop actually
   parses. Interface 1 declares two keyboard collections, on report IDs 7 and 0x11.

   The `keys` and `keys_fixed` columns hold WRONG answers on purpose, because they are
   what a tree without the multi-keyboard fix produces: get_keyboard() short-circuited
   on num_keyboards == 1 and handed back keyboards[PRIMARY_KEYBOARD] every time, so the
   0x11 collection wrote over the report ID 7 one. Its NKRO block sits at offset_idx 1
   and is VARIABLE, and handle_keyboard_descriptor_values assigns
   key_array[offset_idx] = (data_type == ARRAY) unconditionally, so that assignment
   CLEARED the key_array[1] the report ID 7 collection had set. Byte 1 is the first of
   that keyboard's six key slots, so the first keycode in every 6KRO report went
   missing: hold 'a' alone and nothing came out.

   keys_multi is what a tree with get_or_add_keyboard produces, which is simply the
   right answer - and it matches k_ultralink_kbd_cases above, the same collection parsed
   on its own. The two entries exist side by side precisely so they can be compared, and
   once the collapse is fixed they agree.

   The last row is report 0x11 on the same interface, and it is the only case here that
   needs both fixes at once. Without the collapse fix it resolves to the report ID 7 slot;
   with it, to a slot whose bitmap is still rejected for declaring 153 usages over 152
   bits. Both give nothing, which is why keys, keys_fixed and keys_multi all say so and
   only keys_wide carries the answer. */
static const kbd_case_t k_ultralink_iface1_cases[] = {
    {"a",                       {0x07, 0x00, 0x04}, 8,              0x00, {0}, {0},
                                                                    true, {4}},
    {"shift + a",               {0x07, 0x02, 0x04}, 8,              0x02, {0}, {0},
                                                                    true, {4}},
    {"six keys at once",        {0x07, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {5, 6, 7, 8, 9}, {5, 6, 7, 8, 9},
                                                                    true, {4, 5, 6, 7, 8, 9}},
    {"usage 4 (a), NKRO collection", {0x11, 0x00, 0x10}, 21,        0x00, {0}, {0},
                                                                    true, {0}, true, {4}},
    {"rig: ,./ on the NKRO collection", {0x11, 0x00, [8] = 0xC0, [9] = 0x01}, 21,
                                                                    0x00, {0}, {0},
                                                                    true, {0}, true, {54, 55, 56}},
    /* Enter is the one rig report whose bit lands inside the six bytes the collapsed
       key_array covers, so on a tree that collapses the two collections it comes back as
       ErrorRollOver rather than as nothing. In the fifth key slot, not the first: the
       collapse clears key_array[1], so _extract_kbd_other starts packing at byte 2 and
       byte 6, where this bit is, arrives fifth. Same shape the 8BitDo's boot rows record,
       reached here in report protocol and by a different route. */
    {"rig: enter on the NKRO collection", {0x11, 0x00, [7] = 0x01}, 21,
                                                                    0x00, {0, 0, 0, 0, 1}, {0, 0, 0, 0, 1},
                                                                    true, {0}, true, {40}},
};

/* Keychron Ultra-Link 8K, upstream issue 324, the NKRO collection on report ID 0x11
   alone, so the off-by-one range can be read without the key_array interaction on top
   of it.

   The bitmap is declared 19 00 2A 98 00 with 95 98: usage minimum 0, usage maximum 152,
   over 152 bits. That is 153 usages in 152 bits, and it is what the device ships. Three
   columns are wrong on purpose, because three trees throw the block away. main records
   it on size > 32 and then discards it in _extract_kbd_nkro, whose 1:1 recheck the range
   fails; [#359] and everything built on it reject it earlier, in maps_usage_per_bit.
   Either way nkro_count is 0 at decode time, the report falls to _extract_kbd_other, and
   both items in this collection are VARIABLE - so key_array is empty and every keycode
   disappears while the modifier decodes. That is why the second row still wants modifier
   0x02 and no keys.

   keys_wide is what a tree accepting a range that merely covers its block produces. The
   last row is the off-by-one itself: usage 151 is the highest bit with a home, and usage
   152, the one the range declares and the block has no room for, is not expected back. */
static const kbd_case_t k_ultralink_nkro_cases[] = {
    {"usage 4 (a)",              {0x11, 0x00, 0x10}, 21,            0x00, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"shift + a",                {0x11, 0x02, 0x10}, 21,            0x02, {0}, {0},
                                                                    false, {0}, true, {4}},
    {"highest usage, 151",       {0x11, 0x00, [20] = 0x80}, 21,     0x00, {0}, {0},
                                                                    false, {0}, true, {151}},
};

/* A 6KRO keyboard carrying a short keyboard-page bit field, [id][mod][F13-F20][6 keys].
   Both columns hold the same correct values: main ignores the 8-bit field because it
   filters on size > 32, and a parser deciding is_nkro on the summed block width ignores
   it too. Only a parser that flags NKRO per block fails these - it routes the report
   through _extract_kbd_nkro, which never reads key_array, and every keycode disappears
   while the modifiers keep working.

   The last case holds F13 down. It is not reported, on any branch: the bit field is not
   treated as a key bitmap, so its bits go nowhere. That is the cost of the size rule and
   these rows are here to state it, not to hide it. */
static const kbd_case_t k_bit_field_cases[] = {
    {"a",                       {0x01, 0x00, 0x00, 0x04}, 9,        0x00, {4}, {4}},
    {"shift + a",               {0x01, 0x02, 0x00, 0x04}, 9,        0x02, {4}, {4}},
    {"six keys at once",        {0x01, 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 9,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"F13 held, key array still read", {0x01, 0x00, 0x01, 0x04}, 9, 0x00, {4}, {4}},
    {"nothing held",            {0x01, 0x00, 0x00, 0x00}, 9,        0x00, {0}, {0}},
};

/* 8BitDo Retro Mechanical Keyboard, [#57], interface 2: three keyboard collections on
   one interface. A 6KRO keyboard on report ID 1, then NKRO bitmaps on 12 and 10.

   Worse than the Keychron above, because both of the 8BitDo's NKRO blocks map one usage
   per bit over 120 bits and so pass every test the parser applies. On a tree where they
   land on keyboards[0] alongside the 6KRO key array, is_nkro is set on that entry and
   _extract_kbd_nkro runs for report ID 1 as well - so a 6KRO report is decoded as though
   its bytes were bitmap bits. The keyboard does not lose one key, it types the wrong
   ones, which is what "I plugged my keyboard it did not work" looks like from the
   outside.

   The `keys` and `keys_fixed` columns are therefore both wrong on purpose again, and
   keys_multi is the right answer. The doubling in keys_fixed is not a typo: both NKRO
   blocks land on keyboards[0] and both get walked over the same report bytes, so each
   finds the same bit and the keycode comes out twice. The last row exercises the NKRO
   collection on its own report ID and is doubled for the same reason.

   `keys` mirrors `keys_fixed` rather than carrying a separate number. No tree selects it
   for this device: every tree without MAX_NKRO_BLOCKS also lacks the bitmap bound and is
   gated out below, so a value there would be untested. */
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

    /* The two bursts emu/main.c pushes through the NKRO collection: "xyz" as the
       hardware rig's positive control, then Enter to break the line. Pinned here so
       every report the rig puts on the wire has a host-side answer to compare against,
       and so a change to the script cannot quietly stop meaning what the README says.

       The control deliberately uses usages 54 to 56 rather than letters. They sit in
       bitmap bytes 6 and 7, at wire offsets 8 and 9, past the eight bytes
       _extract_kbd_boot copies - so in boot protocol they produce nothing and the rig's
       output loses its tail. k_bitdo_boot_cases below pins that, and it is what stops a
       boot-protocol run reading as a pass: the 8BitDo's 6KRO layout is the boot layout,
       so report ID 1 decodes to abcdef there whether or not the fix is present. */
    {"usages 54-56 (,./), NKRO",  {0x0C, 0x00, [8] = 0xC0, [9] = 0x01}, 17,
                                                                    0x00, {54, 55, 56, 54, 55, 56},
                                                                          {54, 55, 56, 54, 55, 56},
                                                                    true, {54, 55, 56}},
    {"usage 40 (enter), NKRO",    {0x0C, 0x00, [7] = 0x01}, 17,     0x00, {40, 40}, {40, 40},
                                                                    true, {40}},
};

/* The same three reports emu/main.c sends, decoded in boot protocol instead.
   extract_kbd_data returns _extract_kbd_boot before the descriptor is consulted, so
   none of this depends on the tree: both columns are the same everywhere.

   These rows are why the hardware rig is trustworthy. The 8BitDo's 6KRO layout is the
   boot layout, so the first row decodes to the same abcdef a correctly fixed firmware
   produces - a boot-protocol run would otherwise read as a pass. The other two rows are
   the tell: _extract_kbd_boot copies eight bytes from the front of a 17-byte NKRO
   report, so the control's bits at wire offsets 8 and 9 never arrive, and the rig's
   output loses its ",./" tail and its line breaks. Modifier 0x0C is the report ID being
   read as a modifier byte, and the lone 0x01 in the last row is usage 40's bit landing
   at wire offset 7, inside the copy, where it decodes to ErrorRollOver rather than
   Enter. */
static const kbd_case_t k_bitdo_boot_cases[] = {
    {"6KRO burst, boot layout matches",  {0x01, 0x00, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 9,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
    {"control vanishes in boot",         {0x0C, 0x00, [8] = 0xC0, [9] = 0x01}, 17,
                                                                    0x0C, {0}, {0}},
    {"enter becomes ErrorRollOver",      {0x0C, 0x00, [7] = 0x01}, 17,
                                                                    0x0C, {[5] = 1}, {[5] = 1}},
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
};

#undef DEV

#pragma GCC diagnostic pop
