/*
 * Copyright (c) 2025 Yusuf Yamak <yamakyusuf@gmail.com>
 
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mithril_uart

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/types.h>
#include <mithril.h>
struct uart_mithril_config {
	mem_addr_t base;
	void (*irq_config_func)(void);
};

struct uart_mithril_data {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t callback;
	void *cb_data;

#endif
};

static void uart_mithril_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	while ((uart->sr) & UART_SR_TX_FULL);

	uart->txr = c;
}

static int uart_mithril_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_mithril_config *config = dev->config;
	const uart_handle_t *uart = (uart_handle_t*)config->base;
	if(uart->sr & UART_SR_RX_EMPTY)
	{
		return -1;
	}
	*c = uart->rxr;
    return 0;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_mithril_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	int i = 0;
	for(i = 0; i<len; i++)
	{
		if(uart->sr & UART_SR_TX_FULL)
		{
			break;
		}
		uart->txr = tx_data[i];
	}

	return i;
}

static int uart_mithril_fifo_read(const struct device *dev, uint8_t *rx_data, const int len)
{
	int i = 0;
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	for(i = 0; i<len; i++)
	{
		if(uart->sr & UART_SR_RX_EMPTY)
		{
			break;
		}
		rx_data[i] = uart->rxr;
	}
	return i;
}

static void uart_mithril_irq_tx_enable(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	uart->cr |= UART_CR_TX_ENABLE;
}

static void uart_mithril_irq_tx_disable(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	uart->cr &= ~UART_CR_TX_ENABLE;
}

static int uart_mithril_irq_tx_ready(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	const uart_handle_t *uart = (uart_handle_t*)config->base;
	return !(uart->sr & UART_SR_TX_FULL);
}

static void uart_mithril_irq_rx_enable(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	uart->cr |= UART_CR_RX_ENABLE;
}

static void uart_mithril_irq_rx_disable(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	uart->cr &= ~UART_CR_RX_ENABLE;
}

static int uart_mithril_irq_rx_ready(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	uart_handle_t *uart = (uart_handle_t*)config->base;
	return !(uart->sr & UART_SR_RX_EMPTY);
}

static void uart_mithril_irq_err_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void uart_mithril_irq_err_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
}
static int uart_mithril_irq_is_pending(const struct device *dev)
{
	const struct uart_mithril_config *config = dev->config;
	const uart_handle_t *uart = (uart_handle_t*)config->base;
	return !(uart->sr & UART_SR_RX_EMPTY) || !(uart->sr & UART_SR_TX_FULL);
}

static void uart_mithril_irq_callback_set(const struct device *dev,
					   uart_irq_callback_user_data_t cb,
					   void *cb_data)
{
	struct uart_mithril_data *data = dev->data;
	data->callback = cb;
	data->cb_data = cb_data;
}

static int uart_mithril_irq_update(const struct device *dev)
{
	return 1;
}


#endif
static void mithril_uart_irq_handler(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	struct uart_mithril_data *data = dev->data;
	unsigned int key = irq_lock();
	if (data->callback) {
		data->callback(dev, data->cb_data);
	}
	irq_unlock(key);
#endif
}
static const struct uart_driver_api uart_mithril_driver_api = {
	.poll_in		= uart_mithril_poll_in,
	.poll_out		= uart_mithril_poll_out,
	.err_check		= NULL,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_mithril_fifo_fill,
	.fifo_read = uart_mithril_fifo_read,
	.irq_tx_enable = uart_mithril_irq_tx_enable,
	.irq_tx_disable = uart_mithril_irq_tx_disable,
	.irq_tx_ready = uart_mithril_irq_tx_ready,
	.irq_rx_enable = uart_mithril_irq_rx_enable,
	.irq_rx_disable = uart_mithril_irq_rx_disable,
	.irq_rx_ready = uart_mithril_irq_rx_ready,
	.irq_err_enable = uart_mithril_irq_err_enable,
	.irq_err_disable = uart_mithril_irq_err_disable,
	.irq_is_pending = uart_mithril_irq_is_pending,
	.irq_update = uart_mithril_irq_update,
	.irq_callback_set = uart_mithril_irq_callback_set,

#endif
};

static int uart_mithril_init(const struct device *dev)
{
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	const struct uart_mithril_config *config = dev->config;
	config->irq_config_func();
#endif
	return 0;
}
#define UART_MITHRIL_INIT(n) \
	\
	static void uart_mithril_irq_config_func_##n(void) \
	{ \
		IRQ_CONNECT(DT_INST_IRQN(n), \
			DT_INST_IRQ(n, priority), \
			mithril_uart_irq_handler, DEVICE_DT_INST_GET(n), 0); \
		irq_enable(DT_INST_IRQN(n)); \
	} \
	\
	static struct uart_mithril_config uart_mithril_config_##n = \
	{ \
	.base = DT_INST_REG_ADDR(n), \
	.irq_config_func = uart_mithril_irq_config_func_##n \
	}; \
	\
	static struct uart_mithril_data uart_mithril_data_##n; \
	DEVICE_DT_INST_DEFINE(n, \
			      uart_mithril_init, \
			      NULL, &uart_mithril_data_##n, \
				  &uart_mithril_config_##n, \
			      PRE_KERNEL_1,	\
			      CONFIG_SERIAL_INIT_PRIORITY, \
			      &uart_mithril_driver_api);

DT_INST_FOREACH_STATUS_OKAY(UART_MITHRIL_INIT)