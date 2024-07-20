#define DT_DRV_COMPAT mithril_gpio

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/types.h>
#include <zephyr/sys/util.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio/gpio_utils.h>
#include <mithril.h>

#define SUPPORTED_FLAGS (GPIO_INPUT | GPIO_OUTPUT | \
			GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH | \
			GPIO_ACTIVE_LOW | GPIO_ACTIVE_HIGH)

#define GPIO_LOW        0
#define GPIO_HIGH       1

#define LOG_LEVEL CONFIG_GPIO_LOG_LEVEL


/* Helper macros for GPIO */

#define DEV_GPIO_CFG(dev)						\
	((const struct gpio_litex_cfg *)(dev)->config)



/* Driver functions */

static int gpio_mithril_configure(const struct device *dev,
				gpio_pin_t pin, gpio_flags_t flags)
{

	return 0;
}

static int gpio_mithril_port_get_raw(const struct device *dev,
				   gpio_port_value_t *value)
{

	*value = GPIO->in;
	return 0;
}

static int gpio_mithril_port_set_masked_raw(const struct device *dev,
					  gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	uint32_t port_val;

	port_val = GPIO->out;
	port_val = (port_val & ~mask) | (value & mask);
	GPIO->out = port_val;

	return 0;
}

static int gpio_mithril_port_set_bits_raw(const struct device *dev,
					gpio_port_pins_t pins)
{
	uint32_t port_val;

	port_val = GPIO->out;
	port_val |= pins;
	GPIO->out = port_val;

	return 0;
}

static int gpio_mithril_port_clear_bits_raw(const struct device *dev,
					  gpio_port_pins_t pins)
{
	uint32_t port_val;

	port_val = GPIO->out;
	port_val &= ~pins;
	GPIO->out = port_val;

	return 0;
}

static int gpio_mithril_port_toggle_bits(const struct device *dev,
				       gpio_port_pins_t pins)
{
	uint32_t port_val;

	port_val = GPIO->out;
	port_val ^= pins;
	GPIO->out = port_val;

	return 0;
}


static int gpio_mithril_pin_interrupt_configure(const struct device *dev,
					      gpio_pin_t pin,
					      enum gpio_int_mode mode,
					      enum gpio_int_trig trig)
{
    return 0;
}

static int gpio_mithril_init(const struct device *dev)
{
	return 0;
}

static const struct gpio_driver_api gpio_mithril_driver_api = {
	.pin_configure = gpio_mithril_configure,
	.port_get_raw = gpio_mithril_port_get_raw,
	.port_set_masked_raw = gpio_mithril_port_set_masked_raw,
	.port_set_bits_raw = gpio_mithril_port_set_bits_raw,
	.port_clear_bits_raw = gpio_mithril_port_clear_bits_raw,
	.port_toggle_bits = gpio_mithril_port_toggle_bits,
	.pin_interrupt_configure = gpio_mithril_pin_interrupt_configure,
	.manage_callback = NULL,
};

DEVICE_DT_INST_DEFINE(0, \
			    gpio_mithril_init, \
			    NULL, \
			    NULL, \
			    NULL, \
			    POST_KERNEL, \
			    CONFIG_GPIO_INIT_PRIORITY, \
			    &gpio_mithril_driver_api \
			   ); 
