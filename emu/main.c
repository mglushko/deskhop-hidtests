/* 8BitDo Retro Mechanical Keyboard emulator: an on-hardware test for the
 * keyboard collection collapse.
 *
 * WHAT IT PROVES
 *
 * The 8BitDo's interface declares three keyboard collections on one interface,
 * on report IDs 1 (6KRO), 12 and 10 (both 120-bit NKRO bitmaps). Before the fix
 * all three collapse onto keyboards[0], so the NKRO bitmap descriptors overwrite
 * the 6KRO one and an incoming report ID 1 is walked as if it were a bitmap.
 *
 * That turns the six-key burst below into a completely different set of keys,
 * which is what makes this a hardware test anyone can read off a screen:
 *
 *     typed by the emulator     6KRO keycodes 04 05 06 07 08 09
 *     fixed firmware            abcdef
 *     broken firmware           gmovw3
 *
 * Those are not guesses. They are the measured expectations pinned in
 * src/cases_kbd.h (k_bitdo_cases), where the broken column reads
 * {10,16,18,25,26,32} and the fixed column {4,5,6,7,8,9}. Byte 0x04 at payload
 * offset 2 lands on bit 2 of the bitmap's second byte, so usage 8+2 = 10 = 'g',
 * and so on up the report.
 *
 * The trailing ",./" is typed through the NKRO collection on report ID 12. It is
 * the positive control, and it also guards against the one way this test could
 * pass for the wrong reason.
 *
 * That way is boot protocol. extract_kbd_data returns _extract_kbd_boot before it
 * ever consults the descriptor, and the 8BitDo's 6KRO layout IS the boot layout,
 * so a 9-byte report ID 1 decodes to abcdef in boot protocol whether the fix is
 * present or not. deskhop only reaches that state if it was built with
 * ENFORCE_KEYBOARD_BOOT_PROTOCOL 1 and the rig is on board A, but a test that
 * cannot tell the difference is not worth running.
 *
 * So the control uses usages 54, 55 and 56 rather than letters. Those sit in
 * bitmap bytes 6 and 7, which land at wire offsets 8 and 9 - outside the eight
 * bytes _extract_kbd_boot copies. In boot protocol they contribute no keycodes at
 * all, so the tail disappears. Usage 40 on the same collection supplies the line
 * break and vanishes the same way.
 *
 *     abcdef,./  and line breaks   fixed
 *     gmovw3,./  and line breaks   broken, the collapse is present
 *     abcdef     no tail, no line breaks   boot protocol, this run proves nothing
 *     nothing                      the rig is not reaching the PC
 *
 * This types on a timer with no user input, so open a text editor and leave it
 * focused. Hold BOOTSEL while plugging the board in to get back to UF2 mode.
 */
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "bitdo_desc.h"

#define RID_6KRO      0x01   /* modifiers, one reserved byte, six keycodes */
#define RID_NKRO      0x0C   /* modifiers, then a 120-bit usage bitmap     */

#define LEN_6KRO      8
#define LEN_NKRO      16
#define NKRO_BITS     120

#define GRACE_MS      10000  /* time to focus a text editor after plugging in */
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

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

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

static void typing_task(void) {
    static uint32_t next_ms = GRACE_MS;
    static uint8_t  step    = 0;
    static bool     pressed = false;

    /* tud_hid_ready() is false between the report going out and the host
       collecting it, so this naturally paces itself to the endpoint. */
    if (!tud_hid_ready() || now_ms() < next_ms)
        return;

    send_burst(&script[step], !pressed);
    pressed = !pressed;

    if (pressed) {
        next_ms = now_ms() + HOLD_MS;
    } else {
        step = (uint8_t)((step + 1) % SCRIPT_LEN);
        next_ms = now_ms() + (step == 0 ? CYCLE_MS : GAP_MS);
    }

#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, pressed);
#endif
}

/* deskhop drives keyboard LEDs through SET_REPORT on the control endpoint,
   so caps lock on the host lands here. Nothing to do but accept it. */
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
        typing_task();
    }
}
