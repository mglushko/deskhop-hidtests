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
 *   ultralink_nkro_keyboard a usage range one wider than the block it covers,
 *                           which every tree before this one threw away
 *
 * Those decode differently depending on the branch under test, so a case carries
 * up to four expectations, each naming a tree: `keys` is what main produces,
 * `keys_fixed` what a parser keeping every NKRO block produces, `keys_multi` what
 * a parser holding one keyboard_t per collection produces, and `keys_wide` what a
 * parser keeping a usage range wider than its block produces. The right one is
 * selected at compile time from what the Makefile finds in the target, so one
 * unmodified file asserts correctly against every one of them.
 *
 * Each report is copied into an exact-size allocation before being decoded, the
 * same trick mousetest.c and truncate.c use, so ASan's redzone catches a read
 * past the end of the report rather than letting it pass as a plausible key.
 */
#include "main.h"
#include "cases_kbd.h"
#include "handlers.h"

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


/* A corpus-wide structural check, rather than two devices with hand-written answers.
 *
 * On a tree that gives each collection its own keyboard_t, one property has to hold for
 * every descriptor here: any report ID the parser bound to process_keyboard_report must
 * resolve, through the firmware's own get_keyboard(), to a slot that claims that ID. If
 * it resolves to a slot claiming a different one, two collections are sharing a
 * keyboard_t and whichever was parsed second has written over the first.
 *
 * This is what the two multi-collection devices demonstrate case by case, stated once
 * over all 47 descriptors instead. It needs no expectations and no knowledge of any
 * particular device, so it holds for descriptors added later, which is the part the
 * hand-written cases cannot do. Gated because it is false by construction on a tree
 * where get_keyboard() short-circuits.
 *
 * It parses the whole corpus, so it also needs a parser that survives the whole corpus:
 * on a tree without the usages[] bounds fix, gameball_gesture and many_usages take the
 * run down before the check reaches its own conclusion. Every tree that allocates a
 * keyboard_t per collection has that fix too, so the one gate covers both, but the
 * dependency is worth knowing if the two ever come apart.
 *
 * Measured discriminating: against PR #361, which bounds usages[] but does not allocate
 * per collection, it reports three violations - ultralink_iface1 report 17 resolving to a
 * slot claiming 7, and both of the 8BitDo's NKRO IDs resolving to the 6KRO slot.
 */
static int check_keyboard_slots(void) {
    static hid_interface_t iface;
    int broken = 0;

    printf("keyboard slots: every bound report ID resolves to a slot claiming it\n\n");

    for (unsigned i = 0; i < ARRAY_SIZE(descriptors); i++) {
        const descriptor_t *d = &descriptors[i];

        memset(&iface, 0, sizeof(iface));
        iface.protocol = HID_PROTOCOL_REPORT;
        parse_report_descriptor(&iface, d->bytes, d->len);

        for (int rid = 0; rid < 256; rid++) {
            if (hid_handler(&iface, rid) != process_keyboard_report)
                continue;

            const keyboard_t *kb = get_keyboard(&iface, (uint8_t)rid);

            if (kb->report_id != rid) {
                printf("  %-26s report %d resolves to a slot claiming %u\n", d->name, rid,
                       kb->report_id);
                broken++;
            }
        }

        /* Two slots claiming the same ID would make get_keyboard's answer depend on
           search order rather than on the descriptor. */
        for (int a = 0; a < iface.num_keyboards && a < MAX_KEYBOARDS; a++)
            for (int b = a + 1; b < iface.num_keyboards && b < MAX_KEYBOARDS; b++)
                if (iface.keyboards[a].report_id == iface.keyboards[b].report_id) {
                    printf("  %-26s slots %d and %d both claim report %u\n", d->name, a, b,
                           iface.keyboards[a].report_id);
                    broken++;
                }
    }

    if (broken)
        printf("\n  %d violation(s): collections are sharing a keyboard_t\n\n", broken);
    else
        printf("  %u descriptors, no collection shares a keyboard_t with another\n\n",
               (unsigned)ARRAY_SIZE(descriptors));

    return broken;
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
        const uint8_t        *want = (ACCEPTS_WIDE_USAGE_RANGE && c->has_wide)
                                         ? c->keys_wide
                                     : (ONE_KEYBOARD_PER_COLLECTION && c->has_multi)
                                         ? c->keys_multi
                                     : KEEPS_EVERY_BLOCK ? c->keys_fixed : c->keys;

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
            printf("ok%s\n", (ACCEPTS_WIDE_USAGE_RANGE && c->has_wide)
                                 ? "   <- wide usage range"
                             : (ONE_KEYBOARD_PER_COLLECTION && c->has_multi)
                                 ? "   <- multi-keyboard"
                             : memcmp(c->keys, c->keys_fixed, 6) ? "   <- multi-block"
                                                                 : "");
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

    printf("expectations: %s\n", KEEPS_EVERY_BLOCK
               ? "parser keeps every NKRO block (MAX_NKRO_BLOCKS defined)"
               : "parser keeps one NKRO block (main)");
    printf("              %s\n", ONE_KEYBOARD_PER_COLLECTION
               ? "one keyboard_t per collection (get_or_add_keyboard present)"
               : "all collections on one interface share keyboard_t");
    printf("              %s\n\n", ACCEPTS_WIDE_USAGE_RANGE
               ? "a usage range wider than its block is kept (is_key_bitmap present)"
               : "a usage range wider than its block is rejected");

    for (unsigned i = 0; i < ARRAY_SIZE(kbd_devices); i++) {
        failures += run_device(&kbd_devices[i]);
        total += kbd_devices[i].count;
    }

    printf("%d/%d cases across %u devices\n\n", total - failures, total,
           (unsigned)ARRAY_SIZE(kbd_devices));

#if ONE_KEYBOARD_PER_COLLECTION
    failures += check_keyboard_slots();
#endif

    return failures ? 1 : 0;
}
