#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SLEEPWAKE_REPORT_ID = 3, SYSTEM_SLEEP = 0x82, SYSTEM_WAKE = 0x83 };
enum { BUTTON_DEBOUNCE_MS = 20, BUTTON_HOLD_MS = 1500 };

void sleepwake_reset(uint32_t now);
void sleepwake_task(uint32_t now, bool pressed);
bool sleepwake_sleep_armed(uint32_t now);
bool sleepwake_busy(void);
uint8_t sleepwake_current_report(void);
