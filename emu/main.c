/* Hardware stand-ins for devices in the deskhop-hidtests corpus.
 *
 * The harness compiles the firmware's own parser and feeds it real bytes, which
 * is enough to say what the code does with a report but never enumerates
 * anything. These builds close that gap for two devices whose bugs were found
 * on the host and had no way to be seen on a desk. CMake builds both.
 *
 *============================================================================
 * EMU_BITDO - 8BitDo Retro Mechanical Keyboard, hrvach/deskhop#57
 *============================================================================
 *
 * Interface 2 declares three keyboard collections on one interface, on report
 * IDs 1 (6KRO), 12 and 10 (both 120-bit NKRO bitmaps). Before the fix all three
 * collapse onto keyboards[0], the NKRO bitmap descriptors overwrite the 6KRO
 * one, and an incoming report ID 1 is walked as if it were a bitmap:
 *
 *     typed by the emulator     6KRO keycodes 04 05 06 07 08 09
 *     fixed firmware            abcdef
 *     broken firmware           gmovw3
 *
 * Measured, not guessed: k_bitdo_cases pins the broken column at
 * {10,16,18,25,26,32} and the fixed at {4,5,6,7,8,9}.
 *
 * The trailing ",./" is the control and also guards the one way this could pass
 * for the wrong reason. extract_kbd_data returns _extract_kbd_boot before the
 * descriptor is consulted, and the 8BitDo's 6KRO layout IS the boot layout, so
 * report ID 1 decodes to abcdef in boot protocol whether or not the fix is in.
 * The control therefore uses usages 54 to 56, which sit in bitmap bytes 6 and 7
 * at wire offsets 8 and 9, past the eight bytes _extract_kbd_boot copies. In
 * boot protocol they vanish and the tail disappears with them.
 *
 *     abcdef,./  and line breaks   fixed
 *     gmovw3,./  and line breaks   broken, the collapse is present
 *     abcdef     no tail, no breaks   boot protocol, this run proves nothing
 *
 *============================================================================
 * EMU_GAMEBALL - Gameball trackball 0782:001B, hrvach/deskhop#332
 *============================================================================
 *
 * Three interfaces, presented in the order the real device does. The bug is not
 * on the interface you watch, which is the whole reason this one has to be a
 * composite device rather than a single descriptor.
 *
 * The gesture interface declares Report Count 0x3FC8, which is 16328, against a
 * 128 entry usages[] array, and in parser_state_t the very next member after
 * usages[] is p_usage, a pointer the parser then dereferences. On a tree without
 * a bound the parse runs off the end of the array and into that pointer. Parsing
 * this descriptor on upstream main aborts under ASan on the host; on an RP2040
 * it corrupts a live pointer instead.
 *
 * Nothing is ever sent on the gesture interface. Enumerating is the entire test,
 * because the damage happens at parse time. What you watch is the trackball, on
 * interface 0, which moves the pointer in a square and works both side scroll
 * pads. If the parse survived, the square is smooth and the scrolling works. If
 * it did not, the board is unlikely to still be forwarding anything.
 *
 * The circle is 32 relative steps whose deltas sum to zero on both axes, so the
 * pointer returns to where it started every revolution rather than walking off
 * toward a screen edge and tripping deskhop's own output switching. The LED is
 * solid while it circles and flickers while it scrolls.
 *
 * The keyboard interface is presented and never used. It declares eight modifier
 * bits and 48 bits of padding and no key array at all, so it could only ever
 * report modifiers, which would look like a stuck Shift. That is the real
 * device's descriptor, not a simplification.
 *
 *============================================================================
 *
 * The onboard LED reports which region a failure is in, so "nothing happened"
 * does not have to cover everything from an unpowered board to a working rig
 * pointed at the wrong PC. See emu/README.md. Hold BOOTSEL while plugging the
 * board in to get back to UF2 mode.
 */
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "emu_desc.h"

/* tusb_init() is a no-op unless TUD_OPT_RHPORT is defined, and that needs
   CFG_TUSB_RHPORT0_MODE rather than CFG_TUD_ENABLED alone. Getting it wrong
   still compiles and still links; the board just never enumerates. */
#if !defined(TUD_OPT_RHPORT)
#  error "TUD_OPT_RHPORT undefined - tusb_init() would not start the device stack"
#endif

#define GRACE_MS      10000  /* time to focus a window after plugging in */

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint32_t reports_sent = 0;

/*==============================================================================
 *  8BitDo: type a line through two of the three keyboard collections
 *============================================================================*/
#if defined(EMU_BITDO)

#define RID_6KRO      0x01   /* modifiers, one reserved byte, six keycodes */
#define RID_NKRO      0x0C   /* modifiers, then a 120-bit usage bitmap     */
#define LEN_6KRO      8
#define LEN_NKRO      16
#define NKRO_BITS     120

#define HOLD_MS       40
#define GAP_MS        60
#define CYCLE_MS      6000

typedef struct {
    uint8_t rid;
    uint8_t usages[6];
    uint8_t count;
} burst_t;

/* One line per cycle. The first burst is the discriminator. The second and third
   go through the NKRO collection and are chosen to be invisible in boot protocol,
   so a run that cannot distinguish fixed from broken says so by losing its tail.
   Keep every NKRO usage at 48 or above for that to hold. */
static const burst_t script[] = {
    {RID_6KRO, {0x04, 0x05, 0x06, 0x07, 0x08, 0x09}, 6},  /* abcdef / gmovw3 */
    {RID_NKRO, {0x36, 0x37, 0x38},                   3},  /* ,./             */
    {RID_NKRO, {0x28},                               1},  /* Enter           */
};

#define SCRIPT_LEN (sizeof(script) / sizeof(script[0]))

static void send_burst(const burst_t *b, bool pressed) {
    if (b->rid == RID_6KRO) {
        /* [0] modifiers, [1] reserved (Input Const), [2..7] keycode array */
        uint8_t p[LEN_6KRO] = {0};

        if (pressed)
            for (uint8_t i = 0; i < b->count && i < 6; i++)
                p[2 + i] = b->usages[i];

        tud_hid_report(RID_6KRO, p, LEN_6KRO);
    } else {
        /* [0] modifiers, [1..15] one bit per usage, LSB first */
        uint8_t p[LEN_NKRO] = {0};

        if (pressed)
            for (uint8_t i = 0; i < b->count; i++) {
                uint8_t u = b->usages[i];
                if (u < NKRO_BITS)
                    p[1 + u / 8] |= (uint8_t)(1u << (u % 8));
            }

        tud_hid_report(RID_NKRO, p, LEN_NKRO);
    }
}

static void device_task(void) {
    static uint32_t next_ms = GRACE_MS;
    static uint8_t  step    = 0;
    static bool     pressed = false;

    /* tud_hid_ready() is false between the report going out and the host
       collecting it, so this naturally paces itself to the endpoint. */
    if (!tud_hid_ready() || now_ms() < next_ms)
        return;

    send_burst(&script[step], !pressed);
    pressed = !pressed;
    reports_sent++;

    if (pressed) {
        next_ms = now_ms() + HOLD_MS;
    } else {
        step = (uint8_t)((step + 1) % SCRIPT_LEN);
        next_ms = now_ms() + (step == 0 ? CYCLE_MS : GAP_MS);
    }

#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, !pressed);
#endif
}

/*==============================================================================
 *  Gameball: draw a square and work both scroll pads, on interface 0
 *============================================================================*/
#elif defined(EMU_GAMEBALL)

#define ITF_TRACKBALL 0
#define LEN_TRACKBALL 5      /* [buttons, X, Y, wheel, pan], no report ID */

#define STEP_MS       25     /* 48 steps at 25 ms is a revolution every 1.2 s */
#define REVOLUTIONS   3      /* circles between scroll bursts */
#define SCROLL_MS     150
#define PAUSE_MS      700

/* A 120 pixel circle, as 48 relative steps.
 *
 * Small on purpose. A big circle needs clearance on all four sides, and a corner
 * is the worst place to start one because two directions clamp at once, which
 * turns the loop into a staircase and loses the movement that would have closed
 * it. 120 pixels needs little enough room that where the pointer starts stops
 * mattering.
 *
 * Step count and rounding pull against each other: too few steps looks like a
 * polygon, too many makes each step's rounding a larger share of it and the speed
 * vary around the loop, which pointer acceleration turns into shape distortion.
 * 48 steps at radius 60 gives 8 pixel sides and 6 percent variation.
 *
 * The deltas are differences between rounded points on the true circle, so the
 * path never leaves it by more than half a pixel, and they sum to zero on both
 * axes, so the pointer returns to where it started every revolution.
 */
#define CIRCLE_STEPS 48
static const int8_t circle[CIRCLE_STEPS][2] = {
    { -1,  8}, { -1,  8}, { -3,  7}, { -3,  7}, { -4,  7}, { -6,  5}, { -5,  6}, { -7,  4},
    { -7,  3}, { -7,  3}, { -8,  1}, { -8,  1}, { -8, -1}, { -8, -1}, { -7, -3}, { -7, -3},
    { -7, -4}, { -5, -6}, { -6, -5}, { -4, -7}, { -3, -7}, { -3, -7}, { -1, -8}, { -1, -8},
    {  1, -8}, {  1, -8}, {  3, -7}, {  3, -7}, {  4, -7}, {  6, -5}, {  5, -6}, {  7, -4},
    {  7, -3}, {  7, -3}, {  8, -1}, {  8, -1}, {  8,  1}, {  8,  1}, {  7,  3}, {  7,  3},
    {  7,  4}, {  5,  6}, {  6,  5}, {  4,  7}, {  3,  7}, {  3,  7}, {  1,  8}, {  1,  8},
};

/* The two side pads, which is the feature #332's reporter asked about. */
static const int8_t scroll[][2] = {   /* {wheel, pan} */
    { 3,  0}, {-3,  0}, { 0,  3}, { 0, -3},
};
#define SCROLL_STEPS (sizeof(scroll) / sizeof(scroll[0]))

static void device_task(void) {
    static uint32_t next_ms   = GRACE_MS;
    static uint8_t  tick      = 0;
    static uint8_t  revs      = 0;
    static uint8_t  scroll_i  = 0;
    static bool     scrolling = false;
    static bool     flicker   = false;

    if (!tud_hid_n_ready(ITF_TRACKBALL) || now_ms() < next_ms)
        return;

    uint8_t p[LEN_TRACKBALL] = {0};

    if (scrolling) {
        p[3] = (uint8_t)scroll[scroll_i][0];
        p[4] = (uint8_t)scroll[scroll_i][1];
    } else {
        p[1] = (uint8_t)circle[tick][0];
        p[2] = (uint8_t)circle[tick][1];
    }

    /* If the endpoint refuses it, leave the state alone and try the same step
       again rather than skipping a delta and deforming the circle. */
    if (!tud_hid_n_report(ITF_TRACKBALL, 0, p, LEN_TRACKBALL))
        return;

    reports_sent++;

    if (scrolling) {
        flicker = !flicker;
        if (++scroll_i >= SCROLL_STEPS) {
            scroll_i  = 0;
            scrolling = false;
            next_ms   = now_ms() + PAUSE_MS;
        } else {
            next_ms = now_ms() + SCROLL_MS;
        }
    } else {
        if (++tick >= CIRCLE_STEPS) {
            tick = 0;
            if (++revs >= REVOLUTIONS) {
                revs      = 0;
                scrolling = true;
            }
        }
        next_ms = now_ms() + STEP_MS;
    }

#ifdef PICO_DEFAULT_LED_PIN
    /* Solid while the pointer should be moving, flickering while the scroll
       pads are being worked, so the two phases are told apart at a glance. */
    gpio_put(PICO_DEFAULT_LED_PIN, scrolling ? flicker : 1);
#endif
}

#elif defined(EMU_POLL)

#define ITF_MOUSE 0
#define LEN_MOUSE 5      /* [buttons, X, Y, wheel, pan], no report ID */

/* Offer a report every single time the endpoint will take one.
 *
 * Every other emulator here paces itself on a timer, because what it is showing is a
 * decode. This one is showing a rate, so pacing it would measure the timer instead of
 * the polling interval. With no gate at all the reports-per-second the host sees IS the
 * polling rate, which is the entire measurement: roughly 100/s at bInterval 10 and
 * roughly 1000/s at bInterval 1, the full speed ceiling.
 *
 * The delta alternates between +1 and -1 on X. Two properties matter and both are
 * needed:
 *
 *   It is never zero. deskhop drops a mouse report carrying no movement and no button
 *   change (process_mouse_report in src/mouse.c), so a report of all zeroes would be
 *   swallowed before it could be counted and every build would measure the same zero.
 *
 *   It sums to zero over each pair. At a thousand reports a second a pointer with any
 *   net drift crosses the screen almost immediately, trips deskhop's edge switching and
 *   takes the measurement with it. Alternating keeps it jittering in place.
 */
static void device_task(void) {
    static bool right = true;

    if (!tud_hid_n_ready(ITF_MOUSE))
        return;

    uint8_t p[LEN_MOUSE] = {0};
    p[1] = right ? 1 : (uint8_t)-1;

    if (!tud_hid_n_report(ITF_MOUSE, 0, p, LEN_MOUSE))
        return;

    right = !right;
    reports_sent++;

#ifdef PICO_DEFAULT_LED_PIN
    /* Far too fast to see as anything but a dim glow, which is itself the signal that
       reports are flowing. Divided down so the pin is not toggled a thousand times a
       second for nothing. */
    gpio_put(PICO_DEFAULT_LED_PIN, (reports_sent & 0x100) != 0);
#endif
}

#else
#  error "define EMU_BITDO, EMU_GAMEBALL or EMU_POLL"
#endif

/*==============================================================================
 *  Shared: LED status and the main loop
 *============================================================================*/

/* The LED blinks a numbered code: N short flashes, then a long dark gap, over and
   over. Count the flashes.
 *
 *     1   not enumerated. The host has not configured the device.
 *     2   enumerated, then suspended by the host.
 *     3   enumerated and awake, but the IN endpoint never became ready.
 *     4   ready and armed, counting out the grace period before the first report.
 *     solid / flickering   sending; see the per-device notes above.
 *
 * Codes 2 and 3 used to look identical, which is what made a stalled rig
 * indistinguishable from a suspended one. Each of the three tests behind them is
 * a separate public call, so the code says which of tud_mounted(),
 * tud_suspended() and tud_hid_ready() is the one that is false rather than
 * leaving it to be inferred.
 *
 * A code that changes on its own means enumeration is cycling: the host is
 * configuring the device, dropping it, and trying again. */
#define FLASH_ON_MS   120
#define FLASH_OFF_MS  200
#define CODE_GAP_MS   1200

static uint8_t led_code(void) {
    if (!tud_mounted())    return 1;
    if (tud_suspended())   return 2;
    if (!tud_hid_ready())  return 3;
    return 4;
}

static void led_task(void) {
#ifdef PICO_DEFAULT_LED_PIN
    static uint32_t next_ms = 0;
    static uint8_t  phase   = 0;
    static uint8_t  latched = 1;
    uint32_t        t       = now_ms();

    if (reports_sent > 0)
        return; /* device_task owns the LED once reports are flowing */

    if (t < next_ms)
        return;

    /* Latch at the start of a group so a code cannot change halfway through and
       be miscounted. */
    if (phase == 0)
        latched = led_code();

    if (phase < (uint8_t)(latched * 2)) {
        bool on = (phase % 2) == 0;
        gpio_put(PICO_DEFAULT_LED_PIN, on);
        next_ms = t + (on ? FLASH_ON_MS : FLASH_OFF_MS);
        phase++;
    } else {
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        next_ms = t + CODE_GAP_MS;
        phase   = 0;
    }
#endif
}

/* deskhop drives keyboard LEDs through SET_REPORT on the control endpoint, so
   caps lock on the host lands here. Nothing to do but accept it. */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)bufsize;
}

/* Returning 0 here stalls the control request. A boot device gets asked for an
   input report during enumeration on some hosts, and a stall is a reason for one
   to stop binding a driver and leave the port suspended, which presents as a
   device that enumerates and then goes quiet. Hand back a zeroed report of the
   right length instead. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id;

    if (report_type != HID_REPORT_TYPE_INPUT)
        return 0;

#if defined(EMU_GAMEBALL)
    uint16_t len = LEN_TRACKBALL;
#elif defined(EMU_POLL)
    uint16_t len = LEN_MOUSE;
#else
    uint16_t len = LEN_6KRO;
#endif
    if (len > reqlen)
        len = reqlen;

    memset(buffer, 0, len);
    return len;
}

int main(void) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    tusb_init();

    while (true) {
        tud_task();
        device_task();
        led_task();
    }
}
