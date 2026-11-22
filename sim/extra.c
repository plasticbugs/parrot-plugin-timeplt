// extra.c -- SDK calls that test/host/host.c doesn't answer (its examples
// don't make them) but real plugins do: the NES plugin's heap and
// watchdog, the card carried on from the menu, the frame kernel's way back
// to the menu after a fault. Candidates to move into host.c.
#include "sim.h"

#include <stdbool.h>
#include <stdint.h>

#include "cvdiag.h"
#include "cvframes.h"
#include "cvheap.h"
#include "cvsd.h"
#include "cvxip.h"

// the desktop's malloc has room; this is about what the cart's heap has
// once a plugin of a few hundred K is loaded (the cart says nothing like it:
// docs/simulator.md, "What the simulator can't show you")
uint32_t cvheap_free(void) { return 300 * 1024; }
uint32_t cvheap_largest(void) { return 300 * 1024; }
uint32_t cvheap_reserve(void) { return 300 * 1024; }

void watchdog_enable(uint32_t delay_ms, bool pause_on_debug) { (void)delay_ms; (void)pause_on_debug; }
void watchdog_update(void) {}
bool watchdog_caused_reboot(void) { return false; }

bool cvsd_resume(void) { return cvsd_init(); }   // as a card the menu had started

void cvd_stack(void *base, uint32_t bytes) { (void)base; (void)bytes; }
uint32_t cvd_stack_used(void) { return 0; }

bool cvxip_fast(void) { return true; }

void cvf_menu_now(void) {
    model_end();
    for (;;) {}
}
