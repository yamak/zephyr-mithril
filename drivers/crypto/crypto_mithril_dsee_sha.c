/*
 * Copyright (c) 2025 Yusuf Yamak <yamakyusuf@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief MITHRIL DSEE SHA-256 Driver for Zephyr
 *
 * This driver implements SHA-256 hash functionality using the MITHRIL DSEE
 * hardware accelerator. The driver supports both synchronous and asynchronous
 * operations through the Zephyr Crypto API.
 *
 * Hardware Features:
 * - Hardware-accelerated SHA-256 hash computation
 * - Supports partial hash operations (multi-block processing)
 * - Interrupt-driven operation with READY and DIGEST_VALID (DV) signals
 * - Auto-fetch capability for improved performance
 * - Hardware support for context switching and state preservation
 *
 * Driver Operation:
 * - Synchronous Mode: The driver disables interrupts and waits for hardware
 *   operations to complete. This mode blocks the calling thread until
 *   hash computation is finished. The semaphore is held throughout the entire
 *   operation to prevent concurrent access to the hardware.
 *
 * - Asynchronous Mode: The driver initiates hash operations and returns
 *   immediately. When the operation completes, the hardware triggers an
 *   interrupt, and the driver calls the user-supplied callback function
 *   with the results. The wait_finalize flag is used to prevent new hash
 *   operations from starting until the user has processed the digest.
 *
 * Context Switching:
 * The driver implements full hardware context switching capability, allowing
 * multiple hash operations to be interleaved without losing state. The driver:
 * 1. Saves the complete hardware state (hash block, hash block index, init state
 *    flag, and total bit size) when switching between operations
 * 2. Restores the hardware state before continuing a previously started operation
 * 3. Uses the STATE register (0x7C) to maintain hash_block_index and is_init_block flags
 *
 * Cloning Support:
 * The driver supports cloning hash contexts, enabling multiple identical hash
 * operations to be derived from a common starting point:
 * 1. When a hash context with started=true initiates a new session, the driver
 *    recognizes this as a clone operation
 * 2. The driver copies the hardware context (dsee_ctx) from the source session 
 *    to the new session
 * 3. This enables efficient implementation of hash tree structures and parallel
 *    hash operations from a common prefix
 *
 * Data Flow:
 * 1. The session is initialized with mithril_dsee_hash_begin_session()
 * 2. Data is processed through mithril_dsee_hash_handler()
 * 3. For partial updates, multiple calls can be made with finish=false
 * 4. Final call should set finish=true to complete the hash operation
 * 5. In async mode, results are delivered via callback
 * 6. Sessions are cleaned up with mithril_dsee_hash_free_session()
 * 7. Context state is preserved between calls through the dsee_ctx structure
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <errno.h>
#include <zephyr/crypto/crypto.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include "mithril_dsee.h"

LOG_MODULE_REGISTER(crypto_mithril_dsee, CONFIG_CRYPTO_LOG_LEVEL);

#define DT_DRV_COMPAT   mithril_dsee_hash
#define IRQN            DT_INST_IRQN(0)
#define IRQ_PRIO        DT_INST_IRQ(0, priority)

#define MAX_SHA_SESSION_COUNT 48

#define SESSION_INDEX(session, buffer) \
    (((size_t)(session) - (size_t)(buffer)) / sizeof(struct sha_session))

struct sha_session
{
	mithril_dsee_context_t dsee_ctx;
	bool started;
	bool in_use;
};

struct crypto_mithril_dsee_data {
	hash_completion_cb cb;
	struct hash_ctx *current_ctx;
	struct hash_pkt *pending_pkt;
    struct sha_session session_buffer[MAX_SHA_SESSION_COUNT];
	bool wait_finalize;
};

static struct crypto_mithril_dsee_data crypto_mithril_data;

static uint32_t get_unused_session_index(void)
{
	for(int i = 0; i < MAX_SHA_SESSION_COUNT; i++){
		if(crypto_mithril_data.session_buffer[i].in_use == false){
			crypto_mithril_data.session_buffer[i].in_use = true;
			LOG_DBG("Allocating session_id: %d", i);
			return i;
		}
	}
	return -1;
}

static void dsee_isr(const void *arg)
{
	ARG_UNUSED(arg);
	struct crypto_mithril_dsee_data *data = &crypto_mithril_data;
	bool ready = mithril_dsee_get_it_status(DSEE_IT_DIN_READY); 
	bool dv = mithril_dsee_get_it_status(DSEE_IT_DV);
	
	// Security checks
	if (data->current_ctx == NULL) {
		LOG_ERR("ISR triggered but no active context exists, thread: %p", (void*)k_current_get());
		// Clear the flags to prevent the interrupt from triggering again
		if (dv) mithril_dsee_clear_flag(DSEE_FLAG_DV);
		if (ready) mithril_dsee_clear_flag(DSEE_FLAG_DIN_READY);
		return;
	}
	
	struct hash_ctx *ctx = data->current_ctx;
	
	if (ctx->drv_sessn_state == NULL) {
		LOG_ERR("ISR triggered but context has no session, ctx: %p, thread: %p", (void*)ctx, (void*)k_current_get());
		// Clear the flags
		if (dv) mithril_dsee_clear_flag(DSEE_FLAG_DV);
		if (ready) mithril_dsee_clear_flag(DSEE_FLAG_DIN_READY);
		return;
	}
	
	struct sha_session *session = ctx->drv_sessn_state;
	uint32_t session_id = SESSION_INDEX(session, data->session_buffer);

	LOG_DBG("ISR triggered - ready: %d, digest valid: %d, session_id: %d", 
	        ready, dv, session_id);

	if (dv) {
        mithril_dsee_clear_flag(DSEE_FLAG_DV);
		if (data->pending_pkt) {
			LOG_DBG("Processing digest valid interrupt, session_id: %d", session_id);
			mithril_dsee_read_digest(data->pending_pkt->out_buf);
			if(data->cb != NULL){
				data->cb(data->pending_pkt, 0);
			}
			data->pending_pkt = NULL;
			session->started = false;
            data->wait_finalize = false;
		}
	} else if (ready) {
        mithril_dsee_clear_flag(DSEE_FLAG_DIN_READY);
		if (data->pending_pkt) {
			LOG_DBG("Processing ready interrupt, session_id: %d", session_id);
			if(data->cb != NULL){
				data->cb(data->pending_pkt, 0);
			}
			data->pending_pkt = NULL;
		}
	}
}

static int mithril_dsee_hash_handler(struct hash_ctx *ctx, struct hash_pkt *pkt, bool finish)
{
	int status = 0;
	if (ctx->device == NULL) {
		LOG_ERR("Invalid device context, ctx: %p", (void*)ctx);
		return -ENODEV;
	}
	struct crypto_mithril_dsee_data *data = ctx->device->data;
	struct sha_session *session = ctx->drv_sessn_state;
	uint32_t session_id = SESSION_INDEX(session, data->session_buffer);
	mithril_dsee_wait_idle();
	mithril_dsee_restore_context(&session->dsee_ctx);
	data->current_ctx = ctx;

    if (!session->started) {
        LOG_DBG("Initializing hash for session_id: %d", session_id);

        mithril_dsee_hash_init();
        session->started = true;
    }

	uint16_t flags = ctx->flags;

	if (flags & CAP_SYNC_OPS) {
		mithril_dsee_enable_irq(DSEE_IT_DIN_READY, false);
		mithril_dsee_enable_irq(DSEE_IT_DV, false);
	} else {
		data->pending_pkt = pkt;
		mithril_dsee_enable_irq(DSEE_IT_DIN_READY, true);
		mithril_dsee_enable_irq(DSEE_IT_DV, true);
	}
	if (pkt->in_len > 0) {
		mithril_dsee_write_hash_data_range((uint32_t)pkt->in_buf,
						   (uint32_t)(pkt->in_buf + pkt->in_len - 1));
	}

	if (finish) {
		if (pkt->in_len > 0) {
			mithril_dsee_hash_start_and_finalize();
		} else {
			mithril_dsee_hash_finalize();
		}
        if(flags & CAP_ASYNC_OPS){
			// If the operation is asynchronous, set the wait_finalize flag
			// This flag prevents new operations until the digest is processed
            data->wait_finalize = true;
        }
		else if (flags & CAP_SYNC_OPS) {
			mithril_dsee_wait_digest();
			mithril_dsee_read_digest(pkt->out_buf);
			session->started = false;
        }
	} else {
		// Partial hash operation
		if (pkt->in_len > 0) {
			mithril_dsee_hash_start();
			if (flags & CAP_SYNC_OPS) {
				mithril_dsee_wait_ready();
			}
		}
	}
	// Restore the interrupt enable flags
	if (flags & CAP_ASYNC_OPS) {
		mithril_dsee_enable_irq(DSEE_IT_DIN_READY, false);
		mithril_dsee_enable_irq(DSEE_IT_DV, false);
	}
	mithril_dsee_wait_idle();
	mithril_dsee_save_context(&session->dsee_ctx);
	
	return status;
}


static int mithril_dsee_hash_begin_session(const struct device *dev, struct hash_ctx *ctx,
					   enum hash_algo algo)
{
	struct crypto_mithril_dsee_data *data = (struct crypto_mithril_dsee_data *)dev->data;
    if (algo != CRYPTO_HASH_ALGO_SHA256) {
		LOG_ERR("Unsupported algorithm: %d, ctx: %p", algo, (void*)ctx);
		return -EINVAL;
	}
	
	if (ctx == NULL) {
		LOG_ERR("NULL context provided, thread: %p", (void*)k_current_get());
		return -EINVAL;
	}
	
	uint32_t session_id = get_unused_session_index();

	if(session_id == -1){
		LOG_ERR("No free session available");
		return -ENOMEM;
	}

	struct sha_session *new_session = &data->session_buffer[session_id];
	new_session->started = false;
	// If the session is already started, clone it
    if(ctx->started == true){
        LOG_DBG("Cloning session");
        struct sha_session *old_session = ctx->drv_sessn_state;
        
        if (old_session) {
            memcpy(&new_session->dsee_ctx, &old_session->dsee_ctx, sizeof(mithril_dsee_context_t));
            new_session->started = old_session->started;
            
        } else {
            LOG_WRN("Clone failed: session is empty %p", (void*)ctx);
        }
    }
	
	ctx->drv_sessn_state = new_session;
	ctx->started = true;
	
	
	
	ctx->device = dev;
	ctx->hash_hndlr = mithril_dsee_hash_handler;
	
	LOG_INF("Session started successfully session_id: %d", session_id);
	return 0;
}

static int mithril_dsee_hash_free_session(const struct device *dev, struct hash_ctx *ctx)
{
	struct sha_session* session = ctx->drv_sessn_state;
	session->in_use = false;
	return 0;
}

static int mithril_dsee_hash_async_callback_set(const struct device *dev, hash_completion_cb cb)
{

	struct crypto_mithril_dsee_data *data = (struct crypto_mithril_dsee_data *)dev->data;
	if (cb == NULL) {
		LOG_ERR("Invalid callback pointer, thread: %p", (void*)k_current_get());

		return -EINVAL;
	}
	data->cb = cb;
	return 0;
}

static int mithril_dsee_hash_query_hw_caps(const struct device *dev)
{
	return (CAP_ASYNC_OPS | CAP_SYNC_OPS);
}


static int mithril_dsee_init(const struct device *dev)
{
	struct crypto_mithril_dsee_data *data = (struct crypto_mithril_dsee_data *)dev->data;
	mithril_dsee_clear_flag(DSEE_IT_DV);
	mithril_dsee_clear_flag(DSEE_IT_DIN_READY);

	mithril_dsee_enable_irq(DSEE_IT_DIN_READY, false);
	mithril_dsee_enable_irq(DSEE_IT_DV, false);

	mithril_dsee_enable_auto_fetch(true);

    
	
	IRQ_CONNECT(IRQN, IRQ_PRIO, dsee_isr, 0, 0);
	irq_enable(IRQN);
	LOG_DBG("MITHRIL DSEE SHA driver initialized successfully");

	return 0;
}

static struct crypto_driver_api hash_mithril_api = {
	.hash_begin_session = mithril_dsee_hash_begin_session,
	.hash_free_session = mithril_dsee_hash_free_session,
	.hash_async_callback_set = mithril_dsee_hash_async_callback_set,
	.query_hw_caps = mithril_dsee_hash_query_hw_caps};

DEVICE_DT_INST_DEFINE(0, mithril_dsee_init, NULL, &crypto_mithril_data, NULL, POST_KERNEL,
		      CONFIG_CRYPTO_INIT_PRIORITY, &hash_mithril_api);