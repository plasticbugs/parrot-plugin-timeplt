// The simulator's stand-in for the pico-sdk's watchdog: there is none on
// the desktop, so a plugin that feeds it carries on (sim/extra.c).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void watchdog_enable(uint32_t delay_ms, bool pause_on_debug);
void watchdog_update(void);
bool watchdog_caused_reboot(void);
#ifdef __cplusplus
}
#endif
