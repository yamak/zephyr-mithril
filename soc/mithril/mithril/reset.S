/*
 * Copyright (c) 2016 Jean-Paul Etienne <fractalclone@gmail.com>
 * Contributors: 2018 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>
#include <offsets.h>

/* exports */
GTEXT(__reset)

/* imports */
GTEXT(z_mithril_prep_c)
GTEXT(riscv_cpu_wake_flag)
GTEXT(riscv_cpu_sp)
GTEXT(arch_secondary_cpu_init)

SECTION_FUNC(reset, __reset)
	/*
	 * jump to __initialize
	 * use call opcode in case __initialize is far away.
	 * This will be dependent on linker.ld configuration.
	 */
	call __mithril_initialize

/* use ABI name of registers for the sake of simplicity */

/*
 * Remainder of asm-land initialization code before we can jump into
 * the C domain
 */
SECTION_FUNC(TEXT, __mithril_initialize)
#ifdef CONFIG_INIT_STACKS
	/* Pre-populate all bytes in z_interrupt_stacks with 0xAA */
	la t0, z_interrupt_stacks
	li t1, __z_interrupt_stack_SIZEOF
	add t1, t1, t0

	/* Populate z_interrupt_stacks with 0xaaaaaaaa */
	li t2, 0xaaaaaaaa
aa_loop:
	sw t2, 0x00(t0)
	addi t0, t0, 4
	blt t0, t1, aa_loop
#endif

	/*
	 * Initially, setup stack pointer to
	 * z_interrupt_stacks + __z_interrupt_stack_SIZEOF
	 */
	la sp, z_interrupt_stacks
	li t0, __z_interrupt_stack_SIZEOF
	add sp, sp, t0

#ifdef CONFIG_WDOG_INIT
	call _WdogInit
#endif

	/*
	 * Jump into C domain. z_prep_c zeroes BSS
	 * and then enters kernel z_cstart
	 */
	call z_mithril_prep_c

loop_unconfigured_cores:
	wfi
	j loop_unconfigured_cores
