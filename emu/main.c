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
 * The square returns to where it started on purpose, so the pointer cannot drift
 * into a screen edge and trip deskhop's own output switching.
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

#define STEP_MS       300
#define CYCLE_MS      3000

typedef struct {
    int8_t x, y, wheel, pan;
} move_t;

static const move_t script[] = {
    /* 40 px square, returning to where it started */
    { 40,   0,  0,  0},
    {  0,  40,  0,  0},
    {-40,   0,  0,  0},
    {  0, -40,  0,  0},
    /* the two side pads, which is the feature #332's reporter asked about */
    {  0,   0,  3,  0},
    {  0,   0, -3,  0},
    {  0,   0,  0,  3},
    {  0,   0,  0, -3},
};

#define SCRIPT_LEN (sizeof(script) / sizeof(script[0]))

static void device_task(void) {
    static uint32_t next_ms = GRACE_MS;
    static uint8_t  step    = 0;

    if (!tud_hid_n_ready(ITF_TRACKBALL) || now_ms() < next_ms)
        return;

    const move_t *m = &script[step];
    uint8_t p[LEN_TRACKBALL] = {
        0, (uint8_t)m->x, (uint8_t)m->y, (uint8_t)m->wheel, (uint8_t)m->pan,
    };

    tud_hid_n_report(ITF_TRACKBALL, 0, p, LEN_TRACKBALL);
    reports_sent++;

    step = (uint8_t)((step + 1) % SCRIPT_LEN);
    next_ms = now_ms() + (step == 0 ? CYCLE_MS : STEP_MS);

#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, step != 0);
#endif
}

#else
#  error "define EMU_BITDO or EMU_GAMEBALL"
#endif

/*==============================================================================
 *  Shared: LED status and the main loop
 *============================================================================*/

/* Patterns are one second read as 16 slots of SLOT_MS, most significant bit
   first, which keeps adding a state to a line of hex rather than a branch.

     one flash a second     powered, but the host never enumerated it
     two flashes a second   enumerated and armed, counting out the grace period
     rapid blinking         enumerated, but the endpoint never goes ready
     activity               sending reports, look downstream of the rig

   Armed and stalled are worth separating. Both happen after enumeration and
   before anything is sent, but one resolves itself within GRACE_MS and the
   other never does. */
#define SLOT_MS 62

typedef enum {
    LED_UNMOUNTED = 0,  /* 1000 0000 0000 0000 */
    LED_ARMED,          /* 1010 0000 0000 0000 */
    LED_STALLED,        /* 1010 1010 1010 1010 */
} led_state_t;

static const uint16_t led_pattern[] = {
    [LED_UNMOUNTED] = 0x8000,
    [LED_ARMED]     = 0xA000,
    [LED_STALLED]   = 0xAAAA,
};

static void led_task(void) {
#ifdef PICO_DEFAULT_LED_PIN
    static uint32_t next_ms = 0;
    static uint8_t  slot    = 0;
    uint32_t        t       = now_ms();

    if (reports_sent > 0)
        return; /* device_task owns the LED once reports are flowing */

    if (t < next_ms)
        return;
    next_ms = t + SLOT_MS;

    /* Readiness only drops momentarily once reports are in flight, and none are
       yet, so before the first one this cleanly separates an endpoint that came
       up from one that did not. */
    led_state_t st = !tud_mounted()   ? LED_UNMOUNTED
                     : !tud_hid_ready() ? LED_STALLED
                                        : LED_ARMED;

    gpio_put(PICO_DEFAULT_LED_PIN, (led_pattern[st] >> (15 - slot)) & 1u);
    slot = (uint8_t)((slot + 1) & 15);
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

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer;   (void)reqlen;
    return 0;
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
