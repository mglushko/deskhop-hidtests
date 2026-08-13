/* Report Count is a 32-bit global item, so values up to 4.29 billion are legal
   syntax. Bounding the usage array stops the memory corruption but the element
   loop in handle_main_input() still runs once per declared element.
 *
 * Measures cost per element on the host. The number that matters is the target's:
 * the RP2040 runs a Cortex-M0+ at 120 MHz, and DeskHop's watchdog fires after
 * 500 ms, which is a budget of 60 million cycles for the whole parse.
 */
#include "main.h"
#include <time.h>

/* Vendor collection with one usage and a 4-byte Report Count, the same shape the
   Gameball uses with the count dialled up. Report Size 8 keeps it off the
   size==1 fast path in handle_main_input(). */
static int build(uint8_t *d, uint32_t count) {
    int n = 0;

    d[n++] = 0x06; d[n++] = 0xE0; d[n++] = 0xFF;   /* Usage Page (vendor FFE0)  */
    d[n++] = 0x09; d[n++] = 0x01;                  /* Usage (1)                 */
    d[n++] = 0xA1; d[n++] = 0x01;                  /* Collection (Application)  */
    d[n++] = 0x09; d[n++] = 0x02;                  /* Usage (2)                 */
    d[n++] = 0x75; d[n++] = 0x08;                  /* Report Size (8)           */

    d[n++] = 0x97;                                 /* Report Count, 4-byte data */
    d[n++] = (uint8_t)(count);
    d[n++] = (uint8_t)(count >> 8);
    d[n++] = (uint8_t)(count >> 16);
    d[n++] = (uint8_t)(count >> 24);

    d[n++] = 0x81; d[n++] = 0x02;                  /* Input (Data,Var,Abs)      */
    d[n++] = 0xC0;                                 /* End Collection            */
    return n;
}

int main(void) {
    static hid_interface_t iface;
    uint8_t                desc[64];

    const uint32_t counts[] = {1000000, 5000000, 20000000, 50000000};

    printf("  %14s  %12s  %14s\n", "report count", "seconds", "ns/element");
    printf("  ----------------------------------------------\n");

    double ns_per = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(counts); i++) {
        int len = build(desc, counts[i]);

        memset(&iface, 0, sizeof(iface));
        iface.protocol = HID_PROTOCOL_REPORT;

        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        parse_report_descriptor(&iface, desc, len);
        clock_gettime(CLOCK_MONOTONIC, &b);

        double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
        ns_per   = s * 1e9 / counts[i];
        printf("  %14u  %12.3f  %14.1f\n", counts[i], s, ns_per);
    }

    printf("\n  host x86-64 at ~4 GHz; target is Cortex-M0+ at 120 MHz, in order, no cache\n");
    printf("  watchdog budget is 500 ms = 60,000,000 core cycles\n");
    return 0;
}
