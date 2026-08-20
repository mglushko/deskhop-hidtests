/* Generate descriptors and count every access that lands outside usages[].
 *
 *   ./fuzz [count] [seed]
 *
 * Must be linked against a parser built by tools/instrument.py, which routes each
 * usages[] access through dbg_touch() below and then clamps it, so one process can
 * chew through thousands of descriptors that would otherwise corrupt parser state
 * on the first one.
 *
 * The generator leans on the shapes that actually break things: large report
 * counts, long usage runs, and vendor pages. It is not trying to be a general HID
 * fuzzer, it is trying to hit the usage array hard.
 */
#include "main.h"

#include <errno.h>
#include <limits.h>

static long touches, out_of_bounds, highest;
static int  this_descriptor_went_out;

void dbg_touch(parser_state_t *parser, long abs_idx) {
    (void)parser;

    touches++;
    if (abs_idx > highest)
        highest = abs_idx;

    if (abs_idx < 0 || abs_idx >= HID_MAX_USAGES) {
        out_of_bounds++;
        this_descriptor_went_out = 1;
    }
}

/* xorshift so a seed reproduces a run exactly, independent of libc */
static uint32_t rng_state = 1;

static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint32_t rnd_below(uint32_t n) {
    return n ? rnd() % n : 0;
}

#define PUT1(hdr, v)   do { d[n++] = (hdr); d[n++] = (uint8_t)(v); } while (0)
#define PUT2(hdr, v)   do { d[n++] = (hdr); d[n++] = (uint8_t)(v); d[n++] = (uint8_t)((v) >> 8); } while (0)

static int generate(uint8_t *d, int cap) {
    int n = 0;
    int items = 4 + (int)rnd_below(30);

    /* usage page: mostly desktop, sometimes vendor */
    if (rnd_below(3) == 0)
        PUT2(0x06, 0xFFE0);
    else
        PUT1(0x05, 0x01);

    PUT1(0x09, 0x02);          /* Usage (Mouse) */
    PUT1(0xA1, 0x01);          /* Collection (Application) */

    for (int i = 0; i < items && n < cap - 16; i++) {
        switch (rnd_below(8)) {
            case 0: /* a run of usages, sometimes longer than the array holds */
                for (uint32_t k = 0, run = 1 + rnd_below(200); k < run && n < cap - 8; k++)
                    PUT1(0x09, rnd_below(0x100));
                break;

            case 1: /* usage min/max */
                PUT1(0x19, rnd_below(0x100));
                PUT1(0x29, rnd_below(0x100));
                break;

            case 2: /* report size */
                PUT1(0x75, (uint8_t)(1 + rnd_below(32)));
                break;

            case 3: /* small report count */
                PUT1(0x95, (uint8_t)(1 + rnd_below(64)));
                break;

            case 4: /* large report count, the interesting one */
                PUT2(0x96, 1 + rnd_below(4096));
                break;

            case 5: /* report ID */
                PUT1(0x85, (uint8_t)(1 + rnd_below(24)));
                break;

            case 6: /* input item */
                PUT1(0x81, rnd_below(4) ? 0x02 : 0x03);
                break;

            default: /* nested collection, opened and usually closed */
                PUT1(0xA1, rnd_below(2));
                if (rnd_below(4))
                    d[n++] = 0xC0;
                break;
        }
    }

    PUT1(0x81, 0x02);
    d[n++] = 0xC0;
    return n;
}

/* strtol, not atol: atol("abc") is 0 and indistinguishable from an explicit 0,
   and both used to run zero descriptors and then report that every access stayed
   in bounds. Returns 0 on success. */
static int parse_arg(const char *s, const char *make_var, long lo, long hi, long *out) {
    char *end;

    errno = 0;
    long v = strtol(s, &end, 0);

    if (end == s || *end != '\0' || errno == ERANGE || v < lo || v > hi) {
        fprintf(stderr, "fuzz: %s=%s is not a number in %ld..%ld\n", make_var, s, lo, hi);
        return 1;
    }

    *out = v;
    return 0;
}

int main(int argc, char **argv) {
    static hid_interface_t iface;
    uint8_t desc[8192];

    long count = 40000, seed = 1;

    if (argc > 1 && parse_arg(argv[1], "N", 1, LONG_MAX, &count))
        return 2;

    /* Seed 0 is the fixed point of xorshift: rnd() returns 0 for ever, generate()
       emits the same four-usage descriptor every time, and the run reports a clean
       pass on a parser that overflows to index 4564. Reject it rather than remap
       it, so `make fuzz SEED=0` cannot silently mean something else. */
    if (argc > 2 && parse_arg(argv[2], "SEED", 1, UINT32_MAX, &seed))
        return 2;

    rng_state = (uint32_t)seed;

    long bad_descriptors = 0;

    for (long i = 0; i < count; i++) {
        int len = generate(desc, (int)sizeof(desc));

        this_descriptor_went_out = 0;
        memset(&iface, 0, sizeof(iface));
        iface.protocol = HID_PROTOCOL_REPORT;
        parse_report_descriptor(&iface, desc, len);

        if (this_descriptor_went_out)
            bad_descriptors++;
    }

    printf("  descriptors parsed           : %ld\n", count);
    printf("  usages[] capacity            : %d\n", HID_MAX_USAGES);
    printf("  total accesses               : %ld\n", touches);
    printf("  highest index touched        : %ld\n", highest);
    printf("  out-of-bounds accesses       : %ld\n", out_of_bounds);
    printf("  descriptors going out of bounds: %ld\n", bad_descriptors);

    /* A run that never reached usages[] proves nothing about its bounds. Saying
       "all accesses stayed inside" would be true and useless - the same trap the
       compare target's check-ref rule refuses. Reaching here means the parser was
       instrumented but never touched the array, so the instrumentation is stale. */
    if (touches == 0) {
        printf("\n  no access to usages[] was recorded over %ld descriptor(s).\n", count);
        printf("  Refusing to report success - an unexercised bound is not a bound.\n");
        printf("  Check that tools/instrument.py still matches this parser.\n");
        return 1;
    }

    printf("  RESULT: %s\n", out_of_bounds ? "OUT OF BOUNDS" : "all accesses stayed inside usages[]");

    return out_of_bounds ? 1 : 0;
}
