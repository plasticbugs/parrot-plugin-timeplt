// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// tpz80.c -- the board's two Z80s, the main CPU (3.072 MHz) and the sound
// CPU (1.789772 MHz): z80.c built once for both (tpz80.h). Each has its
// page tables here and its hooks in tp.c; the core calls the hooks of the
// CPU it is running.
#include "tpz80.h"

const uint8_t *tpm_rd_page[256], *tps_rd_page[256];
uint8_t *tpm_wr_page[256], *tps_wr_page[256];
void (*tpm_trace)(void), (*tps_trace)(void);

#define Z80_CPUS 2
#define z80_rd_pages(c)    ((c) ? tps_rd_page : tpm_rd_page)
#define z80_wr_pages(c)    ((c) ? tps_wr_page : tpm_wr_page)
static const z80_hooks_t hooks[2] = {
	{tpm_hook_rd, tpm_hook_wr, tpm_hook_in, tpm_hook_out, tpm_hook_irq_ack},
	{tps_hook_rd, tps_hook_wr, tps_hook_in, tps_hook_out, tps_hook_irq_ack},
};
#define z80_hooks(c)       (&hooks[c])
#define z80_trace(c)       ((c) ? tps_trace : tpm_trace)
#include "z80.c"
