#include "pico/stdlib.h"
#include "tusb.h"
#include "control.h"

bool sleepwake_bootsel_pressed(void);

/* Enumerating or resetting USB discards old gestures, including a held button. */
void tud_mount_cb(void) { sleepwake_reset(to_ms_since_boot(get_absolute_time())); }
void tud_umount_cb(void) { sleepwake_reset(to_ms_since_boot(get_absolute_time())); }

int main(void) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
    tusb_init();
    uint32_t last_sample = to_ms_since_boot(get_absolute_time());
    sleepwake_reset(last_sample);
    while (true) {
        tud_task();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((uint32_t)(now - last_sample) < 5)
            continue;
        last_sample = now;
        sleepwake_task(now, sleepwake_bootsel_pressed());
#ifdef PICO_DEFAULT_LED_PIN
        /* Solid: release for Sleep. Fast blink: a report is waiting for the USB host.
           A short flash each two seconds means no USB host has enumerated us. */
        bool on = sleepwake_sleep_armed(now);
        if (!on && sleepwake_busy())
            on = now % 200 < 100;
        if (!tud_mounted())
            on = now % 2000 < 100;
        gpio_put(PICO_DEFAULT_LED_PIN, on);
#endif
    }
}
