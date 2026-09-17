/* End to end consumer and system control.
 *
 * Parses a descriptor, then pushes real reports through process_consumer_report() and
 * process_system_report(), which tools/lift.py pulls verbatim out of the target's
 * keyboard.c. What the receivers hand to the send path is recorded by src/recorders.c
 * and asserted here. `compare` diffs the parse, so a change confined to keyboard.c is
 * invisible to it, and PR [#358] is exactly that change: this is the target that sees it.
 *
 * #358 changes function bodies only, no macro and no header, so there is nothing to
 * #ifdef on. Every case carries both answers and each row is classified instead: MAIN
 * matches want_main, FIXED want_fixed, AGREED both (the controls), and NEITHER is a
 * failure. The verdict at the end says which branch the code under test behaves like;
 * FINDINGS.md, "How #358 is measured", has the rest.
 *
 * Each report is decoded from an exact-size allocation, as in mousetest.c, kbdtest.c and
 * shortreport.c, so ASan's redzone catches a read past its end. process_consumer_report
 * reads raw_report[0] unconditionally, so a zero-length report is not a thing the
 * firmware survives and not a thing this feeds it.
 */
#include "main.h"
#include "cases_cc.h"
#include "handlers.h"
#include "support.h"

typedef enum { R_MAIN, R_FIXED, R_AGREED, R_NEITHER } verdict_e;

static const char *verdict_name(verdict_e v) {
    switch (v) {
        case R_MAIN:   return "pre-#358";
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

/* Which way a report should leave, and what it should carry. The active output sends
   through send_*_control(); the other board queues a packet, and that packet's type and
   length are what the receiving board decodes by, so a payload that is right but typed
   or sized wrong is lost all the same. */
static bool sent_right_way(const sent_t *got, cc_path_e path, bool local) {
    if (got->via == SENT_NOTHING)
        return true; /* matches() has already judged whether nothing was right */
    if (local)
        return got->via == (path == CC_SYSTEM ? SENT_SYSTEM_LOCAL : SENT_CONSUMER_LOCAL);
    return got->via == SENT_QUEUED &&
           got->packet_type == (path == CC_SYSTEM ? SYSTEM_CONTROL_MSG : CONSUMER_CONTROL_MSG) &&
           got->len == payload_len(path);
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

    uint8_t *report = dup_exact("cctest", c->report, c->len);

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

    print_hex(s->payload, n, 18);
}

/* Returns the number of cases that failed. A report ID not bound to the receiver under
   test is counted through *routing_failures instead, so the per-device tally stays a
   count of cases. */
static int run_device(const cc_device_t *dev, int *seen, int *routing_failures) {
    static hid_interface_t iface;

    parse_iface(&iface, dev->desc, dev->desc_len, HID_PROTOCOL_REPORT);

    printf("%s (%d bytes)\n", dev->name, dev->desc_len);
    printf("  uses_report_id = %d, %s: rid=%u var=%d arr=%d\n", iface.uses_report_id,
           dev->path == CC_SYSTEM ? "system" : "consumer",
           (unsigned)(dev->path == CC_SYSTEM ? iface.system.report_id : iface.consumer.report_id),
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
        (*routing_failures)++;
    } else {
        printf("  routing: report %u -> %s\n", dev->expect_report_id,
               dev->path == CC_SYSTEM ? "process_system_report" : "process_consumer_report");
    }

    printf("\n  %-24s %-14s %-18s %-18s %s\n", "keys held", "raw report", "sent (local)",
           "sent (remote)", "behaves like");
    print_rule(92);

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

        bool right_way = sent_right_way(&local, dev->path, true) &&
                         sent_right_way(&remote, dev->path, false);

        bool ok = v != R_NEITHER && same_both && right_way && local.calls <= 1 &&
                  remote.calls <= 1;
        if (!ok)
            failures++;
        else if (v != R_AGREED)
            seen[v]++;

        printf("  %-24s ", c->what);
        print_hex(c->report, c->len < 4 ? c->len : 4, 12);
        printf("  ");

        print_payload(&local, n);
        printf(" ");
        print_payload(&remote, n);
        printf(" %s", verdict_name(v));

        if (!ok) {
            printf("   MISMATCH\n");
            if (v == R_NEITHER) {
                printf("%28swanted pre-#358 ", "");
                if (!c->sent_main) printf("(nothing sent)");
                else print_hex(c->want_main, n, 0);
                printf(" or #358 ");
                if (!c->sent_fixed) printf("(nothing sent)");
                else print_hex(c->want_fixed, n, 0);
                printf("\n");
            }
            if (!same_both)
                printf("%28slocal and remote payloads differ - the send path changed the data\n",
                       "");
            if (!right_way)
                printf("%28ssent by the wrong path, or queued with the wrong packet type or "
                       "length\n", "");
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
    int failures = 0, routing = 0, total = 0;
    int seen[R_NEITHER];

    memset(seen, 0, sizeof(seen));

    for (unsigned i = 0; i < ARRAY_SIZE(cc_devices); i++) {
        failures += run_device(&cc_devices[i], seen, &routing);
        total += cc_devices[i].count;
    }

    printf("%d/%d cases across %u devices\n", total - failures, total,
           (unsigned)ARRAY_SIZE(cc_devices));
    /* The whole point of the target. Rows where the two branches agree say nothing
       about which one this is; only the separating rows do. */
    printf("\n  separating rows: %d behave like pre-#358 main, %d like #358\n", seen[R_MAIN],
           seen[R_FIXED]);

    bool split = seen[R_MAIN] && seen[R_FIXED];
    bool none  = !seen[R_MAIN] && !seen[R_FIXED];

    /* The verdict is what the separating rows say, whatever else went wrong; each failure
       kind then gets a line of its own, so one cannot hide another. */
    if (split)
        printf("  VERDICT: INCONSISTENT - some rows behave like pre-#358 main, some like #358\n");
    else if (seen[R_FIXED])
        printf("  VERDICT: this branch has the #358 fix\n");
    else if (seen[R_MAIN])
        printf("  VERDICT: this branch does NOT have the #358 fix\n");
    else
        printf("  VERDICT: no separating row ran - this target is proving nothing\n");

    if (failures)
        printf("  FAILED: %d case(s) matched neither branch\n", failures);
    if (routing)
        printf("  FAILED: %d device(s) whose report ID is not bound to the receiver under test\n",
               routing);

    /* An inconsistent split is a real failure: the two receivers got the fix
       independently, which is not a state either branch is supposed to be in. And a
       run with no separating row at all has silently stopped testing the thing it
       exists for - the same refusal as fuzz's touches == 0 and compare's seen == 0. */
    return (failures || routing || split || none) ? 1 : 0;
}
