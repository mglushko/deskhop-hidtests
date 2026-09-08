/* Exercises the firmware's gesture and delivery code with independent USB state.
   Completion is explicit: accepting a transfer does not mean the host read it. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "tusb.h"

static bool mounted, suspended, ready, accepts, wake_allowed, pressed, in_flight;
static uint32_t now;
static uint8_t flight[2], received[32];
static unsigned count, attempts, wake_calls, wake_signals, checks;

static void check(const char *name, bool ok) {
    printf("  %s  %s\n", ok ? "PASS" : "FAIL", name);
    checks++;
    assert(ok);
}

bool tud_mounted(void) { return mounted; }
bool tud_suspended(void) { return suspended; }
bool tud_hid_ready(void) { return mounted && !suspended && ready && !in_flight; }
bool tud_remote_wakeup(void) {
    wake_calls++;
    if (suspended && wake_allowed) {
        wake_signals++;
        return true;
    }
    return false;
}
bool tud_hid_report(uint8_t id, const void *payload, uint16_t length) {
    assert(id == SLEEPWAKE_REPORT_ID && length == 1 && tud_hid_ready());
    attempts++;
    if (!accepts)
        return false;
    flight[0] = id;
    flight[1] = *(const uint8_t *)payload;
    in_flight = true;
    return true;
}

static void tick(uint32_t elapsed, bool down) {
    now += elapsed;
    pressed = down;
    sleepwake_task(now, pressed);
}

static void complete(void) {
    assert(in_flight && count < sizeof(received));
    received[count++] = flight[1];
    in_flight = false;
    tud_hid_report_complete_cb(0, flight, sizeof(flight));
}

static void drain(void) {
    unsigned limit = 0;
    while (sleepwake_busy()) {
        assert(++limit < 16);
        tick(5, pressed);
        if (in_flight)
            complete();
    }
}

static void reset(uint32_t start) {
    mounted = ready = accepts = wake_allowed = true;
    suspended = pressed = in_flight = false;
    count = attempts = wake_calls = wake_signals = 0;
    memset(received, 0, sizeof(received));
    now = start;
    sleepwake_reset(now);
    tick(BUTTON_DEBOUNCE_MS, false);
}

static void gesture(uint32_t held) {
    assert(held >= BUTTON_DEBOUNCE_MS);
    tick(5, true);
    tick(BUTTON_DEBOUNCE_MS, true);
    tick(held - BUTTON_DEBOUNCE_MS, false);
    tick(BUTTON_DEBOUNCE_MS, false);
}

int main(void) {
    reset(0);
    tick(10000, false);
    check("idle never sends keys or wake requests", !attempts && !wake_calls);
    gesture(100);
    check("tap submits Wake", in_flight && flight[1] == SYSTEM_WAKE);
    tick(100, false);
    check("submission waits for host completion", attempts == 1 && sleepwake_busy());
    complete();
    check("GET_REPORT state records the delivered press", sleepwake_current_report() == SYSTEM_WAKE);
    drain();
    check("tap delivers exactly Wake and release", count == 2 && received[0] == SYSTEM_WAKE && received[1] == 0);
    check("release clears GET_REPORT state", sleepwake_current_report() == 0);
    check("awake host receives no remote wake request", wake_calls == 0);

    reset(0);
    tick(5, true);
    tick(BUTTON_DEBOUNCE_MS, true);
    tick(BUTTON_HOLD_MS - BUTTON_DEBOUNCE_MS - 1, true);
    check("Sleep indication waits for threshold", !sleepwake_sleep_armed(now));
    tick(1, true);
    check("Sleep indication lights at threshold", sleepwake_sleep_armed(now));
    tick(5000, true);
    check("holding BOOTSEL does not send or repeat", attempts == 0);
    tick(5, false);
    tick(BUTTON_DEBOUNCE_MS, false);
    drain();
    check("long hold delivers exactly Sleep and release", count == 2 && received[0] == SYSTEM_SLEEP && received[1] == 0);
    check("Sleep indication clears on release", !sleepwake_sleep_armed(now));
    gesture(100);
    drain();
    check("next gesture chooses its own command", count == 4 && received[2] == SYSTEM_WAKE && received[3] == 0);

    for (unsigned boundary = 0; boundary < 2; boundary++) {
        reset(UINT32_MAX - 100);
        gesture(BUTTON_HOLD_MS - 1 + boundary);
        drain();
        check(boundary ? "Sleep threshold survives timer wrap" : "short press survives timer wrap",
              count == 2 && received[0] == (boundary ? SYSTEM_SLEEP : SYSTEM_WAKE));
    }

    reset(0);
    tick(1, true);
    tick(5, false);
    tick(5, true);
    tick(5, false);
    tick(100, false);
    check("button bounce emits no gesture", attempts == 0);
    tick(1, true);
    tick(20, true);
    tick(5, false);
    tick(5, true);
    tick(5, false);
    tick(20, false);
    drain();
    check("release bounce produces one pair", count == 2);

    reset(0);
    sleepwake_reset(now);
    tick(0, true);
    tick(2000, true);
    tick(1, false);
    tick(20, false);
    check("button held at boot must be released before arming", attempts == 0);
    gesture(100);
    drain();
    check("next press after boot is accepted", count == 2);

    reset(0);
    ready = false;
    gesture(100);
    check("busy endpoint retains the pair", attempts == 0 && sleepwake_busy());
    ready = true;
    accepts = false;
    tick(5, false);
    check("failed press submission retains the pair", attempts == 1 && !count && sleepwake_busy());
    accepts = true;
    tick(5, false);
    const uint8_t wrong[2] = {SLEEPWAKE_REPORT_ID, SYSTEM_SLEEP};
    tud_hid_report_complete_cb(0, wrong, sizeof(wrong));
    check("unrelated completion does not retire the press", sleepwake_current_report() == 0 && sleepwake_busy());
    complete();
    accepts = false;
    tick(5, false);
    check("failed release submission retains the release", count == 1 && sleepwake_busy());
    accepts = true;
    drain();
    check("retries preserve exactly one press and release", count == 2 && received[0] == SYSTEM_WAKE && received[1] == 0);

    reset(0);
    gesture(100);
    in_flight = false;
    tud_hid_report_fail_cb(0, 0x81, 0);
    tick(5, false);
    check("failed IN transfer retries the original press", attempts == 2 && flight[1] == SYSTEM_WAKE && !count);
    complete();
    tick(5, false);
    in_flight = false;
    tud_hid_report_fail_cb(0, 0x81, 0);
    drain();
    check("failed IN release transfer is retried without a duplicate press", count == 2 && received[1] == 0);

    reset(0);
    ready = false;
    gesture(100);
    gesture(2000);
    gesture(100);
    ready = true;
    drain();
    check("full queue drops the whole extra gesture", count == 4);
    check("queued gestures retain FIFO order and both releases",
          received[0] == SYSTEM_WAKE && received[1] == 0 && received[2] == SYSTEM_SLEEP && received[3] == 0);

    reset(0);
    suspended = true;
    tick(100, false);
    gesture(2000);
    check("Sleep while directly suspended does not wake or queue Sleep", wake_calls == 0 && !sleepwake_busy());
    gesture(100);
    check("Wake requests resume without sending into suspension", wake_calls == 1 && wake_signals == 1 && !attempts);
    tick(100, false);
    check("pending Wake does not spam resume requests", wake_calls == 1 && sleepwake_busy());
    suspended = false;
    drain();
    check("resume delivers queued Wake then release", count == 2 && received[0] == SYSTEM_WAKE && received[1] == 0);

    reset(0);
    suspended = true;
    wake_allowed = false;
    gesture(100);
    check("host-denied wakeup emits no signal and retains input", wake_calls == 1 && !wake_signals && !count && sleepwake_busy());
    suspended = false;
    drain();
    check("external resume drains retained input", count == 2);

    reset(0);
    ready = false;
    gesture(100);
    suspended = true;
    tick(0, false);
    check("new suspension waits for the USB idle interval", wake_calls == 0);
    tick(4, false);
    check("less than five milliseconds is too early to wake", wake_calls == 0);
    tick(1, false);
    check("pending Wake signals after idle interval", wake_calls == 1);

    reset(0);
    gesture(2000);
    complete(); /* The PC sees Sleep and suspends before polling the release. */
    suspended = true;
    tick(100, false);
    tick(100, false);
    check("a pending Sleep release never wakes the PC", !wake_calls && count == 1 && sleepwake_busy());
    gesture(100);
    check("BOOTSEL can wake with the previous release still pending", wake_calls == 1 && wake_signals == 1);
    suspended = false;
    drain();
    check("resume sends old release, new Wake, new release",
          count == 4 && received[0] == SYSTEM_SLEEP && received[1] == 0 && received[2] == SYSTEM_WAKE && received[3] == 0);

    reset(0);
    gesture(100);
    mounted = false;
    in_flight = false; /* USB reset aborts outstanding transfers. */
    tick(5, false);
    check("disconnect clears pending keys and current report", !sleepwake_busy() && sleepwake_current_report() == 0);
    gesture(2000);
    mounted = true;
    tick(20, false);
    check("reconnection does not replay offline gestures", count == 0 && !sleepwake_busy());
    sleepwake_reset(now); /* Also models a USB reset followed by mount without an offline poll. */
    tick(0, true);
    tick(2000, true);
    tick(5, false);
    tick(20, false);
    check("held button during re-enumeration does not generate Sleep", !sleepwake_busy());
    gesture(100);
    drain();
    check("a fresh gesture after reconnect works", count == 2);

    printf("\n%u BOOTSEL control checks passed\n", checks);
    return 0;
}
