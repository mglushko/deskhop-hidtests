/* End to end consumer and system control.
 *
 * Parses a descriptor, then pushes real reports through process_consumer_report()
 * and process_system_report(), which tools/lift.py pulls verbatim out of the
 * target's keyboard.c. What the receivers hand to the send path is recorded by
 * src/recorders.c and asserted here.
 *
 * This is the target that closes the gap the README used to describe: `compare`
 * diffs the *parse*, so a change confined to keyboard.c is invisible to it. PR
 * [#358] is exactly that change, and compare prints `identical parse` on all 45
 * descriptors including cherry_kc6000_consumer, the device it fixes.
 *
 * WHICH BRANCH AM I LOOKING AT
 *
 * #358 changes function bodies only - no macro, no header change - so cases_kbd.h's
 * `#ifdef MAX_NKRO_BLOCKS` trick has nothing to test. Every case therefore carries
 * both answers, and this classifies the branch instead of assuming it: a row that
 * matches want_main is MAIN, one that matches want_fixed is FIXED, one that matches
 * both is AGREED (the controls), and one that matches neither is a failure. The
 * verdict at the end says which branch the code under test behaves like, which is
 * the question compare answers wrongly.
 *
 * Each report is copied into an exact-size allocation before being decoded, the same
 * trick mousetest.c, kbdtest.c and shortreport.c use, so ASan's redzone catches a
 * read past the end of the report rather than letting it pass as a plausible key.
 * process_consumer_report reads raw_report[0] unconditionally, so a zero-length
 * report is not a thing the firmware survives and not a thing this feeds it.
 */
#include "main.h"
#include "cases_cc.h"
#include "handlers.h"

typedef enum { R_MAIN, R_FIXED, R_AGREED, R_NEITHER } verdict_e;

static const char *verdict_name(verdict_e v) {
    switch (v) {
        case R_MAIN:   return "main";
        case R_FIXED:  return "#358";
        case R_AGREED: return "agreed";
        default:       return "NEITHER";
    }
}

/* How many payload bytes the receiver's send path actually carries. Both receivers
   pass a fixed constant, so this is a property of the path, not of the report. */
static int payload_len(cc_path_e path) {
    return path == CC_SYSTEM ? SYSTEM_CONTROL_LENGTH : CONSUMER_CONTROL_LENGTH;
}

static bool matches(const sent_t *got, bool want_sent, const uint8_t *want, int n) {
    if (!want_sent)
        return got->via == SENT_NOTHING;

    return got->via != SENT_NOTHING && memcmp(got->payload, want, (size_t)n) == 0;
}

/* Run one case once, with the board either being the active output or not. Returns
   what was recorded. */
static sent_t run_once(const cc_device_t *dev, const cc_case_t *c, hid_interface_t *iface,
                       bool local) {
    /* CURRENT_BOARD_IS_ACTIVE_OUTPUT is (active_output == board_role), so this is
       what decides send_*_control() against queue_packet(). Both are recorded. */
    global_state.board_role    = 0;
    global_state.active_output = local ? 0 : 1;

    harness_sent_reset();

    uint8_t *report = malloc((size_t)c->len);
    if (!report) {
        fprintf(stderr, "cctest: out of memory\n");
        exit(3);
    }
    memcpy(report, c->report, (size_t)c->len);

    if (dev->path == CC_SYSTEM)
        process_system_report(report, c->len, 0, iface);
    else
        process_consumer_report(report, c->len, 0, iface);

    free(report);
    return harness_sent;
}

static void print_payload(const sent_t *s, int n) {
    if (s->via == SENT_NOTHING) {
        printf("%-18s", "(nothing sent)");
        return;
    }

    int printed = 0;
    for (int i = 0; i < n; i++)
        printed += printf("%02X ", s->payload[i]);
    for (int i = printed; i < 18; i++)
        printf(" ");
}

static int run_device(const cc_device_t *dev, verdict_e *seen) {
    static hid_interface_t iface;

    memset(&iface, 0, sizeof(iface));
    iface.protocol = HID_PROTOCOL_REPORT;
    parse_report_descriptor(&iface, dev->desc, dev->desc_len);

    printf("%s (%d bytes)\n", dev->name, dev->desc_len);
    printf("  uses_report_id = %d, %s: rid=%u var=%d arr=%d\n", iface.uses_report_id,
           dev->path == CC_SYSTEM ? "system" : "consumer",
           dev->path == CC_SYSTEM ? iface.system.report_id : iface.consumer.report_id,
           dev->path == CC_SYSTEM ? iface.system.is_variable : iface.consumer.is_variable,
           dev->path == CC_SYSTEM ? iface.system.is_array : iface.consumer.is_array);

    int failures = 0;

    /* The parser has to have bound this report ID to the receiver under test, or the
       reports below would never reach it on real hardware however they decode here.
       Asserted rather than assumed, and separately from the decode. */
    process_report_f want_handler =
        dev->path == CC_SYSTEM ? process_system_report : process_consumer_report;

    if (hid_handler(&iface, dev->expect_report_id) != want_handler) {
        printf("  ROUTING: report %u is not bound to %s - reports would never arrive\n",
               dev->expect_report_id, dev->path == CC_SYSTEM ? "process_system_report"
                                                             : "process_consumer_report");
        failures++;
    } else {
        printf("  routing: report %u -> %s\n", dev->expect_report_id,
               dev->path == CC_SYSTEM ? "process_system_report" : "process_consumer_report");
    }

    printf("\n  %-24s %-14s %-18s %-18s %s\n", "keys held", "raw report", "sent (local)",
           "sent (remote)", "behaves like");
    printf("  ");
    for (int i = 0; i < 92; i++)
        printf("-");
    printf("\n");

    int n = payload_len(dev->path);

    for (unsigned i = 0; i < dev->count; i++) {
        const cc_case_t *c = &dev->cases[i];

        if (c->len < 1 || (size_t)c->len > sizeof(c->report)) {
            printf("  %-24s len %d outside 1..%zu - fix the case\n", c->what, c->len,
                   sizeof(c->report));
            failures++;
            continue;
        }

        sent_t local  = run_once(dev, c, &iface, true);
        sent_t remote = run_once(dev, c, &iface, false);

        bool m = matches(&local, c->sent_main, c->want_main, n);
        bool f = matches(&local, c->sent_fixed, c->want_fixed, n);

        verdict_e v = m && f ? R_AGREED : m ? R_MAIN : f ? R_FIXED : R_NEITHER;

        /* Which of the two send paths ran is decided by CURRENT_BOARD_IS_ACTIVE_OUTPUT
           and nothing else; the payload must not depend on it. */
        bool same_both = local.via == SENT_NOTHING
                             ? remote.via == SENT_NOTHING
                             : remote.via != SENT_NOTHING &&
                                   memcmp(local.payload, remote.payload, (size_t)n) == 0;

        bool ok = v != R_NEITHER && same_both && local.calls <= 1 && remote.calls <= 1;
        if (!ok)
            failures++;
        else if (v != R_AGREED)
            seen[v]++;

        printf("  %-24s ", c->what);
        for (int b = 0; b < c->len && b < 4; b++)
            printf("%02X ", c->report[b]);
        for (int b = c->len < 4 ? c->len : 4; b < 4; b++)
            printf("   ");
        printf("  ");

        print_payload(&local, n);
        printf(" ");
        print_payload(&remote, n);
        printf(" %s", verdict_name(v));

        if (!ok) {
            printf("   MISMATCH\n");
            if (v == R_NEITHER) {
                printf("%28swanted main ", "");
                if (!c->sent_main) printf("(nothing sent)");
                else for (int b = 0; b < n; b++) printf("%02X ", c->want_main[b]);
                printf(" or #358 ");
                if (!c->sent_fixed) printf("(nothing sent)");
                else for (int b = 0; b < n; b++) printf("%02X ", c->want_fixed[b]);
                printf("\n");
            }
            if (!same_both)
                printf("%28slocal and remote payloads differ - the send path changed the data\n",
                       "");
            if (local.calls > 1 || remote.calls > 1)
                printf("%28s%d/%d sends from one report - should be exactly one\n", "",
                       local.calls, remote.calls);
        } else {
            printf("\n");
        }
    }

    printf("\n  %u/%u reports sent what the descriptor and branch specify\n\n",
           dev->count - failures, dev->count);
    return failures;
}

int main(void) {
    int       failures = 0, total = 0;
    verdict_e seen[R_NEITHER];

    memset(seen, 0, sizeof(seen));

    for (unsigned i = 0; i < ARRAY_SIZE(cc_devices); i++) {
        failures += run_device(&cc_devices[i], seen);
        total += cc_devices[i].count;
    }

    printf("%d/%d cases across %u devices\n", total - failures, total,
           (unsigned)ARRAY_SIZE(cc_devices));

    /* The whole point of the target. Rows where the two branches agree say nothing
       about which one this is; only the separating rows do. */
    printf("\n  separating rows: %d behave like main, %d like #358\n", seen[R_MAIN],
           seen[R_FIXED]);

    if (failures)
        printf("  VERDICT: cannot classify - %d case(s) matched neither branch\n", failures);
    else if (seen[R_MAIN] && seen[R_FIXED])
        printf("  VERDICT: INCONSISTENT - some rows behave like main, some like #358\n");
    else if (seen[R_FIXED])
        printf("  VERDICT: this branch has the #358 fix\n");
    else if (seen[R_MAIN])
        printf("  VERDICT: this branch does NOT have the #358 fix\n");
    else
        printf("  VERDICT: no separating row ran - this target is proving nothing\n");

    /* An inconsistent split is a real failure: the two receivers got the fix
       independently, which is not a state either branch is supposed to be in. And a
       run with no separating row at all has silently stopped testing the thing it
       exists for - the same refusal as fuzz's touches == 0 and compare's seen == 0. */
    if (!failures && ((seen[R_MAIN] && seen[R_FIXED]) || (!seen[R_MAIN] && !seen[R_FIXED])))
        return 1;

    return failures ? 1 : 0;
}
