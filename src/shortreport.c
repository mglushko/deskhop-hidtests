/* Feed every truncation of every report to the decode path.
 *
 *   ./shortreport                         every case at every length, print a summary
 *   ./shortreport <device>[#k] <n> <len>  run one case in process, ASan report visible
 *
 * Five names sit in both case tables or twice in one, so a bare name selects the first
 * entry carrying it in table order and <device>#2 the second. The repro line at the end
 * of a sweep prints whichever form names its entry exactly.
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
 * patch for an unreachable case. The floors, read out of the receivers:
 *
 *   process_mouse_report     no guard at all         -> 1 byte
 *   process_keyboard_report  length < KBD_REPORT_LENGTH returns -> 8 bytes
 *
 * These are hand copies of firmware logic, in the same way mousetest.c's
 * dispatch() is, and carry the same risk of going stale. Unlike dispatch() they
 * are load bearing: raising a floor hides a real finding and lowering one invents
 * a false one. Re-check them against keyboard.c and mouse.c when touching either.
 * Last checked against upstream c220d0c and DeskHop Extended 637b985.
 */
#include "main.h"
#include "cases_mouse.h"
#include "cases_kbd.h"
#include "support.h"

/* process_mouse_report hands whatever arrived straight to extract_report_values */
#define MOUSE_MIN_LEN 1

/* process_keyboard_report returns early on length < KBD_REPORT_LENGTH */
#define KBD_MIN_LEN   KBD_REPORT_LENGTH

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

/* Uniform view over the two case tables, so the driver below is written once. */
typedef struct {
    path_e      path;
    const void *dev;
    const char *name;
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

/* Five names sit in both tables or twice in one, so a name alone can be ambiguous. The
   k-th entry carrying a name, counted from 1 in table order, is selected as `name#k`;
   a bare name is `name#1`. */
static int name_count(const entry_t *entries, unsigned num, const char *name) {
    int n = 0;

    for (unsigned i = 0; i < num; i++)
        if (strcmp(entries[i].name, name) == 0)
            n++;
    return n;
}

/* NULL when nothing matches; *matches says how many entries carry the name, or -1 when
   the #k suffix itself was malformed and parse_arg has already said so. */
static const entry_t *find_entry(const entry_t *entries, unsigned num, const char *spec,
                                 int *matches) {
    const char *hash = strchr(spec, '#');
    size_t      len  = hash ? (size_t)(hash - spec) : strlen(spec);
    long        want = 1;

    *matches = -1;
    if (hash && parse_arg("shortreport", "entry", hash + 1, 1, LONG_MAX, &want))
        return NULL;

    *matches = 0;
    const entry_t *found = NULL;
    for (unsigned i = 0; i < num; i++) {
        if (strncmp(entries[i].name, spec, len) != 0 || entries[i].name[len] != '\0')
            continue;
        if (++*matches == want)
            found = &entries[i];
    }
    return found;
}

/* The form of an entry's name that selects exactly it on the command line. */
static void print_entry_name(const entry_t *entries, unsigned num, const entry_t *e) {
    if (name_count(entries, num, e->name) == 1) {
        printf("%s", e->name);
        return;
    }

    int k = 0;
    for (const entry_t *p = entries; p <= e; p++)
        if (strcmp(p->name, e->name) == 0)
            k++;
    printf("%s#%d", e->name, k);
}

static unsigned build_entries(entry_t *out, unsigned cap) {
    unsigned n = 0;

    for (unsigned i = 0; i < ARRAY_SIZE(mouse_devices) && n < cap; i++)
        out[n++] = (entry_t){PATH_MOUSE, &mouse_devices[i], mouse_devices[i].name,
                             mouse_devices[i].count, MOUSE_MIN_LEN};

    for (unsigned i = 0; i < ARRAY_SIZE(kbd_devices) && n < cap; i++)
        out[n++] = (entry_t){PATH_KBD, &kbd_devices[i], kbd_devices[i].name,
                             kbd_devices[i].count, KBD_MIN_LEN};

    return n;
}

int main(int argc, char **argv) {
    entry_t  entries[ARRAY_SIZE(mouse_devices) + ARRAY_SIZE(kbd_devices)];
    unsigned num = build_entries(entries, ARRAY_SIZE(entries));

    /* single case, in process, so the ASan report lands on the terminal */
    if (argc == 4) {
        int            matches;
        const entry_t *e = find_entry(entries, num, argv[1], &matches);
        if (!e) {
            if (matches == 0)
                fprintf(stderr, "shortreport: no device named '%s' has decode cases\n", argv[1]);
            else if (matches > 0)
                fprintf(stderr, "shortreport: '%s' names %d entries; pick one with #1..#%d\n",
                        argv[1], matches, matches);
            return 2;
        }

        long idx;
        if (parse_arg("shortreport", "case", argv[2], 0, (long)e->count - 1, &idx))
            return 2;

        int full = case_len(e, (unsigned)idx);
        if (full < 0 || full > case_cap(e)) {
            fprintf(stderr,
                    "shortreport: case %ld of %s has len %d, past its report[%d] - fix the case\n",
                    idx, e->name, full, case_cap(e));
            return 2;
        }

        long n;
        if (parse_arg("shortreport", "length", argv[3], e->min_len, full, &n))
            return 2;

        printf("%s case %ld (%s): first %ld of %d report bytes\n", e->name, idx,
               case_what(e, (unsigned)idx), n, full);
        decode_prefix(e->path, e->dev, (unsigned)idx, (int)n);
        printf("clean\n");
        return 0;
    }

    if (argc != 1) {
        fprintf(stderr, "usage: shortreport [<device>[#k] <case> <len>]\n");
        return 2;
    }

    printf("  %-27s %6s %8s %10s   %s\n", "DEVICE", "path", "lengths", "failures",
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

            if (full < 0 || full > case_cap(e)) {
                printf("  %-27s %6s   case %u: len %d past its report[%d] - fix the case\n",
                       e->name, e->path == PATH_MOUSE ? "mouse" : "kbd", c, full, case_cap(e));
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
            printf("  %-27s %6s %8ld %10ld   case %d at %d bytes\n", e->name,
                   e->path == PATH_MOUSE ? "mouse" : "kbd", tried, bad, first_case, first_len);
        else
            printf("  %-27s %6s %8ld %10s   -\n", e->name,
                   e->path == PATH_MOUSE ? "mouse" : "kbd", tried, "0");
    }

    printf("\n  %ld of %ld truncated reports failed\n", total_bad, total);

    int rc = 0;
    if (worst) {
        printf("\n  reproducing the first failure: ");
        print_entry_name(entries, num, worst);
        printf(" case %u at %d bytes\n\n", worst_case, worst_len);
        fflush(stdout);
        run_isolated(worst->path, worst->dev, worst_case, worst_len, 0);
        printf("\n  repeat it directly with: ./shortreport ");
        print_entry_name(entries, num, worst);
        printf(" %u %d\n", worst_case, worst_len);
        rc = 1;
    } else {
        printf("  every truncated report decoded without reading outside the buffer\n");
    }

    if (bad_cases) {
        printf("\n  %d case(s) were not replayed: fix them in the case tables\n", bad_cases);
        rc = 2;
    }

    /* Both tables' gates, since this binary replays both; the denominator above is
       only as complete as these lines say. */
    if (mouse_kept_out[0].device || kbd_kept_out[0].device)
        printf("\n");
    print_kept_out(mouse_kept_out, "  ");
    print_kept_out(kbd_kept_out, "  ");
    return rc;
}
