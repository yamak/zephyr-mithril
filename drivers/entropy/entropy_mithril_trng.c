/*
 * Copyright (c) 2025 Yusuf Yamak <yamakyusuf@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Mithril TRNG driver implements a dual-pool entropy management system
 * to efficiently handle both thread and ISR entropy requests.
 *
 * Pool Structure:
 * - ISR Pool: Dedicated for interrupt context entropy requests
 * - Thread Pool: Services thread context requests
 *
 * The driver employs a threshold-based water level system:
 * - When a pool's available entropy falls below its threshold, TRNG is enabled
 * - When both pools are full, TRNG is automatically disabled to save power
 *
 * Data Flow:
 * 1. TRNG generates random data and triggers an interrupt
 * 2. ISR reads the data and attempts to fill the ISR pool first
 * 3. If ISR pool is full, data goes to Thread pool
 * 4. If both pools are full, TRNG is disabled until more space is available
 *
 * The driver supports two modes of operation:
 * 1. Normal Mode (entropy_mithril_get_entropy):
 *    - Uses Thread pool with semaphore-based synchronization
 *    - Blocks until requested amount of entropy is available
 *
 * 2. ISR Mode (entropy_mithril_get_entropy_isr):
 *    - Uses ISR pool for non-blocking requests
 *    - Supports busy-wait mode for guaranteed entropy generation
 *
 * Power Efficiency:
 * - TRNG is automatically managed based on pool levels
 * - Only runs when entropy levels are below thresholds
 * - Completely stops when both pools are full
 *
 * Thread Safety:
 * - Ring buffer operations are protected with IRQ locks
 * - Separate pools prevent contention between ISR and thread contexts
 * - Semaphores manage concurrent access to Thread pool
 */

#define DT_DRV_COMPAT mithril_trng

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/irq.h>
#include <zephyr/sys/ring_buffer.h>
#include "mithril_trng.h"

#define IRQN                   DT_INST_IRQN(0)
#define IRQ_PRIO               DT_INST_IRQ(0, priority)
#define MITHRIL_TRNG_DATA_SIZE 4

struct entropy_pool {
	struct ring_buf pool;
	uint8_t threshold;
};

struct entropy_mithril_dev_data {
	struct k_sem sem_lock;
	struct k_sem sem_sync;
	struct entropy_pool thr_pool;
	struct entropy_pool isr_pool;
	uint8_t thr_buffer[CONFIG_ENTROPY_MITHRIL_TRNG_THR_POOL_SIZE];
	uint8_t isr_buffer[CONFIG_ENTROPY_MITHRIL_TRNG_ISR_POOL_SIZE];
};

static struct entropy_mithril_dev_data entropy_mithril_data;

// Check CONFIG_ENTROPY_MITHRIL_TRNG_ISR_POOL_SIZE and CONFIG_ENTROPY_MITHRIL_TRNG_THR_POOL_SIZE
// to see if they are power of 2
BUILD_ASSERT((CONFIG_ENTROPY_MITHRIL_TRNG_ISR_POOL_SIZE & 
	(CONFIG_ENTROPY_MITHRIL_TRNG_ISR_POOL_SIZE - 1)) == 0,
	     "CONFIG_ENTROPY_MITHRIL_TRNG_ISR_POOL_SIZE must be a power of 2");
BUILD_ASSERT((CONFIG_ENTROPY_MITHRIL_TRNG_THR_POOL_SIZE & 
(CONFIG_ENTROPY_MITHRIL_TRNG_THR_POOL_SIZE - 1)) == 0,
	     "CONFIG_ENTROPY_MITHRIL_TRNG_THR_POOL_SIZE must be a power of 2");

static uint8_t read_trng_data_register(uint8_t *buf, uint8_t len)
{
	uint32_t entropy_word = mithril_trng_get_data();
	uint8_t cnt = len;
	for (int i = 0; i < 4 && cnt > 0; i++) {
		buf[--cnt] = entropy_word & 0xFF;
		entropy_word >>= 8;
	}
	return cnt;
}

static uint32_t put_entropy_to_pool(struct entropy_pool *rng_pool, uint8_t *buf, uint32_t len)
{
	unsigned int key = irq_lock();
	uint32_t written_cnt = ring_buf_put(&rng_pool->pool, buf, len);
	irq_unlock(key);
	return written_cnt;
}

static uint32_t get_entropy_from_pool(struct entropy_pool *rng_pool, uint8_t *buf, uint32_t len)
{
	unsigned int key = irq_lock();
	uint32_t read_cnt = ring_buf_get(&rng_pool->pool, buf, len);
	uint32_t available = ring_buf_size_get(&rng_pool->pool);
	irq_unlock(key);
	if (available <= rng_pool->threshold) {
		mithril_trng_enable(true);
	}
	return read_cnt;
}

static void isr(const void *arg)
{
	ARG_UNUSED(arg);
	uint8_t buf[MITHRIL_TRNG_DATA_SIZE];
	read_trng_data_register(buf, MITHRIL_TRNG_DATA_SIZE);
	uint32_t written_cnt =
		put_entropy_to_pool(&entropy_mithril_data.isr_pool, buf, MITHRIL_TRNG_DATA_SIZE);
	if (written_cnt < MITHRIL_TRNG_DATA_SIZE) {
		written_cnt = put_entropy_to_pool(&entropy_mithril_data.thr_pool, buf + written_cnt,
						  MITHRIL_TRNG_DATA_SIZE - written_cnt);
		// Both pools are full. Disable TRNG
		if (written_cnt == 0) {
			mithril_trng_enable(false);
		}
	}
	mithril_trng_clear_flag(TRNG_FLAG_READY);
	k_sem_give(&entropy_mithril_data.sem_sync);
}

static int entropy_mithril_get_entropy(const struct device *dev, uint8_t *buf, uint16_t len)
{
	while(len)
	{
		k_sem_take(&entropy_mithril_data.sem_lock, K_FOREVER);
		uint32_t bytes = get_entropy_from_pool(&entropy_mithril_data.thr_pool, buf, len);
		k_sem_give(&entropy_mithril_data.sem_lock);
		len -= bytes;
		buf += bytes;
		// bytes < len means that we have enough entropy in the pool
		if(bytes < len)
		{
			k_sem_take(&entropy_mithril_data.sem_sync, K_FOREVER);
		}
	}
	return 0;
}

static int entropy_mithril_get_entropy_isr(const struct device *dev, uint8_t *buf, uint16_t len,
					   uint32_t flags)
{
	uint16_t cnt = len;

	if (likely((flags & ENTROPY_BUSYWAIT) == 0U)) {
		return get_entropy_from_pool(&entropy_mithril_data.isr_pool, buf, len);
	}

	if (len) {
		unsigned int key;
		int irq_enabled;
		// Disable trng IRQ atomically. Following operations done with polling.
		// So, we need to disable IRQ before polling.
		key = irq_lock();
		irq_enabled = irq_is_enabled(IRQN);
		irq_disable(IRQN);
		irq_unlock(key);
		// To read fresh entropy, clear ready flag
		mithril_trng_clear_flag(TRNG_FLAG_READY);

		// May be disabled before
		mithril_trng_enable(true);

		uint8_t *buf_ptr = buf;
		do {
			while (!mithril_trng_get_flag_status(TRNG_FLAG_READY)) {
				k_cpu_atomic_idle(irq_lock());
			}
			uint8_t read_cnt = read_trng_data_register(buf_ptr, len > 4 ? 4 : len);

			buf_ptr += read_cnt;

			len -= read_cnt;

			mithril_trng_clear_flag(TRNG_FLAG_READY);
		} while (len > 0);

		if (irq_enabled) {
			irq_enable(IRQN);
		}
	}
	return cnt;
}

static int entropy_mithril_init(const struct device *dev)
{
	struct entropy_mithril_dev_data *dev_data = dev->data;
	ring_buf_init(&dev_data->thr_pool.pool, CONFIG_ENTROPY_MITHRIL_TRNG_THR_POOL_SIZE,
		      dev_data->thr_buffer);
	ring_buf_init(&dev_data->isr_pool.pool, CONFIG_ENTROPY_MITHRIL_TRNG_ISR_POOL_SIZE,
		      dev_data->isr_buffer);

	dev_data->thr_pool.threshold = CONFIG_ENTROPY_MITHRIL_TRNG_THR_THRESHOLD;
	dev_data->isr_pool.threshold = CONFIG_ENTROPY_MITHRIL_TRNG_ISR_THRESHOLD;
	// Unlock lock semaphore
	k_sem_init(&dev_data->sem_lock, 1, 1);
	// Lock sync semaphore in init. It will be unlocked in isr
	k_sem_init(&dev_data->sem_sync, 0, 1);

	mithril_trng_enable(true);
	mithril_trng_enable_irq(TRNG_IT_READY, true);
	IRQ_CONNECT(IRQN, IRQ_PRIO, isr, &entropy_mithril_data, 0);
	irq_enable(IRQN);

	return 0;
}

static const struct entropy_driver_api entropy_mithril_api = {
	.get_entropy = entropy_mithril_get_entropy,
	.get_entropy_isr = entropy_mithril_get_entropy_isr};

DEVICE_DT_INST_DEFINE(0, entropy_mithril_init, NULL, &entropy_mithril_data, NULL, PRE_KERNEL_1,
		      CONFIG_ENTROPY_INIT_PRIORITY, &entropy_mithril_api);
