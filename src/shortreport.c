/* Feed every truncation of every report to the decode path.
 *
 *   ./shortreport                    every case at every length, print a summary
 *   ./shortreport <device> <n> <len> run one case in process, ASan report visible
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
 * Last checked against main at 59577cc.
 */
#include "main.h"
#include "cases_mouse.h"
#include "cases_kbd.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

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

    memset(&iface, 0, sizeof(iface));
    iface.protocol = protocol;
    parse_report_descriptor(&iface, desc, desc_len);

    /* exact size: a static buffer would leave the overread inside valid memory */
    uint8_t *report = malloc((size_t)n);
    if (!report) {
        fprintf(stderr, "shortreport: out of memory\n");
        _exit(3);
    }
    memcpy(report, bytes, (size_t)n);

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

/* Returns 0 if the child came back clean, otherwise its exit status. */
static int run_isolated(path_e path, const void *dev, unsigned case_idx, int n, int quiet) {
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
        decode_prefix(path, dev, case_idx, n);
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : WEXITSTATUS(status);
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
        const entry_t *e = NULL;
        for (unsigned i = 0; i < num; i++)
            if (strcmp(entries[i].name, argv[1]) == 0) {
                e = &entries[i];
                break;
            }
        if (!e) {
            fprintf(stderr, "shortreport: no device named '%s' has decode cases\n", argv[1]);
            return 2;
        }

        int idx = atoi(argv[2]);
        if (idx < 0 || (unsigned)idx >= e->count) {
            fprintf(stderr, "shortreport: case must be 0..%u for %s\n", e->count - 1, e->name);
            return 2;
        }

        int full = case_len(e, (unsigned)idx);
        int n    = atoi(argv[3]);
        if (n < e->min_len || n > full) {
            fprintf(stderr, "shortreport: length must be %d..%d for that case\n", e->min_len, full);
            return 2;
        }

        printf("%s case %d (%s): first %d of %d report bytes\n", e->name, idx,
               case_what(e, (unsigned)idx), n, full);
        decode_prefix(e->path, e->dev, (unsigned)idx, n);
        printf("clean\n");
        return 0;
    }

    if (argc != 1) {
        fprintf(stderr, "usage: shortreport [<device> <case> <len>]\n");
        return 2;
    }

    printf("  %-24s %6s %8s %10s   %s\n", "DEVICE", "path", "lengths", "failures",
           "first failing case, length");
    printf("  ");
    for (int i = 0; i < 78; i++)
        printf("-");
    printf("\n");

    long           total = 0, total_bad = 0;
    const entry_t *worst = NULL;
    unsigned       worst_case = 0;
    int            worst_len = 0;

    for (unsigned d = 0; d < num; d++) {
        const entry_t *e = &entries[d];
        long           tried = 0, bad = 0;
        int            first_case = -1, first_len = 0;

        for (unsigned c = 0; c < e->count; c++) {
            int full = case_len(e, c);

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
            printf("  %-24s %6s %8ld %10ld   case %d at %d bytes\n", e->name,
                   e->path == PATH_MOUSE ? "mouse" : "kbd", tried, bad, first_case, first_len);
        else
            printf("  %-24s %6s %8ld %10s   -\n", e->name,
                   e->path == PATH_MOUSE ? "mouse" : "kbd", tried, "0");
    }

    printf("\n  %ld of %ld truncated reports failed\n", total_bad, total);

    if (worst) {
        printf("\n  reproducing the first failure: %s case %u at %d bytes\n\n", worst->name,
               worst_case, worst_len);
        fflush(stdout);
        run_isolated(worst->path, worst->dev, worst_case, worst_len, 0);
        printf("\n  repeat it directly with: ./shortreport %s %u %d\n", worst->name, worst_case,
               worst_len);
        return 1;
    }

    printf("  every truncated report decoded without reading outside the buffer\n");
    return 0;
}
