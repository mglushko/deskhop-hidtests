/* p_usage only ever moves forward across a whole descriptor, so usages[] can be
   used up before the parser reaches the collection we actually care about.
 *
 * Puts N vendor usages in front of a known good boot mouse and asks whether the
 * mouse still resolves. On a parser that bounds accesses but never resets the
 * cursor, X and Y stop being identified past roughly 126 usages: the device
 * enumerates but the pointer never moves. On one that restarts the usage list per
 * main item, the offsets stay correct however long the prefix gets.
 */
#include "main.h"
#include "descriptors.h"

/* n vendor usages spread over main items, then the boot mouse verbatim */
static int build(uint8_t *d, int n) {
    int p = 0;

    d[p++] = 0x06; d[p++] = 0xE0; d[p++] = 0xFF;   /* Usage Page (vendor)      */
    d[p++] = 0x09; d[p++] = 0x01;                  /* Usage (1)                */
    d[p++] = 0xA1; d[p++] = 0x01;                  /* Collection (Application) */

    for (int i = 0; i < n; i++) {
        d[p++] = 0x09; d[p++] = (uint8_t)(0x10 + (i & 0x3F)); /* Usage        */
        d[p++] = 0x75; d[p++] = 0x08;                         /* Report Size  */
        d[p++] = 0x95; d[p++] = 0x01;                         /* Report Count */
        d[p++] = 0x81; d[p++] = 0x02;                         /* Input        */
    }

    d[p++] = 0xC0;                                 /* End Collection, no data  */

    memcpy(d + p, d_boot_mouse, sizeof(d_boot_mouse));
    p += (int)sizeof(d_boot_mouse);
    return p;
}

int main(void) {
    static hid_interface_t iface;
    static uint8_t         desc[16384];

    const int counts[] = {0, 8, 32, 64, 120, 126, 127, 128, 130, 200, 500};

    printf("  %10s  %8s  %10s  %8s  %8s\n", "preceding", "desc", "mouse", "X off", "Y off");
    printf("  %10s  %8s  %10s  %8s  %8s\n", "usages", "bytes", "found?", "", "");
    printf("  --------------------------------------------------------\n");

    for (unsigned i = 0; i < ARRAY_SIZE(counts); i++) {
        int len = build(desc, counts[i]);

        memset(&iface, 0, sizeof(iface));
        iface.protocol = HID_PROTOCOL_REPORT;
        parse_report_descriptor(&iface, desc, len);

        printf("  %10d  %8d  %10s  %8u  %8u\n", counts[i], len,
               iface.mouse.is_found ? "yes" : "NO", iface.mouse.move_x.offset,
               iface.mouse.move_y.offset);
    }

    printf("\n  X/Y offsets dropping to 0 means the usage array ran out before the mouse\n");
    return 0;
}
