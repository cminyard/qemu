/*
 * Internal execution defines for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_INTERNAL_H
#define ACCEL_TCG_INTERNAL_H

#include "exec/exec-all.h"

struct tb_desc {
    CPUState *cpu;
    target_ulong pc;
    target_ulong cs_base;
    CPUArchState *env;
    tb_page_addr_t phys_pc;
    tb_page_addr_t page_addr[2];
    uint32_t flags;
    uint32_t cflags;
    uint32_t trace_vcpu_dstate;
};

TranslationBlock *tb_gen_code(struct tb_desc *desc);

void QEMU_NORETURN cpu_io_recompile(CPUState *cpu, uintptr_t retaddr);
void page_init(void);
void tb_htable_init(void);

#endif /* ACCEL_TCG_INTERNAL_H */
