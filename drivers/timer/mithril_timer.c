/*
 * Copyright (c) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>

#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/sys_clock.h>
#include <zephyr/spinlock.h>
#include <zephyr/irq.h>
#include <mithril.h>
#include <riscv_timer.h>

#define DT_DRV_COMPAT mithril_timer0

#define CYC_PER_TICK (uint32_t)(sys_clock_hw_cycles_per_sec() \
				/ CONFIG_SYS_CLOCK_TICKS_PER_SEC)

/* the unsigned long cast limits divisions to native CPU register width */
#define cycle_diff_t unsigned long

static struct k_spinlock lock;
static uint64_t last_count;
static uint64_t last_ticks;
static uint32_t last_elapsed;

#if defined(CONFIG_TEST)
const int32_t z_sys_timer_irq_for_test = TIMER_IRQN;
#endif



static void set_mtimecmp(uint64_t time)
{
	RV_Timer_CmpUpdate(time)
}

static void set_divider(void)
{

}

static uint64_t mtime(void)
{
	return RV_Timer_Read();
}

static void timer_isr(const void *arg)
{
	ARG_UNUSED(arg);

	k_spinlock_key_t key = k_spin_lock(&lock);

	uint64_t now = mtime();
	uint64_t dcycles = now - last_count;
	uint32_t dticks = (cycle_diff_t)dcycles / CYC_PER_TICK;

	last_count += (cycle_diff_t)dticks * CYC_PER_TICK;
	last_ticks += dticks;
	last_elapsed = 0;

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		uint64_t next = last_count + CYC_PER_TICK;

		set_mtimecmp(next);
	}

	k_spin_unlock(&lock, key);
	sys_clock_announce(dticks);
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(idle);

	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return;
	}

	if (ticks == K_TICKS_FOREVER) {
		set_mtimecmp(UINT64_MAX);
		return;
	}

	/*
	 * Clamp the max period length to a number of cycles that can fit
	 * in half the range of a cycle_diff_t for native width divisions
	 * to be usable elsewhere. Also clamp it to half the range of an
	 * int32_t as this is the type used for elapsed tick announcements.
	 * The half range gives us extra room to cope with the unavoidable IRQ
	 * servicing latency. The compiler should optimize away the least
	 * restrictive of those tests automatically.
	 */
	ticks = CLAMP(ticks, 0, (cycle_diff_t)-1 / 2 / CYC_PER_TICK);
	ticks = CLAMP(ticks, 0, INT32_MAX / 2);

	k_spinlock_key_t key = k_spin_lock(&lock);
	uint64_t cyc = (last_ticks + last_elapsed + ticks) * CYC_PER_TICK;

	set_mtimecmp(cyc);
	k_spin_unlock(&lock, key);
}

uint32_t sys_clock_elapsed(void)
{
	if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		return 0;
	}

	k_spinlock_key_t key = k_spin_lock(&lock);
	uint64_t now = mtime();
	uint64_t dcycles = now - last_count;
	uint32_t dticks = (cycle_diff_t)dcycles / CYC_PER_TICK;

	last_elapsed = dticks;
	k_spin_unlock(&lock, key);
	return dticks;
}

uint32_t sys_clock_cycle_get_32(void)
{
	return ((uint32_t)mtime()) << CONFIG_RISCV_MACHINE_TIMER_SYSTEM_CLOCK_DIVIDER;
}

uint64_t sys_clock_cycle_get_64(void)
{
	return mtime() << CONFIG_RISCV_MACHINE_TIMER_SYSTEM_CLOCK_DIVIDER;
}

static int sys_clock_driver_init(void)
{

	set_divider();

	IRQ_CONNECT(TIMER_IRQN, 0, timer_isr, NULL, 0);
	last_ticks = mtime() / CYC_PER_TICK;
	last_count = last_ticks * CYC_PER_TICK;
	set_mtimecmp(last_count + CYC_PER_TICK);
	irq_enable(TIMER_IRQN);
	return 0;
}

#ifdef CONFIG_SMP
void smp_timer_init(void)
{
	set_mtimecmp(last_count + CYC_PER_TICK);
	irq_enable(TIMER_IRQN);
}
#endif

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2,
	 CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
