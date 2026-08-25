/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "status_metrics.h"

#include <inttypes.h>
#include <string.h>
#include "bridge_protocol.h"
#include "bridge_spi.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_udp.h"
#include "receiver_control_protocol.h"
#include "wifi_manager.h"

#define LINK_RETRY_INTERVAL_MS 2000u
#define HEALTHY_LOG_INTERVAL_MS 30000u

static const char *TAG = "status";

static bool query_receiver(struct sv_receiver_status_response *status)
{
	uint8_t command[] = {SV_CMD_GET_STATUS};
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	size_t response_length = sizeof(response);
	int result = bridge_spi_command(command, sizeof(command), response, &response_length,
		pdMS_TO_TICKS(500));
	if (result != ESP_OK || response_length < sizeof(*status)) return false;
	memcpy(status, response, sizeof(*status));
	return status->command == SV_CMD_GET_STATUS && status->status == SV_STATUS_OK &&
		status->protocol_version == SV_RECEIVER_CONTROL_VERSION;
}

static void status_task(void *argument)
{
	(void)argument;
	bool link_was_healthy = false;
	vTaskDelay(pdMS_TO_TICKS(500));
	while (true) {
		struct bridge_stats bridge;
		struct gateway_udp_stats gateway;
		struct sv_receiver_status_response receiver;
		bool link_healthy = query_receiver(&receiver);
		bridge_spi_get_stats(&bridge);
		gateway_udp_get_stats(&gateway);

		if (link_healthy) {
			if (!link_was_healthy) {
				ESP_LOGI(TAG,
					"SPI link established: receiver=%02x%02x%02x%02x%02x%02x protocol=%u",
					receiver.receiver_id[0], receiver.receiver_id[1], receiver.receiver_id[2],
					receiver.receiver_id[3], receiver.receiver_id[4], receiver.receiver_id[5],
					receiver.protocol_version);
			}
			ESP_LOGI(TAG,
				"health spi={transfers=%" PRIu32 ",transfer_errors=%" PRIu32
				",reports=%" PRIu32
				",decode=%" PRIu32 ",drops=%" PRIu32 ",timeouts=%" PRIu32
				"} nrf={rf=%" PRIu32 ",rf_crc=%" PRIu32 ",report_drops=%" PRIu32
				",spi_crc=%" PRIu32 "} wifi={connected=%u,setup_ap=%u} udp={connected=%u,datagrams=%" PRIu32
				",reports=%" PRIu32 ",discarded=%" PRIu32 ",receive_errors=%" PRIu32
				",ack_age_ms=%" PRIu32 "}",
				bridge.transfers, bridge.transfer_errors, bridge.reports,
				bridge.decode_errors, bridge.queue_drops,
				bridge.command_timeouts, receiver.rf_received, receiver.rf_crc_errors,
				receiver.report_queue_drops, receiver.spi_crc_errors,
				wifi_manager_is_connected(), wifi_manager_is_setup_ap(),
				gateway.server_connected, gateway.datagrams_sent, gateway.reports_sent,
				gateway.reports_discarded, gateway.receive_errors,
				gateway.last_ack_age_ms);
		} else {
			ESP_LOGW(TAG,
				"SPI receiver unavailable: transfers=%" PRIu32
				", transfer_errors=%" PRIu32 ", decode=%" PRIu32
				", command_timeouts=%" PRIu32,
				bridge.transfers, bridge.transfer_errors, bridge.decode_errors,
				bridge.command_timeouts);
		}

		link_was_healthy = link_healthy;
		vTaskDelay(pdMS_TO_TICKS(link_healthy ? HEALTHY_LOG_INTERVAL_MS : LINK_RETRY_INTERVAL_MS));
	}
}

int status_metrics_init(void)
{
	return xTaskCreate(status_task, "status", 4096, NULL, 5, NULL) == pdPASS ?
		ESP_OK : ESP_ERR_NO_MEM;
}
