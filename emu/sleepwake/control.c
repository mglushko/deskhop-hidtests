/* BOOTSEL gestures and asynchronous USB delivery. No timer-generated keys. */
#include "control.h"
#include "tusb.h"

enum { QUEUE_SIZE = 4, SUSPEND_GUARD_MS = 5 };
typedef struct {
    bool candidate, stable, armed, tracking;
    uint32_t changed_at, pressed_at;
    uint8_t queue[QUEUE_SIZE], head, count, current;
    bool in_flight, was_suspended, wake_attempted;
    uint32_t suspended_at;
} control_state_t;
static control_state_t state;

void sleepwake_reset(uint32_t now) {
    state = (control_state_t){.changed_at = now};
}

bool sleepwake_busy(void) { return state.count != 0; }
uint8_t sleepwake_current_report(void) { return state.current; }

bool sleepwake_sleep_armed(uint32_t now) {
    return state.tracking && state.stable && state.candidate
        && (uint32_t)(now - state.pressed_at) >= BUTTON_HOLD_MS;
}

static void enqueue(uint8_t usage) {
    /* Reserve the release along with its press. A full queue drops the whole gesture. */
    if (state.count > QUEUE_SIZE - 2)
        return;
    state.queue[(state.head + state.count++) % QUEUE_SIZE] = usage;
    state.queue[(state.head + state.count++) % QUEUE_SIZE] = 0;
    if (usage == SYSTEM_WAKE)
        state.wake_attempted = false;
}

static bool wake_is_pending(void) {
    for (uint8_t i = 0; i < state.count; i++)
        if (state.queue[(state.head + i) % QUEUE_SIZE] == SYSTEM_WAKE)
            return true;
    return false;
}

void sleepwake_task(uint32_t now, bool pressed) {
    if (!tud_mounted()) {
        sleepwake_reset(now);
        return;
    }

    bool suspended = tud_suspended();
    if (suspended && !state.was_suspended) {
        state.suspended_at = now;
        state.wake_attempted = false;
    }
    state.was_suspended = suspended;

    if (pressed != state.candidate) {
        state.candidate = pressed;
        state.changed_at = now;
    }
    if ((uint32_t)(now - state.changed_at) >= BUTTON_DEBOUNCE_MS) {
        if (state.candidate != state.stable) {
            state.stable = state.candidate;
            if (state.stable) {
                state.tracking = state.armed;
                state.pressed_at = state.changed_at;
            } else {
                if (state.tracking) {
                    uint8_t usage = (uint32_t)(state.changed_at - state.pressed_at) >= BUTTON_HOLD_MS
                        ? SYSTEM_SLEEP : SYSTEM_WAKE;
                    /* A direct USB host already asleep does not need another Sleep.
                       Behind DeskHop, this USB link can remain awake independently. */
                    if (usage == SYSTEM_WAKE || !suspended)
                        enqueue(usage);
                }
                state.tracking = false;
            }
        }
        /* Require a debounced release after boot or re-enumeration before accepting input. */
        if (!state.stable)
            state.armed = true;
    }

    if (!state.count)
        return;
    if (suspended) {
        /* Only an explicit Wake gesture requests resume. In particular, a release left
           after Sleep must not immediately wake the PC again. Allow USB's 5 ms idle
           interval, then try once; TinyUSB enforces the host's wake permission. A new
           tap can retry, and an external resume can drain the reports either way. */
        if (wake_is_pending() && !state.wake_attempted
            && (uint32_t)(now - state.suspended_at) >= SUSPEND_GUARD_MS) {
            tud_remote_wakeup();
            state.wake_attempted = true;
        }
        return;
    }
    if (!state.in_flight && tud_hid_ready()) {
        uint8_t usage = state.queue[state.head];
        if (tud_hid_report(SLEEPWAKE_REPORT_ID, &usage, sizeof(usage)))
            state.in_flight = true;
    }
}

/* Submission alone does not retire a report. The host must have polled it successfully
   before its release or a later gesture can follow. */
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report, uint16_t length) {
    if (instance != 0 || length != 2 || report[0] != SLEEPWAKE_REPORT_ID
        || !state.in_flight || !state.count || report[1] != state.queue[state.head])
        return;
    state.current = report[1];
    state.head = (state.head + 1) % QUEUE_SIZE;
    state.count--;
    state.in_flight = false;
}

void tud_hid_report_fail_cb(uint8_t instance, uint8_t endpoint, uint16_t length) {
    (void)length;
    if (instance == 0 && endpoint == 0x81)
        state.in_flight = false; /* The head stays queued for another attempt. */
}
