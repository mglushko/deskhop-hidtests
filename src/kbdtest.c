/* End to end keyboard decode.
 *
 * Parses a keyboard's descriptor, then pushes reports through extract_kbd_data()
 * and checks the 8-byte boot-style report that comes out. Expected values are
 * worked out by hand from the descriptor bytes, so this fails if either the
 * parser or the extraction changes meaning.
 *
 * Unlike the mouse path, nothing here needs lifting. extract_kbd_data and its
 * three helpers - _extract_kbd_boot, _extract_kbd_other, _extract_kbd_nkro -
 * all live in hid_report.c, which every binary here already compiles. Only the
 * declarations sit in keyboard.h; keyboard.c just calls in.
 *
 * The devices, chosen for the path each one takes through extract_kbd_data:
 *
 *   boot_keyboard           _extract_kbd_boot, the plain 8-byte report
 *   rpi_keyboard            same, plus the len == KBD_REPORT_LENGTH + 1 case
 *   boot_protocol           the HID_PROTOCOL_BOOT early return, which ignores
 *                           the descriptor entirely
 *   composite               _extract_kbd_other: keyboard behind report ID 1,
 *                           keys picked out of key_array rather than assumed
 *   nkro_keyboard           _extract_kbd_nkro, one 240-bit block
 *   keyboardio_keyboard     an NKRO block starting at bit 68, four bits into a
 *                           byte - the shape [#216] blames for shifted keys
 *   superlight2_rx_keyboard three blocks, of which main keeps the first
 *   wooting_keyboard        four blocks, of which main keeps the last
 *
 * The last two decode differently depending on the branch under test, so every
 * case carries two expectations: `keys` is what main produces, `keys_fixed` what
 * a parser that keeps every NKRO block produces. MAX_NKRO_BLOCKS exists only on
 * the latter, so the right column is selected at compile time and one unmodified
 * file asserts correctly against both. Rows where the two columns differ are
 * exactly the rows the multi-block fix moves.
 *
 * Each report is copied into an exact-size allocation before being decoded, the
 * same trick mousetest.c and truncate.c use, so ASan's redzone catches a read
 * past the end of the report rather than letting it pass as a plausible key.
 */
#include "main.h"
#include "descriptors.h"

#ifdef MAX_NKRO_BLOCKS
#define KEEPS_EVERY_BLOCK 1
#else
#define KEEPS_EVERY_BLOCK 0
#endif

#define REPORT_MAX 40

typedef struct {
    const char *what;
    uint8_t     report[REPORT_MAX];
    int         len;
    uint8_t     modifier;
    uint8_t     keys[6];       /* expected on main */
    uint8_t     keys_fixed[6]; /* expected once every NKRO block is kept */
} case_t;

typedef struct {
    const char    *name;
    const uint8_t *desc;
    int            desc_len;
    uint8_t        protocol;
    const case_t  *cases;
    unsigned       count;
} device_cases_t;

/* Plain boot layout: [modifier][reserved][6 keycodes], no report ID, and
   is_nkro clear, so extract_kbd_data takes the len == 8 shortcut. */
static const case_t boot_cases[] = {
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
static const case_t rpi_cases[] = {
    {"a",                  {0x00, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"ctrl + alt + delete",{0x05, 0x00, 0x4C}, 8,                   0x05, {0x4C}, {0x4C}},
    {"stray leading id byte", {0x01, 0x02, 0x00, 0x04}, 9,          0x02, {4}, {4}},
};

/* HID_PROTOCOL_BOOT returns before the descriptor is consulted at all, so the
   bytes are taken at face value whatever d_boot_keyboard happens to declare. */
static const case_t boot_protocol_cases[] = {
    {"shift + a, boot protocol", {0x02, 0x00, 0x04}, 8,             0x02, {4}, {4}},
};

/* Keyboard on report ID 1 of a three-collection dongle. uses_report_id is set,
   so the boot shortcut is skipped and _extract_kbd_other runs: it drops the ID
   byte, takes the modifier from modifier.offset_idx, and picks keycodes out of
   the byte positions key_array marked - here bytes 1 through 6. */
static const case_t composite_cases[] = {
    {"a",                  {0x01, 0x00, 0x04}, 8,                   0x00, {4}, {4}},
    {"ctrl + alt + delete",{0x01, 0x05, 0x4C}, 8,                   0x05, {0x4C}, {0x4C}},
    {"six keys at once",   {0x01, 0x00, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 8,
                                                                    0x00, {4, 5, 6, 7, 8, 9}, {4, 5, 6, 7, 8, 9}},
};

/* One 240-bit block on report ID 1: [id][modifier][30 bytes of bitmap]. The
   block starts at bit 8 and usage_min is 0, so usage u is simply bit u of the
   bitmap, counting from bit 0 of byte 2. */
static const case_t nkro_cases[] = {
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
static const case_t keyboardio_cases[] = {
    {"usage 4 (a)",        {[8] = 0x10}, 36,                        0x00, {4}, {4}},
    {"usage 5 and 6",      {[8] = 0x60}, 36,                        0x00, {5, 6}, {5, 6}},
    {"usage 12, next byte",{[9] = 0x10}, 36,                        0x00, {12}, {12}},
    {"shift + a",          {[0] = 0x02, [8] = 0x10}, 36,            0x02, {4}, {4}},
};

/* Logitech G Pro Superlight 2 receiver, [#215]. Three blocks; main keeps only
   the first, because only that one clears the size > 32 filter. Usages 4 to 115
   work either way. Usage 135 is the first of the Japanese and Korean IME keys
   that live in the second block, and it is the case that separates the two. */
static const case_t superlight2_cases[] = {
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
static const case_t wooting_cases[] = {
    {"usage 4 (a), block 0",   {[1] = 0x01}, 28,                    0x00, {0}, {4}},
    {"usage 51, block 1",      {[7] = 0x01}, 28,                    0x00, {0}, {51}},
    {"usage 176, block 3",     {[22] = 0x01}, 28,                   0x00, {176}, {176}},
    {"shift held, no key",     {[0] = 0x02}, 28,                    0x02, {0}, {0}},
    {"shift + a",              {[0] = 0x02, [1] = 0x01}, 28,        0x02, {0}, {4}},
};

#define DEV(d, p, c) {#d, d_##d, (int)sizeof(d_##d), p, c, (unsigned)ARRAY_SIZE(c)}

static const device_cases_t devices[] = {
    DEV(boot_keyboard, HID_PROTOCOL_REPORT, boot_cases),
    DEV(rpi_keyboard, HID_PROTOCOL_REPORT, rpi_cases),
    DEV(boot_keyboard, HID_PROTOCOL_BOOT, boot_protocol_cases),
    DEV(composite, HID_PROTOCOL_REPORT, composite_cases),
    DEV(nkro_keyboard, HID_PROTOCOL_REPORT, nkro_cases),
    DEV(keyboardio_keyboard, HID_PROTOCOL_REPORT, keyboardio_cases),
    DEV(superlight2_rx_keyboard, HID_PROTOCOL_REPORT, superlight2_cases),
    DEV(wooting_keyboard, HID_PROTOCOL_REPORT, wooting_cases),
};

#undef DEV

static void print_keys(const uint8_t *k) {
    int printed = 0;

    for (int i = 0; i < 6; i++)
        if (k[i])
            printed += printf("%s%u", printed ? "," : "", k[i]);

    if (!printed)
        printed = printf("-");

    /* count the dash too, or keyless rows run a column wide */
    for (int i = printed; i < 17; i++)
        printf(" ");
}

static int run_device(const device_cases_t *dev) {
    static hid_interface_t iface;

    memset(&iface, 0, sizeof(iface));
    iface.protocol = dev->protocol;
    parse_report_descriptor(&iface, dev->desc, dev->desc_len);

    printf("%s (%d bytes)%s\n", dev->name, dev->desc_len,
           dev->protocol == HID_PROTOCOL_BOOT ? ", boot protocol" : "");
    printf("  num_keyboards = %u, uses_report_id = %d, is_nkro = %d\n\n",
           iface.num_keyboards, iface.uses_report_id, iface.keyboards[0].is_nkro);

    printf("  %-32s %4s   %8s   %-17s %s\n", "keys held", "mod", "returned", "decoded", "");
    printf("  ");
    for (int i = 0; i < 78; i++)
        printf("-");
    printf("\n");

    int failures = 0;
    for (unsigned i = 0; i < dev->count; i++) {
        const case_t         *c = &dev->cases[i];
        hid_keyboard_report_t out;
        const uint8_t        *want = KEEPS_EVERY_BLOCK ? c->keys_fixed : c->keys;

        /* a len past the end of the array would overread the struct below */
        if (c->len < 0 || (size_t)c->len > sizeof(c->report)) {
            printf("  %-32s len %d exceeds report[%zu] - fix the case\n", c->what, c->len,
                   sizeof(c->report));
            failures++;
            continue;
        }

        /* exact-size allocation: an overread lands in ASan's redzone rather than
           in the next case's bytes */
        uint8_t *report = malloc(c->len);
        memcpy(report, c->report, c->len);

        memset(&out, 0, sizeof(out));
        int32_t ret = extract_kbd_data(report, c->len, 0, &iface, &out);

        free(report);

        int ok = out.modifier == c->modifier && memcmp(out.keycode, want, 6) == 0;
        if (!ok)
            failures++;

        printf("  %-32s 0x%02X   %8d   ", c->what, out.modifier, ret);
        print_keys(out.keycode);

        if (ok) {
            /* flag the rows the multi-block fix is responsible for */
            printf("ok%s\n", memcmp(c->keys, c->keys_fixed, 6) ? "   <- multi-block" : "");
        } else {
            printf("MISMATCH, wanted mod 0x%02X ", c->modifier);
            print_keys(want);
            /* the input matters more than the output when a case fails, and the
               NKRO reports are too long to work out from the case name */
            printf("\n%38sfrom ", "");
            for (int b = 0; b < c->len; b++)
                printf("%02X%s", c->report[b], b + 1 < c->len ? " " : "\n");
        }
    }

    printf("\n  %u/%u reports decoded as the descriptor specifies\n\n",
           dev->count - failures, dev->count);
    return failures;
}

int main(void) {
    int failures = 0, total = 0;

    printf("expectations: %s\n\n",
           KEEPS_EVERY_BLOCK ? "parser keeps every NKRO block (MAX_NKRO_BLOCKS defined)"
                             : "parser keeps one NKRO block (main)");

    for (unsigned i = 0; i < ARRAY_SIZE(devices); i++) {
        failures += run_device(&devices[i]);
        total += devices[i].count;
    }

    printf("%d/%d cases across %u devices\n", total - failures, total,
           (unsigned)ARRAY_SIZE(devices));
    return failures ? 1 : 0;
}
