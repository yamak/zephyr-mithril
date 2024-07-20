/*
 * Copyright (c) 2018 - 2021 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mithril_intc

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/types.h>
#include <zephyr/arch/riscv/irq.h>


static inline uint32_t mithril_irq_pending(void)
{
	uint32_t pending;

	__asm__ volatile ("csrr %0, mip" : "=r"(pending));
	return pending;
}

static inline void mithril_irq_setie(uint32_t ie)
{
	if (ie) {
		__asm__ volatile ("csrrs x0, mstatus, %0"
				:: "r"(MSTATUS_IEN));
	} else {
		__asm__ volatile ("csrrc x0, mstatus, %0"
				:: "r"(MSTATUS_IEN));
	}
}

void arch_irq_enable(unsigned int irq)
{
	__asm__ volatile("csrs mie, %0\n" : : "r"(1 << irq));
}

void arch_irq_disable(unsigned int irq)
{
	__asm__ volatile("csrc  mie, %0\n" : : "r"(1<<irq));
}

int arch_irq_is_enabled(unsigned int irq)
{
	uint32_t enabled_irqs;
	__asm__ volatile ("csrr %0, mie" : "=r"(enabled_irqs));
	return enabled_irqs & (1 << irq);
}

static int mithril_irq_init(const struct device *dev)
{
	mithril_irq_setie(1);
	return 0;
}

DEVICE_DT_INST_DEFINE(0, mithril_irq_init, NULL, NULL, NULL,
		      PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY, NULL);
