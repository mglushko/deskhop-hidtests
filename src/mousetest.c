/* End to end mouse decode.
 *
 * Parses a pointing device's descriptor, then pushes real reports through
 * extract_report_values(), which tools/lift.py pulls verbatim out of the target's
 * mouse.c. Expected values are worked out by hand from the descriptor, so this
 * fails if either the parser or the extraction changes meaning.
 *
 * The devices, chosen for the shapes they exercise:
 *
 *   gameball_trackball      8-bit fields, no report ID at all
 *   kensington_expert_mouse 12-bit X/Y, and the pointer split across two report
 *                           IDs: buttons/wheel/pan on 1, X/Y on 2
 *   cherry_mw8_mouse        12-bit X/Y in one report, so Y starts at bit 20 and
 *                           never lands on a byte boundary
 *   mx518_mouse             two vendor bytes sitting between the buttons and the
 *                           axes, and the wheel declared before X/Y
 *   kernel_multi_collection two mouse collections in one descriptor, so the
 *                           decode runs against whichever one won
 *
 * cherry_mw8c_mouse runs the Kensington's cases because its pointer section is
 * byte for byte the same; if that ever stops being true, this notices.
 *
 * Each report is copied into an exact-size allocation before being decoded, the
 * same trick truncate.c uses, so ASan's redzone catches a read past the end of
 * the report rather than letting it pass as a plausible number.
 */
#include "main.h"
#include "descriptors.h"

/* mirrors the dispatch in usb.c:tuh_hid_report_received_cb */
static const char *dispatch(hid_interface_t *iface, uint8_t itf_protocol, const uint8_t *report) {
    if (iface->uses_report_id || itf_protocol == HID_ITF_PROTOCOL_NONE) {
        uint8_t report_id = iface->uses_report_id ? report[0] : 0;

        if (report_id < MAX_REPORTS && iface->report_handler[report_id])
            return iface->report_handler[report_id] == process_mouse_report
                       ? "report_handler -> process_mouse_report"
                       : "report_handler -> WRONG RECEIVER";
        return "DROPPED (no handler)";
    }
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD)
        return "process_keyboard_report  <-- WRONG";
    if (itf_protocol == HID_ITF_PROTOCOL_MOUSE)
        return "process_mouse_report (direct)";
    return "DROPPED";
}

typedef struct {
    const char *what;
    uint8_t     report[8];
    int         len;
    int32_t     x, y, wheel, pan, buttons;
} case_t;

typedef struct {
    const char    *name;
    const uint8_t *desc;
    int            desc_len;
    const case_t  *cases;
    unsigned       count;
} device_cases_t;

/* Gameball trackball, mi_00: no report ID, everything 8 bits, one 5-byte report. */
static const case_t gameball_cases[] = {
    {"ball right (X +20)",         {0x00, 0x14, 0x00, 0x00, 0x00}, 5,   20,    0,  0,  0,  0},
    {"ball left  (X -20)",         {0x00, 0xEC, 0x00, 0x00, 0x00}, 5,  -20,    0,  0,  0,  0},
    {"ball down  (Y +20)",         {0x00, 0x00, 0x14, 0x00, 0x00}, 5,    0,   20,  0,  0,  0},
    {"ball up    (Y -20)",         {0x00, 0x00, 0xEC, 0x00, 0x00}, 5,    0,  -20,  0,  0,  0},
    {"ball diagonal up-left",      {0x00, 0xF6, 0xF6, 0x00, 0x00}, 5,  -10,  -10,  0,  0,  0},
    {"ball fast (X +127)",         {0x00, 0x7F, 0x00, 0x00, 0x00}, 5,  127,    0,  0,  0,  0},
    {"ball fast (X -128)",         {0x00, 0x80, 0x00, 0x00, 0x00}, 5, -128,    0,  0,  0,  0},

    {"side pad: scroll up",        {0x00, 0x00, 0x00, 0x01, 0x00}, 5,    0,    0,  1,  0,  0},
    {"side pad: scroll down",      {0x00, 0x00, 0x00, 0xFF, 0x00}, 5,    0,    0, -1,  0,  0},
    {"side pad: scroll down fast", {0x00, 0x00, 0x00, 0xFB, 0x00}, 5,    0,    0, -5,  0,  0},
    {"side pad: pan right",        {0x00, 0x00, 0x00, 0x00, 0x01}, 5,    0,    0,  0,  1,  0},
    {"side pad: pan left",         {0x00, 0x00, 0x00, 0x00, 0xFF}, 5,    0,    0,  0, -1,  0},
    {"both pads at once",          {0x00, 0x00, 0x00, 0x02, 0xFE}, 5,    0,    0,  2, -2,  0},

    {"button 1 (left)",            {0x01, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  1},
    {"button 2 (right)",           {0x02, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  2},
    {"button 3 (middle)",          {0x04, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  4},
    {"button 4",                   {0x08, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0,  8},
    {"button 5",                   {0x10, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0, 16},
    {"all five buttons",           {0x1F, 0x00, 0x00, 0x00, 0x00}, 5,    0,    0,  0,  0, 31},

    {"drag: btn1 + move",          {0x01, 0x0A, 0xF6, 0x00, 0x00}, 5,   10,  -10,  0,  0,  1},
    {"btn3 + scroll",              {0x04, 0x00, 0x00, 0x03, 0x00}, 5,    0,    0,  3,  0,  4},
    {"everything at once",         {0x1F, 0x7F, 0x81, 0x02, 0xFE}, 5,  127, -127,  2, -2, 31},
};

/* Kensington Expert Mouse, mi_00 (issue #218), and byte for byte the same layout
   on the Cherry MW 8C, mi_01 (issue #133).
     report 1: [id][buttons 5 bits + 3 pad][wheel][pan]
     report 2: [id][X 12 bits][Y 12 bits], packed low nibble first:
               byte 1 = X & 0xFF, byte 2 = (X >> 8) | ((Y & 0xF) << 4), byte 3 = Y >> 4
   extract_value() skips a field whose report_id does not match the report in
   hand, so a report 1 leaves X and Y at zero and a report 2 leaves wheel and pan
   at zero. Buttons are the exception: when skipped they fall back to the last
   known state->mouse_buttons, which is zero throughout this test. */
static const case_t kensington_cases[] = {
    {"r1: button 1 (left)",     {0x01, 0x01, 0x00, 0x00}, 4,     0,     0,  0,  0,  1},
    {"r1: button 2 (right)",    {0x01, 0x02, 0x00, 0x00}, 4,     0,     0,  0,  0,  2},
    {"r1: all five buttons",    {0x01, 0x1F, 0x00, 0x00}, 4,     0,     0,  0,  0, 31},
    {"r1: scroll up",           {0x01, 0x00, 0x01, 0x00}, 4,     0,     0,  1,  0,  0},
    {"r1: scroll down",         {0x01, 0x00, 0xFF, 0x00}, 4,     0,     0, -1,  0,  0},
    {"r1: pan right",           {0x01, 0x00, 0x00, 0x01}, 4,     0,     0,  0,  1,  0},
    {"r1: pan left",            {0x01, 0x00, 0x00, 0xFF}, 4,     0,     0,  0, -1,  0},
    {"r1: btn3 + scroll",       {0x01, 0x04, 0x03, 0x00}, 4,     0,     0,  3,  0,  4},

    {"r2: ball right (X +1)",   {0x02, 0x01, 0x00, 0x00}, 4,     1,     0,  0,  0,  0},
    {"r2: ball left  (X -1)",   {0x02, 0xFF, 0x0F, 0x00}, 4,    -1,     0,  0,  0,  0},
    {"r2: ball down  (Y +1)",   {0x02, 0x00, 0x10, 0x00}, 4,     0,     1,  0,  0,  0},
    {"r2: ball up    (Y -1)",   {0x02, 0x00, 0xF0, 0xFF}, 4,     0,    -1,  0,  0,  0},
    {"r2: X +2047 (max)",       {0x02, 0xFF, 0x07, 0x00}, 4,  2047,     0,  0,  0,  0},
    {"r2: X -2047 (min)",       {0x02, 0x01, 0x08, 0x00}, 4, -2047,     0,  0,  0,  0},
    {"r2: Y +2047 (max)",       {0x02, 0x00, 0xF0, 0x7F}, 4,     0,  2047,  0,  0,  0},
    {"r2: Y -2047 (min)",       {0x02, 0x00, 0x10, 0x80}, 4,     0, -2047,  0,  0,  0},
    {"r2: X +100, Y -100",      {0x02, 0x64, 0xC0, 0xF9}, 4,   100,  -100,  0,  0,  0},
    {"r2: X sign bit only",     {0x02, 0x00, 0x08, 0x00}, 4, -2048,     0,  0,  0,  0},
};

/* Cherry MW 8 Advanced, mi_01 (issue #133), the model that works. One report
   carries the lot, so Y begins at bit 20:
     report 3: [id][buttons 5 bits + 3 pad][X 12 bits][Y 12 bits][wheel][pan]
               byte 2 = X & 0xFF, byte 3 = (X >> 8) | ((Y & 0xF) << 4), byte 4 = Y >> 4 */
static const case_t cherry_mw8_cases[] = {
    {"button 1 (left)",      {0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0,  1},
    {"all five buttons",     {0x03, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0, 31},
    {"ball right (X +1)",    {0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 7,     1,     0,    0,    0,  0},
    {"ball left  (X -1)",    {0x03, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00}, 7,    -1,     0,    0,    0,  0},
    {"ball down  (Y +1)",    {0x03, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}, 7,     0,     1,    0,    0,  0},
    {"ball up    (Y -1)",    {0x03, 0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00}, 7,     0,    -1,    0,    0,  0},
    {"X +2047, Y -2047",     {0x03, 0x00, 0xFF, 0x17, 0x80, 0x00, 0x00}, 7,  2047, -2047,    0,    0,  0},
    {"X -2047, Y +2047",     {0x03, 0x00, 0x01, 0xF8, 0x7F, 0x00, 0x00}, 7, -2047,  2047,    0,    0,  0},
    {"scroll up",            {0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 7,     0,     0,    1,    0,  0},
    {"scroll down",          {0x03, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 7,     0,     0,   -1,    0,  0},
    {"pan right",            {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 7,     0,     0,    0,    1,  0},
    {"pan left",             {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 7,     0,     0,    0,   -1,  0},
    {"drag: btn1 + move",    {0x03, 0x01, 0x0A, 0x60, 0xFF, 0x00, 0x00}, 7,    10,   -10,    0,    0,  1},
    {"everything at once",   {0x03, 0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 7,  2047, -2047,  127, -127, 31},
};

/* Logitech MX518. No report ID. The two vendor bytes at 1 and 2 are declared
   inside the mouse collection but belong to no usage the parser tracks, so the
   axes sit further along than a naive reading suggests:
     [buttons 8][vendor][vendor][wheel 8][X 12 bits][Y 12 bits]
   X starts at bit 32 and Y at bit 44, so byte 5 carries the top nibble of X in
   its low half and the bottom nibble of Y in its high half:
     byte 4 = X & 0xFF, byte 5 = (X >> 8) | ((Y & 0xF) << 4), byte 6 = Y >> 4
   This device has no AC Pan, so pan stays 0 throughout. */
static const case_t mx518_cases[] = {
    {"button 1 (left)",       {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0, 0,   1},
    /* -1, not 255: get_report_value sign-extends, and this is the first mouse in
       the corpus with 8 buttons, so it is the first whose button field can set
       bit 7. Harmless on the wire - mouse_report_t.buttons is uint8_t, so the
       low 8 bits ship as 0xFF either way - but it is why the expected value
       here is not the 255 you would write down from the descriptor alone. */
    {"all eight buttons",     {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0, 0,  -1},
    {"move right (X +1)",     {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}, 7,     1,     0,    0, 0,   0},
    {"move left  (X -1)",     {0x00, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x00}, 7,    -1,     0,    0, 0,   0},
    {"move down  (Y +1)",     {0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00}, 7,     0,     1,    0, 0,   0},
    {"move up    (Y -1)",     {0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0xFF}, 7,     0,    -1,    0, 0,   0},
    {"scroll up",             {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 7,     0,     0,    1, 0,   0},
    {"scroll down",           {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00}, 7,     0,     0,   -1, 0,   0},
    {"X +2047, Y -2047",      {0x00, 0x00, 0x00, 0x00, 0xFF, 0x17, 0x80}, 7,  2047, -2047,    0, 0,   0},
    {"X -2047, Y +2047",      {0x00, 0x00, 0x00, 0x00, 0x01, 0xF8, 0x7F}, 7, -2047,  2047,    0, 0,   0},
    {"drag: btn1 + move",     {0x01, 0x00, 0x00, 0x00, 0x0A, 0x60, 0xFF}, 7,    10,   -10,    0, 0,   1},
    /* the point of this one: vendor bytes full of noise must not reach any axis */
    {"vendor bytes ignored",  {0x00, 0xFF, 0xFF, 0x00, 0x01, 0x00, 0x00}, 7,     1,     0,    0, 0,   0},
    {"everything at once",    {0xFF, 0xAA, 0x55, 0x7F, 0xFF, 0x17, 0x80}, 7,  2047, -2047,  127, 0,  -1},
};

/* Kernel docs multi-collection device, decoded against report ID 2 - the second
   mouse collection, which is the one left standing in iface->mouse after the
   parser walks both. Layout after the ID byte:
     [buttons 5 + 3 pad][X 12 bits][Y 12 bits][wheel 8][pan 8]
   The last case feeds report ID 1, the first collection, and expects nothing to
   come out. That is not a typo. extract_value bails when the report's leading ID
   byte does not equal mouse->report_id, and the second collection overwrote
   report_id with 2 as the parser walked past it, so every field of an ID 1
   report fails the check and the values stay zero. usb.c still routes those
   reports here, because report_handler[1] was bound while the first collection
   was being parsed - so they arrive at the mouse path and are silently dropped.
   Both collections happen to declare the same layout, so nothing would have been
   lost by decoding ID 1 with ID 2's offsets; the parser just has no way to do
   that with one mouse_t per interface. */
static const case_t kernel_multi_cases[] = {
    {"button 1 (left)",       {0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0,  1},
    {"all five buttons",      {0x02, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00}, 7,     0,     0,    0,    0, 31},
    {"move right (X +1)",     {0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00}, 7,     1,     0,    0,    0,  0},
    {"move left  (X -1)",     {0x02, 0x00, 0xFF, 0x0F, 0x00, 0x00, 0x00}, 7,    -1,     0,    0,    0,  0},
    {"move down  (Y +1)",     {0x02, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00}, 7,     0,     1,    0,    0,  0},
    {"move up    (Y -1)",     {0x02, 0x00, 0x00, 0xF0, 0xFF, 0x00, 0x00}, 7,     0,    -1,    0,    0,  0},
    {"X +2047, Y -2047",      {0x02, 0x00, 0xFF, 0x17, 0x80, 0x00, 0x00}, 7,  2047, -2047,    0,    0,  0},
    {"scroll up",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00}, 7,     0,     0,    1,    0,  0},
    {"scroll down",           {0x02, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00}, 7,     0,     0,   -1,    0,  0},
    {"pan right",             {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 7,     0,     0,    0,    1,  0},
    {"pan left",              {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, 7,     0,     0,    0,   -1,  0},
    {"everything at once",    {0x02, 0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 7,  2047, -2047,  127, -127, 31},
    {"report 1 dropped",      {0x01, 0x1F, 0xFF, 0x17, 0x80, 0x7F, 0x81}, 7,     0,     0,    0,    0,  0},
};

#define DEV(d, c) {#d, d_##d, (int)sizeof(d_##d), c, (unsigned)ARRAY_SIZE(c)}

static const device_cases_t devices[] = {
    DEV(gameball_trackball, gameball_cases),
    DEV(kensington_expert_mouse, kensington_cases),
    DEV(cherry_mw8c_mouse, kensington_cases),
    DEV(cherry_mw8_mouse, cherry_mw8_cases),
    DEV(mx518_mouse, mx518_cases),
    DEV(kernel_multi_collection, kernel_multi_cases),
};

#undef DEV

static int run_device(const device_cases_t *dev) {
    static hid_interface_t iface;
    device_t state = {0};

    memset(&iface, 0, sizeof(iface));
    iface.protocol = HID_PROTOCOL_REPORT;
    parse_report_descriptor(&iface, dev->desc, dev->desc_len);

    printf("%s (%d bytes)\n", dev->name, dev->desc_len);
    printf("  mouse.is_found = %d, mouse report id = %u\n", iface.mouse.is_found,
           iface.mouse.move_x.report_id);
    printf("  num_keyboards  = %u\n", iface.num_keyboards);
    printf("  uses_report_id = %d\n\n", iface.uses_report_id);

    if (dev->count) {
        printf("  Dispatch (src/usb.c), both ways this interface can present itself:\n");
        printf("    bInterfaceProtocol = MOUSE : %s\n",
               dispatch(&iface, HID_ITF_PROTOCOL_MOUSE, dev->cases[0].report));
        printf("    bInterfaceProtocol = NONE  : %s\n\n",
               dispatch(&iface, HID_ITF_PROTOCOL_NONE, dev->cases[0].report));
    }

    printf("  %-26s %-24s %6s %6s %6s %6s %4s\n", "movement", "raw report", "X", "Y", "wheel",
           "pan", "btn");
    printf("  ");
    for (int i = 0; i < 91; i++)
        printf("-");
    printf("\n");

    int failures = 0;
    for (unsigned i = 0; i < dev->count; i++) {
        const case_t  *c = &dev->cases[i];
        mouse_values_t v = {0};
        char           hex[3 * sizeof(c->report) + 1];
        int            n = 0;

        /* A len past the end of the array would overread the struct here and
           overflow hex[] below, in the one file whose job is catching that. */
        if (c->len < 0 || (size_t)c->len > sizeof(c->report)) {
            printf("  %-26s len %d exceeds report[%zu] - fix the case\n", c->what, c->len,
                   sizeof(c->report));
            failures++;
            continue;
        }

        /* exact-size allocation: an overread lands in ASan's redzone, not in the
           next case's bytes */
        uint8_t *report = malloc(c->len);
        memcpy(report, c->report, c->len);

        extract_report_values(report, c->len, &state, &v, &iface);

        for (int b = 0; b < c->len; b++)
            n += sprintf(hex + n, "%02X ", report[b]);

        free(report);

        int ok = v.move_x == c->x && v.move_y == c->y && v.wheel == c->wheel && v.pan == c->pan &&
                 v.buttons == c->buttons;
        if (!ok)
            failures++;

        printf("  %-26s %-24s %6d %6d %6d %6d %4d   %s\n", c->what, hex, v.move_x, v.move_y,
               v.wheel, v.pan, v.buttons, ok ? "ok" : "MISMATCH");
    }

    printf("\n  %u/%u cases decoded as the descriptor specifies\n\n",
           dev->count - failures, dev->count);
    return failures;
}

int main(void) {
    int failures = 0, total = 0;

    for (unsigned i = 0; i < ARRAY_SIZE(devices); i++) {
        failures += run_device(&devices[i]);
        total += devices[i].count;
    }

    printf("%d/%d cases across %u devices\n", total - failures, total,
           (unsigned)ARRAY_SIZE(devices));
    return failures ? 1 : 0;
}
