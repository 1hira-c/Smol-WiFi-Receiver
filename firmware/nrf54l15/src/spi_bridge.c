/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "spi_bridge.h"

#include "bridge_protocol.h"
#include "receiver_control.h"
#include "report_builder.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_bridge, LOG_LEVEL_INF);

#define SPI_NODE DT_NODELABEL(spi20)
#define USER_NODE DT_PATH(zephyr_user)
#define READY_LOW_GUARD_US 50u

static const struct device *spi_device = DEVICE_DT_GET(SPI_NODE);
static const struct gpio_dt_spec ready_gpio =
	GPIO_DT_SPEC_GET(USER_NODE, bridge_ready_gpios);
static const struct spi_config spi_config = {
	/* Maximum accepted peripheral clock; the jumper prototype controller uses 2 MHz. */
	.frequency = 8000000,
	.operation = SPI_OP_MODE_SLAVE | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	.slave = 0,
};
static uint8_t tx_buffer[SV_BRIDGE_ENVELOPE_SIZE] __aligned(4);
static uint8_t rx_buffer[SV_BRIDGE_ENVELOPE_SIZE] __aligned(4);
K_SEM_DEFINE(transfer_done, 0, 1);
K_SEM_DEFINE(bridge_start, 0, 1);
static struct sv_bridge_message pending_response;
static bool response_pending;
static uint16_t tx_sequence;
static uint32_t crc_errors;

static void transfer_callback(const struct device *device, int result, void *user_data)
{
	ARG_UNUSED(device);
	ARG_UNUSED(result);
	ARG_UNUSED(user_data);
	(void)gpio_pin_set_dt(&ready_gpio, 0);
	k_sem_give(&transfer_done);
}

static void prepare_tx(void)
{
	struct sv_bridge_message message = {0};
	message.sequence = tx_sequence++;
	if (response_pending) {
		message = pending_response;
		message.sequence = (uint16_t)(tx_sequence - 1u);
		response_pending = false;
	} else if (report_builder_next(message.payload)) {
		message.kind = SV_BRIDGE_RECEIVER_REPORT;
		message.payload_length = SV_BRIDGE_PAYLOAD_SIZE;
	}
	if (report_builder_pending() || response_pending) {
		message.flags |= SV_BRIDGE_FLAG_MORE_PENDING;
	}
	sv_bridge_encode(tx_buffer, &message);
}

static void process_rx(void)
{
	struct sv_bridge_message request;
	if (!sv_bridge_decode(&request, rx_buffer)) {
		crc_errors++;
		return;
	}
	if (request.kind == SV_BRIDGE_COMMAND) {
		receiver_control_handle(&request, &pending_response);
		response_pending = true;
	}
}

static void spi_thread(void)
{
	struct spi_buf tx = {.buf = tx_buffer, .len = sizeof(tx_buffer)};
	struct spi_buf rx = {.buf = rx_buffer, .len = sizeof(rx_buffer)};
	struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};

	k_sem_take(&bridge_start, K_FOREVER);
	while (true) {
		prepare_tx();
		memset(rx_buffer, 0, sizeof(rx_buffer));
		int result = spi_transceive_cb(spi_device, &spi_config, &tx_set, &rx_set,
			transfer_callback, NULL);
		if (result != 0) {
			LOG_ERR("Failed to arm SPIS: %d", result);
			k_msleep(10);
			continue;
		}
		(void)gpio_pin_set_dt(&ready_gpio, 1);
		k_sem_take(&transfer_done, K_FOREVER);
		process_rx();
		/* Let the controller observe READY low before this transfer is rearmed. */
		k_busy_wait(READY_LOW_GUARD_US);
	}
}

K_THREAD_DEFINE(spi_bridge_thread, 4096, spi_thread, NULL, NULL, NULL, 5, 0, 0);

int spi_bridge_init(void)
{
	if (!device_is_ready(spi_device) || !gpio_is_ready_dt(&ready_gpio)) {
		return -ENODEV;
	}
	int result = gpio_pin_configure_dt(&ready_gpio, GPIO_OUTPUT_INACTIVE);
	if (result == 0) {
		receiver_control_init();
		k_sem_give(&bridge_start);
	}
	return result;
}

uint32_t spi_bridge_crc_errors(void)
{
	return crc_errors;
}
