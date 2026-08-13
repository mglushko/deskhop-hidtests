/* Feed every truncation of every descriptor to the parser.
 *
 *   ./truncate              run all prefixes of all descriptors, print a summary
 *   ./truncate <name> <n>   run one case in process with the ASan report visible
 *
 * A device can present a short or malformed descriptor, and the parse loop reads a
 * header and then up to four data bytes without checking they are still inside the
 * buffer:
 *
 *     while (desc_len > 0) {
 *         item.hdr = *(header_t *)report++;
 *         item.val = get_descriptor_value(report, item.hdr.size);
 *
 * Two things make this test work. Each prefix is copied into its own exact-size
 * heap allocation, so ASan's redzone sits immediately after the last valid byte and
 * an overread is caught rather than silently reading neighbouring data. And each
 * case runs in a forked child, so one crash does not hide the remaining cases.
 */
#include "main.h"
#include "descriptors.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static void parse_prefix(const descriptor_t *d, int n) {
    static hid_interface_t iface;

    /* exact size: a static buffer would leave the overread inside valid memory */
    uint8_t *buf = malloc((size_t)n);
    memcpy(buf, d->bytes, (size_t)n);

    memset(&iface, 0, sizeof(iface));
    iface.protocol = HID_PROTOCOL_REPORT;

    parse_report_descriptor(&iface, buf, n);

    free(buf);
}

/* Returns 0 if the child came back clean, otherwise its exit status. */
static int run_isolated(const descriptor_t *d, int n, int quiet) {
    fflush(stdout);

    pid_t pid = fork();
    if (pid == 0) {
        if (quiet) {
            int null = open("/dev/null", O_WRONLY);
            if (null >= 0) {
                dup2(null, 2);
                dup2(null, 1);
            }
        }
        parse_prefix(d, n);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
}

int main(int argc, char **argv) {
    /* single case, in process, so the ASan report lands on the terminal */
    if (argc == 3) {
        const descriptor_t *d = find_descriptor(argv[1]);
        if (!d) {
            fprintf(stderr, "truncate: no descriptor named '%s'\n", argv[1]);
            return 2;
        }
        int n = atoi(argv[2]);
        if (n < 1 || n > d->len) {
            fprintf(stderr, "truncate: length must be 1..%d\n", d->len);
            return 2;
        }
        printf("parsing first %d of %d bytes of %s\n", n, d->len, d->name);
        parse_prefix(d, n);
        printf("clean\n");
        return 0;
    }

    printf("  %-22s %8s %10s   %s\n", "DESCRIPTOR", "lengths", "failures", "first failing length");
    printf("  ");
    for (int i = 0; i < 68; i++)
        printf("-");
    printf("\n");

    long total = 0, total_bad = 0;
    const descriptor_t *worst = NULL;
    int worst_len = 0;

    for (unsigned i = 0; i < ARRAY_SIZE(descriptors); i++) {
        const descriptor_t *d = &descriptors[i];
        int bad = 0, first_bad = -1;

        for (int n = 1; n <= d->len; n++) {
            total++;
            if (run_isolated(d, n, 1) != 0) {
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
            printf("  %-22s %8d %10d   %d\n", d->name, d->len, bad, first_bad);
        else
            printf("  %-22s %8d %10s   -\n", d->name, d->len, "0");
    }

    printf("\n  %ld of %ld truncations failed\n", total_bad, total);

    if (worst) {
        printf("\n  reproducing the first failure: %s truncated to %d bytes\n\n", worst->name,
               worst_len);
        fflush(stdout);
        run_isolated(worst, worst_len, 0);
        printf("\n  repeat it directly with: ./truncate %s %d\n", worst->name, worst_len);
        return 1;
    }

    printf("  every truncation parsed without reading outside the buffer\n");
    return 0;
}
