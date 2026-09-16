/* Feed every truncation of every report to the decode path.
 *
 *   ./shortreport                    every case at every length, print a summary
 *   ./shortreport <entry> <n> <len>  run one case in process, ASan report visible
 *
 * An entry is named by its table and its descriptor, mouse/<name> or kbd/<name>, with
 * /boot on the end where the device is replayed in boot protocol: kbd/boot_keyboard/boot.
 * Five descriptors sit in both tables or twice in one, which is why the name alone is
 * not the entry; where it is unambiguous a bare <name> still selects it, as the repro
 * lines in FINDINGS.md do. The sweep's table and its repro line print the full form,
 * which names the same entry on every tree, whatever the HARNESS_* gates left out.
 *
 * The mirror image of truncate.c. That one truncates the *descriptor*, which a
 * device supplies once at enumeration; this one truncates the *report*, which a
 * device supplies thousands of times a second and which nothing validates against
 * the length the descriptor implied. A descriptor can declare a 30-byte NKRO
 * bitmap and the device can then send eight bytes, and every offset the parser
 * derived is now pointing past the end of the buffer.
 *
 * Two things make this work, both borrowed from truncate.c. Each prefix is copied
 * into its own exact-size heap allocation, so ASan's redzone sits immediately
 * after the last valid byte and an overread is caught rather than silently
 * reading the next case's bytes. And each case runs in a forked child, so one
 * crash does not hide the remaining thousands.
 *
 * WHAT LENGTHS ARE REACHABLE
 *
 * Each receiver in the firmware applies its own length guard before it reaches
 * the decode path, so the shortest report that can actually get through differs
 * per path. Replaying below that floor would report a bug no device can trigger,
 * which is worse than not testing at all - it would send someone upstream with a
 * patch for an unreachable case. The floors are MOUSE_MIN_LEN and KBD_MIN_LEN in
 * the case tables' headers, beside the rows they bound, and mousetest and kbdtest
 * refuse a row below them for the same reason. They are hand copies of firmware
 * logic and load bearing: raising a floor hides a real finding and lowering one
 * invents a false one.
 */
#include "main.h"
#include "cases_mouse.h"
#include "cases_kbd.h"
#include "support.h"

typedef enum { PATH_MOUSE, PATH_KBD } path_e;

/* One truncated decode. Values are deliberately not checked: at a length the
   device never promised, there is no right answer to assert - the question is
   only whether the read stayed inside the buffer. */
static void decode_prefix(path_e path, const void *dev_v, unsigned case_idx, int n) {
    static hid_interface_t iface;

    const uint8_t *desc;
    int            desc_len;
    uint8_t        protocol;
    const uint8_t *bytes;

    if (path == PATH_MOUSE) {
        const mouse_device_t *dev = dev_v;
        desc = dev->desc; desc_len = dev->desc_len; protocol = dev->protocol;
        bytes = dev->cases[case_idx].report;
    } else {
        const kbd_device_t *dev = dev_v;
        desc = dev->desc; desc_len = dev->desc_len; protocol = dev->protocol;
        bytes = dev->cases[case_idx].report;
    }

    parse_iface(&iface, desc, desc_len, protocol);

    /* exact size: a static buffer would leave the overread inside valid memory */
    uint8_t *report = dup_exact("shortreport", bytes, n);

    if (path == PATH_MOUSE) {
        device_t       state = {0};
        mouse_values_t v     = {0};
        extract_report_values(report, n, &state, &v, &iface);
    } else {
        hid_keyboard_report_t out;
        memset(&out, 0, sizeof(out));
        extract_kbd_data(report, n, 0, &iface, &out);
    }

    free(report);
}

typedef struct {
    path_e      path;
    const void *dev;
    unsigned    case_idx;
    int         n;
} decode_job_t;

static void decode_prefix_job(const void *arg) {
    const decode_job_t *job = arg;
    decode_prefix(job->path, job->dev, job->case_idx, job->n);
}

/* Returns 0 if the child came back clean, otherwise its exit status. */
static int run_isolated(path_e path, const void *dev, unsigned case_idx, int n, int quiet) {
    decode_job_t job = {path, dev, case_idx, n};
    return run_forked("shortreport", decode_prefix_job, &job, quiet);
}

/* Uniform view over the two case tables, so the driver below is written once. id is the
   entry's name on the command line and in the table: mouse/<name> or kbd/<name>, with
   /boot where the row is replayed in boot protocol. It is the row's own, not its
   position, so it selects the same entry on every tree. */
typedef struct {
    path_e      path;
    const void *dev;
    const char *name;
    char        id[64];
    unsigned    count;
    int         min_len;
} entry_t;

static int case_len(const entry_t *e, unsigned i) {
    if (e->path == PATH_MOUSE)
        return ((const mouse_device_t *)e->dev)->cases[i].len;
    return ((const kbd_device_t *)e->dev)->cases[i].len;
}

static const char *case_what(const entry_t *e, unsigned i) {
    if (e->path == PATH_MOUSE)
        return ((const mouse_device_t *)e->dev)->cases[i].what;
    return ((const kbd_device_t *)e->dev)->cases[i].what;
}

/* How many bytes a case's report[] can hold. mousetest and kbdtest refuse a len past it;
   this checks the same thing, so a mistyped case is reported rather than read past the
   struct in the one binary whose job is catching overreads. */
static int case_cap(const entry_t *e) {
    if (e->path == PATH_MOUSE)
        return (int)sizeof(((const mouse_case_t *)0)->report);
    return (int)sizeof(((const kbd_case_t *)0)->report);
}

/* The entry a command line names: the full form exactly, or a bare descriptor name when
   one entry alone carries it. Says why when nothing is selected. */
static const entry_t *find_entry(const entry_t *entries, unsigned num, const char *spec) {
    const entry_t *found = NULL;
    int            bare  = 0;

    for (unsigned i = 0; i < num; i++)
        if (strcmp(entries[i].id, spec) == 0)
            return &entries[i];

    for (unsigned i = 0; i < num; i++)
        if (strcmp(entries[i].name, spec) == 0) {
            found = &entries[i];
            bare++;
        }
    if (bare == 1)
        return found;

    if (bare == 0) {
        fprintf(stderr, "shortreport: no entry named '%s' has decode cases\n", spec);
        return NULL;
    }
    fprintf(stderr, "shortreport: '%s' is %d entries; name one of", spec, bare);
    for (unsigned i = 0; i < num; i++)
        if (strcmp(entries[i].name, spec) == 0)
            fprintf(stderr, " %s", entries[i].id);
    fprintf(stderr, "\n");
    return NULL;
}

/* One entry per row of the two device tables, mouse first, named as entry_t says. Two
   rows that would share a name are a table fault, refused here rather than left for the
   sweep to report under one name. */
static unsigned build_entries(entry_t *out, unsigned cap) {
    unsigned n = 0;

    for (unsigned i = 0; i < ARRAY_SIZE(mouse_devices) && n < cap; i++)
        out[n++] = (entry_t){PATH_MOUSE, &mouse_devices[i], mouse_devices[i].name, "",
                             mouse_devices[i].count, MOUSE_MIN_LEN};

    for (unsigned i = 0; i < ARRAY_SIZE(kbd_devices) && n < cap; i++)
        out[n++] = (entry_t){PATH_KBD, &kbd_devices[i], kbd_devices[i].name, "",
                             kbd_devices[i].count, KBD_MIN_LEN};

    for (unsigned i = 0; i < n; i++) {
        entry_t *e        = &out[i];
        uint8_t  protocol = e->path == PATH_MOUSE
                                ? ((const mouse_device_t *)e->dev)->protocol
                                : ((const kbd_device_t *)e->dev)->protocol;
        int      len      = snprintf(e->id, sizeof(e->id), "%s/%s%s",
                                     e->path == PATH_MOUSE ? "mouse" : "kbd", e->name,
                                     protocol == HID_PROTOCOL_BOOT ? "/boot" : "");

        if (len < 0 || (size_t)len >= sizeof(e->id)) {
            fprintf(stderr, "shortreport: %s does not fit an entry name of %zu bytes\n",
                    e->name, sizeof(e->id) - 1);
            exit(1);
        }
        for (unsigned j = 0; j < i; j++)
            if (strcmp(out[j].id, e->id) == 0) {
                fprintf(stderr, "shortreport: two entries named %s - fix the case tables\n",
                        e->id);
                exit(1);
            }
    }

    return n;
}

int main(int argc, char **argv) {
    entry_t  entries[ARRAY_SIZE(mouse_devices) + ARRAY_SIZE(kbd_devices)];
    unsigned num = build_entries(entries, ARRAY_SIZE(entries));

    /* single case, in process, so the ASan report lands on the terminal */
    if (argc == 4) {
        const entry_t *e = find_entry(entries, num, argv[1]);
        if (!e)
            return 2;

        long idx;
        if (parse_arg("shortreport", "case", argv[2], 0, (long)e->count - 1, &idx))
            return 2;

        int full = case_len(e, (unsigned)idx);
        if (!case_len_ok(full, e->min_len, (size_t)case_cap(e))) {
            fprintf(stderr, "shortreport: case %ld of %s has len %d outside %d..%d - fix the case\n",
                    idx, e->id, full, e->min_len, case_cap(e));
            return 1;
        }

        long n;
        if (parse_arg("shortreport", "length", argv[3], e->min_len, full, &n))
            return 2;

        printf("%s case %ld (%s): first %ld of %d report bytes\n", e->id, idx,
               case_what(e, (unsigned)idx), n, full);
        decode_prefix(e->path, e->dev, (unsigned)idx, (int)n);
        printf("clean\n");
        return 0;
    }

    if (argc != 1) {
        fprintf(stderr, "usage: shortreport [<entry> <case> <len>]\n"
                        "  <entry> is mouse/<name> or kbd/<name>, with /boot for a row in boot\n"
                        "  protocol, or a bare <name> when one entry alone carries it\n");
        return 2;
    }

    printf("  %-32s %8s %10s   %s\n", "ENTRY", "lengths", "failures",
           "first failing case, length");
    print_rule(81);

    long           total = 0, total_bad = 0;
    int            bad_cases = 0;
    const entry_t *worst = NULL;
    unsigned       worst_case = 0;
    int            worst_len = 0;

    for (unsigned d = 0; d < num; d++) {
        const entry_t *e = &entries[d];
        long           tried = 0, bad = 0;
        int            first_case = -1, first_len = 0;

        for (unsigned c = 0; c < e->count; c++) {
            int full = case_len(e, c);

            if (!case_len_ok(full, e->min_len, (size_t)case_cap(e))) {
                printf("  %-32s   case %u: len %d outside %d..%d - fix the case\n",
                       e->id, c, full, e->min_len, case_cap(e));
                bad_cases++;
                continue;
            }

            for (int n = e->min_len; n <= full; n++) {
                tried++;
                total++;
                if (run_isolated(e->path, e->dev, c, n, 1) != 0) {
                    bad++;
                    total_bad++;
                    if (first_case < 0) {
                        first_case = (int)c;
                        first_len  = n;
                        if (!worst) {
                            worst      = e;
                            worst_case = c;
                            worst_len  = n;
                        }
                    }
                }
            }
        }

        if (first_case >= 0)
            printf("  %-32s %8ld %10ld   case %d at %d bytes\n", e->id, tried, bad,
                   first_case, first_len);
        else
            printf("  %-32s %8ld %10s   -\n", e->id, tried, "0");
    }

    printf("\n  %ld of %ld truncated reports failed\n", total_bad, total);
    /* A table fault fails the run the way it does in mousetest and kbdtest, with the
       status an overread gets, and is named under the count it shrank; 2 stays the answer
       to a bad command line. */
    if (bad_cases)
        printf("  %d case(s) were not replayed and are missing from that count: fix them in "
               "the case tables\n", bad_cases);

    int rc = bad_cases ? 1 : 0;
    if (worst) {
        printf("\n  reproducing the first failure: %s case %u at %d bytes\n\n", worst->id,
               worst_case, worst_len);
        fflush(stdout);
        run_isolated(worst->path, worst->dev, worst_case, worst_len, 0);
        printf("\n  repeat it directly with: ./shortreport %s %u %d\n", worst->id, worst_case,
               worst_len);
        rc = 1;
    } else if (!bad_cases) {
        printf("  every truncated report decoded without reading outside the buffer\n");
    }

    /* Both tables' gates, since this binary replays both; the denominator above is
       only as complete as these lines say. */
    if (mouse_kept_out[0].device || kbd_kept_out[0].device)
        printf("\n");
    print_kept_out(mouse_kept_out, "  ");
    print_kept_out(kbd_kept_out, "  ");
    return rc;
}
