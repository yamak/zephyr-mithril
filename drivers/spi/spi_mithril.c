

#define DT_DRV_COMPAT mithril_spi

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_mithril);

#include "mithril_spi.h"
#include <zephyr/drivers/spi.h>
#include "spi_context.h"
#include <stdbool.h>


struct spi_mithril_data {
    struct spi_context ctx;
};

struct spi_mithril_cfg {
    spi_handle_t* base;
    uint32_t f_sys;
};


static int spi_config(const struct device *dev, uint32_t frequency,
	       uint16_t operation)
{

    const struct spi_mithril_cfg* cfg = dev->config;
    spi_handle_t* spi_handle = cfg->base;
    spi_init_t spi_init_struct;
	if (operation & SPI_HALF_DUPLEX) {
		return -ENOTSUP;
	}

	if (SPI_OP_MODE_GET(operation) != SPI_OP_MODE_MASTER) {
		return -ENOTSUP;
	}

	if (operation & SPI_MODE_LOOP) {
		return -ENOTSUP;
	}

    if (SPI_WORD_SIZE_GET(operation) != 8) {
        return -ENOTSUP;
    }

    if (operation & SPI_TRANSFER_LSB) {
        return -ENOTSUP;
    }

    uint16_t baudrate = (cfg->f_sys / (frequency*2)) - 2;
    if((cfg->f_sys/((baudrate+2)*2)) > frequency)
        baudrate+=4;
    spi_init_struct.baudrate_prescalar = baudrate;
    if(spi_init_struct.baudrate_prescalar)
    spi_init_struct.cpol = operation & SPI_MODE_CPOL;
    spi_init_struct.cpha = operation & SPI_MODE_CPHA;
    spi_init(spi_handle, &spi_init_struct);
	return 0;
}

static void spi_mithril_xfer(const struct device *dev)
{

    struct spi_mithril_data *data = dev->data;
    struct spi_context *ctx = &data->ctx;
    const struct spi_mithril_cfg* cfg = dev->config;
    spi_handle_t* spi_handle = cfg->base;
    uint8_t read_data;

    while (spi_context_tx_on(ctx) || spi_context_rx_on(ctx)) {
        bool txe=false,rxe=false;
        if(spi_context_tx_on(ctx) && spi_context_rx_on(ctx)) {
            txe=true;
            rxe=true;
            read_data = spi_xfer(spi_handle, *ctx->tx_buf);
            *ctx->rx_buf = read_data;
        } else if(spi_context_tx_on(ctx)){
            txe=true;
            spi_transmit(spi_handle, *ctx->tx_buf);
        } else {
            rxe=true;
            spi_receive(spi_handle,ctx->rx_buf);
        }

        if(txe)
            spi_context_update_tx(ctx,1,1);
        if(rxe)
            spi_context_update_rx(ctx,1,1);
    }
    spi_context_cs_control(ctx, false);
	spi_context_complete(ctx, dev, 0);
}

static int spi_mithril_transceive(const struct device *dev,
			  const struct spi_config *config,
			  const struct spi_buf_set *tx_bufs,
			  const struct spi_buf_set *rx_bufs)
{
    int rc = 0;
    struct spi_mithril_data *data = dev->data;

    /* Lock the SPI Context */
    spi_context_lock(&data->ctx, false, NULL, NULL, config);

    /* Configure the SPI bus */
    data->ctx.config = config;
    rc = spi_config(dev, config->frequency, config->operation);
    if (rc < 0) {
        spi_context_release(&data->ctx, rc);
        return rc;
    }

    spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);


    spi_context_cs_control(&data->ctx, true);

    /* Perform transfer */
    spi_mithril_xfer(dev);

    rc = spi_context_wait_for_completion(&data->ctx);

    spi_context_release(&data->ctx, rc);

    return rc;
}

static int spi_mithril_release(const struct device *dev,
		       const struct spi_config *config)
{
    struct spi_mithril_data *data = dev->data;
	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static int spi_mithril_init(const struct device *dev){
    struct spi_mithril_data *data = dev->data;
    spi_context_unlock_unconditionally(&data->ctx);
    return 0;
}

/* Device Instantiation */

static const struct spi_driver_api spi_mithril_api = {
	.transceive = spi_mithril_transceive,
	.release = spi_mithril_release,
};

#define SPI_INIT(n)	\
	static struct spi_mithril_data spi_mithril_data_##n = { \
		SPI_CONTEXT_INIT_LOCK(spi_mithril_data_##n, ctx), \
		SPI_CONTEXT_INIT_SYNC(spi_mithril_data_##n, ctx), \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx)	\
	}; \
	static struct spi_mithril_cfg spi_mithril_cfg_##n = { \
		.base = (spi_handle_t *)DT_INST_REG_ADDR(n), \
		.f_sys = CPU_FREQ, \
	}; \
	DEVICE_DT_INST_DEFINE(n, \
			spi_mithril_init, \
			NULL, \
			&spi_mithril_data_##n, \
			&spi_mithril_cfg_##n, \
			POST_KERNEL, \
			CONFIG_SPI_INIT_PRIORITY, \
			&spi_mithril_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_INIT)
