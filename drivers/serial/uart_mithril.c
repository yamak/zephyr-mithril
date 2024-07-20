#define DT_DRV_COMPAT mithril_uart

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/types.h>
#include <mithril.h>

#define UART_RXR_ADDR		DT_INST_REG_ADDR_BY_NAME(0, rxr)
#define UART_TXR_ADDR		DT_INST_REG_ADDR_BY_NAME(0, txr)
#define UART_SR_ADDR		DT_INST_REG_ADDR_BY_NAME(0, sr)

#define UART_EV_TX		(1 << 0)
#define UART_EV_RX		(1 << 1)
#define UART_IRQ		DT_INST_IRQN(0)

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
typedef void (*irq_cfg_func_t)(void);
#endif




static void uart_mithril_poll_out(const struct device *dev, unsigned char c)
{
  while ((UART0->sr)&0x02);

  UART0->txr = c;
}


static int uart_mithril_poll_in(const struct device *dev, unsigned char *c)
{
    return 0;
}

static int uart_mithril_init(const struct device *dev)
{
    return 0;
}

static const struct uart_driver_api uart_mithril_driver_api = {
	.poll_in		= uart_mithril_poll_in,
	.poll_out		= uart_mithril_poll_out,
	.err_check		= NULL,
};

DEVICE_DT_INST_DEFINE(0,
		uart_mithril_init,
		NULL,
		NULL, NULL,
		PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,
		(void *)&uart_mithril_driver_api);

