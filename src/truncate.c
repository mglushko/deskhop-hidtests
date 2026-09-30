/* Feed every truncation of every descriptor to the parser.
 *
 *   ./truncate              run all prefixes of all descriptors, print a summary
 *   ./truncate <name> <n>   run one case in process with the ASan report visible
 *
 * A device can present a short or malformed descriptor, and the parse loop reads a header
 * and then up to four data bytes (item.hdr = *(header_t *)report++, then
 * get_descriptor_value(report, item.hdr.size)) without checking they are still inside
 * the buffer. Each prefix is copied into its own exact-size heap allocation, so ASan's
 * redzone sits immediately after the last valid byte and an overread is caught rather
 * than silently reading neighbouring data; each case runs in a forked child, so one crash
 * does not hide the remaining cases. The summary counts the failures by what the
 * sanitizer reported, so a read past the buffer is counted as one rather than assumed.
 */
#include "main.h"
#include "descriptors.h"
#include "support.h"

static void parse_prefix(const descriptor_t *d, int n) {
    static hid_interface_t iface;

    /* exact size: a static buffer would leave the overread inside valid memory */
    uint8_t *buf = dup_exact("truncate", d->bytes, n);

    parse_iface(&iface, buf, n, HID_PROTOCOL_REPORT);

    free_exact(buf, n);
}

typedef struct {
    const descriptor_t *d;
    int                 n;
} prefix_job_t;

static void parse_prefix_job(const void *arg) {
    const prefix_job_t *job = arg;
    parse_prefix(job->d, job->n);
}

/* Returns 0 if the child came back clean, otherwise its exit status, with the failure
   named in why. */
static int run_isolated(const descriptor_t *d, int n, char *why, size_t why_len) {
    prefix_job_t job = {d, n};
    return run_forked("truncate", parse_prefix_job, &job, 1, why, why_len);
}

int main(int argc, char **argv) {
    /* single case, in process, so the ASan report lands on the terminal */
    if (argc == 3) {
        const descriptor_t *d = find_descriptor(argv[1]);
        if (!d) {
            fprintf(stderr, "truncate: no descriptor named '%s'\n", argv[1]);
            return 2;
        }
        long n;
        if (parse_arg("truncate", "length", argv[2], 1, d->len, &n))
            return 2;
        printf("parsing first %ld of %d bytes of %s\n", n, d->len, d->name);
        /* out before a sanitizer report ends the process, which flushes nothing */
        fflush(stdout);
        parse_prefix(d, (int)n);
        printf("clean\n");
        return 0;
    }

    if (argc != 1) {
        fprintf(stderr, "usage: truncate [<descriptor> <length>]\n");
        return 2;
    }

    sweep_unsymbolized(argv);

    printf("  %-27s %8s %10s   %s\n", "DESCRIPTOR", "lengths", "failures", "first failing length");
    print_rule(73);

    long total = 0, total_bad = 0;
    const descriptor_t *worst = NULL;
    int worst_len = 0;
    static tally_t kinds[TALLY_KINDS];

    for (unsigned i = 0; i < ARRAY_SIZE(descriptors); i++) {
        const descriptor_t *d = &descriptors[i];
        int bad = 0, first_bad = -1;

        for (int n = 1; n <= d->len; n++) {
            char why[128];

            total++;
            if (run_isolated(d, n, why, sizeof(why)) != 0) {
                tally_add(kinds, why);
                bad++;
                total_bad++;
                if (first_bad < 0) {
                    first_bad = n;
                    if (!worst) {
                        worst = d;
                        worst_len = n;
                    }
                }
            }
        }

        if (first_bad >= 0)
            printf("  %-27s %8d %10d   %d\n", d->name, d->len, bad, first_bad);
        else
            printf("  %-27s %8d %10s   -\n", d->name, d->len, "0");
    }

    printf("\n  %ld of %ld truncations failed\n", total_bad, total);
    print_tally(kinds);

    if (worst) {
        char  len[16];
        char *repro[] = {argv[0], (char *)worst->name, len, NULL};

        snprintf(len, sizeof(len), "%d", worst_len);
        printf("\n  reproducing the first failure: %s truncated to %d bytes\n\n", worst->name,
               worst_len);
        sweep_repro("truncate", repro);
        printf("\n  repeat it directly with: ./truncate %s %d\n", worst->name, worst_len);
        return 1;
    }

    printf("  every truncation parsed without reading outside the buffer\n");
    return 0;
}
