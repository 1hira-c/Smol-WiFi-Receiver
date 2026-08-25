/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "bridge_spi.h"

#include <inttypes.h>
#include <string.h>
#include "bridge_protocol.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "esp_rom_sys.h"
#include "receiver_control_protocol.h"

#define BRIDGE_SPI_HOST SPI2_HOST
#define PIN_READY GPIO_NUM_25
#define PIN_CS GPIO_NUM_7
#define PIN_SCK GPIO_NUM_8
#define PIN_MISO GPIO_NUM_9
#define PIN_MOSI GPIO_NUM_10
#define BRIDGE_SPI_CLOCK_HZ 2000000
#define BRIDGE_CS_GUARD_US 2u
#define BRIDGE_REARM_TIMEOUT_US 1000u
#define COMMAND_RETRY_TICKS pdMS_TO_TICKS(50)
#define COMMAND_MAX_ATTEMPTS 3u

static const char *TAG = "bridge-spi";
static spi_device_handle_t spi_handle;
static QueueHandle_t report_queue;
static SemaphoreHandle_t command_mutex;
static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t command_done;
static uint16_t tx_sequence;
static uint16_t next_request_id = 1;
static struct bridge_stats stats;
static DMA_ATTR uint8_t tx_buffer[SV_BRIDGE_ENVELOPE_SIZE];
static DMA_ATTR uint8_t rx_buffer[SV_BRIDGE_ENVELOPE_SIZE];

enum bridge_transfer_result {
	BRIDGE_TRANSFER_ERROR = -1,
	BRIDGE_TRANSFER_COMPLETE = 0,
	BRIDGE_TRANSFER_MORE_PENDING = 1,
};

static struct {
	bool active;
	uint16_t request_id;
	uint8_t payload[SV_BRIDGE_PAYLOAD_SIZE];
	uint8_t payload_length;
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	uint8_t response_length;
	uint8_t attempts;
	TickType_t next_attempt;
	int result;
} command;

static bool prepare_command(struct sv_bridge_message *message)
{
	bool send = false;
	xSemaphoreTake(state_mutex, portMAX_DELAY);
	TickType_t now = xTaskGetTickCount();
	if (command.active && command.attempts < COMMAND_MAX_ATTEMPTS &&
		(command.attempts == 0 || (int32_t)(now - command.next_attempt) >= 0)) {
		message->kind = SV_BRIDGE_COMMAND;
		message->request_id = command.request_id;
		message->payload_length = command.payload_length;
		memcpy(message->payload, command.payload, command.payload_length);
		command.attempts++;
		command.next_attempt = now + COMMAND_RETRY_TICKS;
		send = true;
	}
	xSemaphoreGive(state_mutex);
	return send;
}

static void accept_response(const struct sv_bridge_message *message)
{
	xSemaphoreTake(state_mutex, portMAX_DELAY);
	if (command.active && message->request_id == command.request_id) {
		command.response_length = message->payload_length;
		memcpy(command.response, message->payload, message->payload_length);
		command.result = (message->flags & SV_BRIDGE_FLAG_ERROR) ? ESP_FAIL : ESP_OK;
		command.active = false;
		xSemaphoreGive(command_done);
	}
	xSemaphoreGive(state_mutex);
}

static enum bridge_transfer_result transact(void)
{
	memset(tx_buffer, 0, sizeof(tx_buffer));
	memset(rx_buffer, 0, sizeof(rx_buffer));
	struct sv_bridge_message outgoing = {.sequence = tx_sequence++};
	(void)prepare_command(&outgoing);
	sv_bridge_encode(tx_buffer, &outgoing);
	spi_transaction_t transaction = {
		.length = SV_BRIDGE_ENVELOPE_SIZE * 8u,
		.tx_buffer = tx_buffer,
		.rx_buffer = rx_buffer,
	};
	/* nRF54L15 SPIS requires at least 1 us from CSN to SCK and back. */
	gpio_set_level(PIN_CS, 0);
	esp_rom_delay_us(BRIDGE_CS_GUARD_US);
	esp_err_t result = spi_device_polling_transmit(spi_handle, &transaction);
	esp_rom_delay_us(BRIDGE_CS_GUARD_US);
	gpio_set_level(PIN_CS, 1);
	if (result != ESP_OK) {
		stats.transfer_errors++;
		if (stats.transfer_errors <= 3 || (stats.transfer_errors % 1000u) == 0) {
			ESP_LOGE(TAG, "SPI transaction failed (%" PRIu32 "): %s",
				stats.transfer_errors, esp_err_to_name(result));
		}
		return BRIDGE_TRANSFER_ERROR;
	}
	stats.transfers++;
	struct sv_bridge_message incoming;
	if (!sv_bridge_decode(&incoming, rx_buffer)) {
		stats.decode_errors++;
		if (stats.decode_errors <= 3 || (stats.decode_errors % 10000u) == 0) {
			ESP_LOG_BUFFER_HEXDUMP(TAG, rx_buffer, 16, ESP_LOG_WARN);
		}
		return BRIDGE_TRANSFER_COMPLETE;
	}
	if (incoming.kind == SV_BRIDGE_RECEIVER_REPORT &&
		incoming.payload_length == SV_SMOL_REPORT_SIZE) {
		if (xQueueSend(report_queue, incoming.payload, 0) != pdTRUE) stats.queue_drops++;
		else stats.reports++;
	} else if (incoming.kind == SV_BRIDGE_COMMAND_RESPONSE) {
		accept_response(&incoming);
	}
	return (incoming.flags & SV_BRIDGE_FLAG_MORE_PENDING) != 0 ?
		BRIDGE_TRANSFER_MORE_PENDING : BRIDGE_TRANSFER_COMPLETE;
}

static bool wait_for_receiver_rearm(void)
{
	bool saw_not_ready = false;
	for (unsigned waited = 0; waited < BRIDGE_REARM_TIMEOUT_US; ++waited) {
		if (!gpio_get_level(PIN_READY)) {
			saw_not_ready = true;
			break;
		}
		esp_rom_delay_us(1);
	}
	if (!saw_not_ready) return false;
	for (unsigned waited = 0; waited < BRIDGE_REARM_TIMEOUT_US; ++waited) {
		if (gpio_get_level(PIN_READY)) return true;
		esp_rom_delay_us(1);
	}
	return false;
}

static void bridge_task(void *argument)
{
	(void)argument;
	while (true) {
		if (gpio_get_level(PIN_READY)) {
			for (unsigned drained = 0; drained < 8; ++drained) {
				enum bridge_transfer_result result = transact();
				if (result != BRIDGE_TRANSFER_MORE_PENDING) break;
				/* Do not mistake the previous transfer's READY-high for rearm. */
				if (!wait_for_receiver_rearm()) break;
			}
		}
		/* Poll at 1 kHz and always leave CPU time for the idle task/watchdog. */
		vTaskDelay(pdMS_TO_TICKS(1));
	}
}

int bridge_spi_init(void)
{
	report_queue = xQueueCreate(128, SV_SMOL_REPORT_SIZE);
	command_mutex = xSemaphoreCreateMutex();
	state_mutex = xSemaphoreCreateMutex();
	command_done = xSemaphoreCreateBinary();
	if (!report_queue || !command_mutex || !state_mutex || !command_done) return ESP_ERR_NO_MEM;
	gpio_config_t ready = {
		.pin_bit_mask = 1ULL << PIN_READY,
		.mode = GPIO_MODE_INPUT,
		.pull_down_en = GPIO_PULLDOWN_ENABLE,
	};
	ESP_RETURN_ON_ERROR(gpio_config(&ready), TAG, "READY gpio");
	gpio_config_t chip_select = {
		.pin_bit_mask = 1ULL << PIN_CS,
		.mode = GPIO_MODE_OUTPUT,
	};
	ESP_RETURN_ON_ERROR(gpio_config(&chip_select), TAG, "CS gpio");
	ESP_RETURN_ON_ERROR(gpio_set_level(PIN_CS, 1), TAG, "CS idle");
	spi_bus_config_t bus = {
		.mosi_io_num = PIN_MOSI,
		.miso_io_num = PIN_MISO,
		.sclk_io_num = PIN_SCK,
		.quadwp_io_num = -1,
		.quadhd_io_num = -1,
		.max_transfer_sz = SV_BRIDGE_ENVELOPE_SIZE,
	};
	ESP_RETURN_ON_ERROR(spi_bus_initialize(BRIDGE_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");
	spi_device_interface_config_t device = {
		.clock_speed_hz = BRIDGE_SPI_CLOCK_HZ,
		.mode = 0,
		.spics_io_num = -1,
		.queue_size = 1,
	};
	ESP_RETURN_ON_ERROR(spi_bus_add_device(BRIDGE_SPI_HOST, &device, &spi_handle), TAG, "SPI device");
	if (xTaskCreate(bridge_task, "bridge-spi", 4096, NULL, 12, NULL) != pdPASS) return ESP_ERR_NO_MEM;
	return ESP_OK;
}

bool bridge_spi_receive_report(uint8_t report[SV_SMOL_REPORT_SIZE], TickType_t timeout)
{
	return xQueueReceive(report_queue, report, timeout) == pdTRUE;
}

int bridge_spi_command(const uint8_t *request, size_t request_length,
	uint8_t *response, size_t *response_length, TickType_t timeout)
{
	if (!request || request_length == 0 || request_length > SV_BRIDGE_PAYLOAD_SIZE ||
		!response || !response_length) return ESP_ERR_INVALID_ARG;
	if (xSemaphoreTake(command_mutex, timeout) != pdTRUE) return ESP_ERR_TIMEOUT;
	(void)xSemaphoreTake(command_done, 0);
	xSemaphoreTake(state_mutex, portMAX_DELAY);
	command.active = true;
	command.request_id = next_request_id++;
	command.payload_length = (uint8_t)request_length;
	memcpy(command.payload, request, request_length);
	command.response_length = 0;
	command.attempts = 0;
	command.result = ESP_ERR_TIMEOUT;
	xSemaphoreGive(state_mutex);
	int result = ESP_ERR_TIMEOUT;
	if (xSemaphoreTake(command_done, timeout) == pdTRUE) {
		xSemaphoreTake(state_mutex, portMAX_DELAY);
		if (*response_length < command.response_length) result = ESP_ERR_INVALID_SIZE;
		else {
			memcpy(response, command.response, command.response_length);
			*response_length = command.response_length;
			result = command.result;
		}
		xSemaphoreGive(state_mutex);
	} else {
		xSemaphoreTake(state_mutex, portMAX_DELAY);
		command.active = false;
		stats.command_timeouts++;
		xSemaphoreGive(state_mutex);
	}
	xSemaphoreGive(command_mutex);
	return result;
}

int bridge_spi_set_radio_enabled(bool enabled)
{
	uint8_t request[] = {SV_CMD_SET_RADIO_ENABLED, enabled ? 1u : 0u};
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	size_t response_length = sizeof(response);
	return bridge_spi_command(request, sizeof(request), response, &response_length,
		pdMS_TO_TICKS(400));
}

void bridge_spi_get_stats(struct bridge_stats *output)
{
	if (output) *output = stats;
}
