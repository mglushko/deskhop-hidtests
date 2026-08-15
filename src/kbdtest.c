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
#include "cases_kbd.h"

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

static int run_device(const kbd_device_t *dev) {
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
        const kbd_case_t         *c = &dev->cases[i];
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

    for (unsigned i = 0; i < ARRAY_SIZE(kbd_devices); i++) {
        failures += run_device(&kbd_devices[i]);
        total += kbd_devices[i].count;
    }

    printf("%d/%d cases across %u devices\n", total - failures, total,
           (unsigned)ARRAY_SIZE(kbd_devices));
    return failures ? 1 : 0;
}
