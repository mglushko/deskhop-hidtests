#pragma once
#include <stdbool.h>
#include <stdint.h>
bool tud_mounted(void);
bool tud_suspended(void);
bool tud_remote_wakeup(void);
bool tud_hid_ready(void);
bool tud_hid_report(uint8_t id, const void *payload, uint16_t length);
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report, uint16_t length);
void tud_hid_report_fail_cb(uint8_t instance, uint8_t endpoint, uint16_t length);
