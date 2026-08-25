/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "gateway_udp.h"

#include <errno.h>
#include <string.h>
#include "app_config.h"
#include "bridge_spi.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_protocol.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "wifi_manager.h"

#define DISCOVER_INTERVAL_MS 1000
#define HELLO_INTERVAL_MS 1000
#define ACK_TIMEOUT_MS 3000

static const char *TAG = "gateway-udp";
static uint8_t gateway_id[6];
static uint32_t boot_id;
static uint32_t sequence;
static struct gateway_udp_stats stats;
static TickType_t last_ack;

static bool resolve_server(const struct app_config *config, struct sockaddr_in *address)
{
	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_port = htons(config->server_port);
	if (config->server[0] == '\0') return false;
	struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
	struct addrinfo *result = NULL;
	if (getaddrinfo(config->server, NULL, &hints, &result) != 0 || !result) return false;
	address->sin_addr = ((struct sockaddr_in *)result->ai_addr)->sin_addr;
	freeaddrinfo(result);
	return true;
}

static int send_message(int socket_fd, const struct sockaddr_in *destination,
	uint8_t kind, const uint8_t *reports, uint8_t report_count)
{
	struct sv_gateway_message message = {
		.kind = kind,
		.report_count = report_count,
		.boot_id = boot_id,
		.sequence = sequence++,
		.payload_length = report_count * SV_SMOL_REPORT_SIZE,
	};
	memcpy(message.gateway_id, gateway_id, sizeof(gateway_id));
	if (message.payload_length > 0) memcpy(message.payload, reports, message.payload_length);
	uint8_t bytes[SV_GATEWAY_MAX_DATAGRAM];
	size_t length = sv_gateway_encode(bytes, &message);
	if (length == 0) return -1;
	int sent = sendto(socket_fd, bytes, length, 0,
		(const struct sockaddr *)destination, sizeof(*destination));
	if (sent == (int)length) {
		stats.datagrams_sent++;
		stats.reports_sent += report_count;
	}
	return sent;
}

static void receive_control(int socket_fd, struct sockaddr_in *server, bool *server_known)
{
	uint8_t bytes[SV_GATEWAY_MAX_DATAGRAM];
	struct sockaddr_in source;
	socklen_t source_length = sizeof(source);
	int length = recvfrom(socket_fd, bytes, sizeof(bytes), MSG_DONTWAIT,
		(struct sockaddr *)&source, &source_length);
	if (length < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) stats.receive_errors++;
		return;
	}
	struct sv_gateway_message message;
	if (!sv_gateway_decode(&message, bytes, (size_t)length)) {
		stats.receive_errors++;
		return;
	}
	if (memcmp(message.gateway_id, gateway_id, sizeof(gateway_id)) != 0 ||
		message.boot_id != boot_id) return;
	if (message.kind == SV_GATEWAY_OFFER) {
		*server = source;
		*server_known = true;
		(void)send_message(socket_fd, server, SV_GATEWAY_HELLO, NULL, 0);
	} else if (message.kind == SV_GATEWAY_ACK && *server_known &&
		source.sin_addr.s_addr == server->sin_addr.s_addr) {
		last_ack = xTaskGetTickCount();
		stats.server_connected = true;
	}
}

static void discard_pending_reports(void)
{
	uint8_t report[SV_SMOL_REPORT_SIZE];
	while (bridge_spi_receive_report(report, 0)) stats.reports_discarded++;
}

static void gateway_task(void *argument)
{
	(void)argument;
	struct app_config config;
	app_config_get(&config);
	while (true) {
		while (!wifi_manager_is_connected()) {
			/* Never let a network outage backpressure the real-time SPI link. */
			discard_pending_reports();
			xEventGroupWaitBits(wifi_manager_event_group(), WIFI_CONNECTED_BIT,
				pdFALSE, pdTRUE, pdMS_TO_TICKS(1));
		}
		int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (socket_fd < 0) {
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		int broadcast = 1;
		(void)setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
		struct sockaddr_in server;
		bool fixed_server = config.server[0] != '\0';
		bool server_known = fixed_server && resolve_server(&config, &server);
		struct sockaddr_in discover = {
			.sin_family = AF_INET,
			.sin_port = htons(config.server_port),
			.sin_addr.s_addr = htonl(INADDR_BROADCAST),
		};
		TickType_t last_discover = 0;
		TickType_t last_hello = 0;
		last_ack = 0;
		stats.server_connected = false;
		while (wifi_manager_is_connected()) {
			receive_control(socket_fd, &server, &server_known);
			/* Read now after receive_control(). An ACK records last_ack using the
			 * current tick; using an older now here can underflow now-last_ack
			 * when receiving across a tick boundary and cause a false timeout. */
			TickType_t now = xTaskGetTickCount();
			if (fixed_server && !server_known &&
				now - last_discover >= pdMS_TO_TICKS(DISCOVER_INTERVAL_MS)) {
				server_known = resolve_server(&config, &server);
				last_discover = now;
			} else if (!fixed_server && !server_known &&
				now - last_discover >= pdMS_TO_TICKS(DISCOVER_INTERVAL_MS)) {
				(void)send_message(socket_fd, &discover, SV_GATEWAY_DISCOVER, NULL, 0);
				last_discover = now;
			}
			if (server_known && now - last_hello >= pdMS_TO_TICKS(HELLO_INTERVAL_MS)) {
				(void)send_message(socket_fd, &server, SV_GATEWAY_HELLO, NULL, 0);
				last_hello = now;
			}
			if (stats.server_connected && now - last_ack >= pdMS_TO_TICKS(ACK_TIMEOUT_MS)) {
				stats.server_connected = false;
				if (!fixed_server) server_known = false;
			}
			if (stats.server_connected) {
				uint8_t reports[SV_GATEWAY_MAX_PAYLOAD];
				uint8_t count = 0;
				while (count < SV_GATEWAY_MAX_REPORTS && bridge_spi_receive_report(
					&reports[count * SV_SMOL_REPORT_SIZE], 0)) count++;
				if (count && send_message(socket_fd, &server, SV_GATEWAY_REPORT_BATCH,
					reports, count) < 0) stats.reports_discarded += count;
			} else discard_pending_reports();
			stats.last_ack_age_ms = last_ack == 0 ? UINT32_MAX :
				(uint32_t)((now - last_ack) * portTICK_PERIOD_MS);
			vTaskDelay(pdMS_TO_TICKS(1));
		}
		close(socket_fd);
	}
}

int gateway_udp_init(void)
{
	ESP_RETURN_ON_ERROR(esp_read_mac(gateway_id, ESP_MAC_WIFI_STA), TAG, "gateway MAC");
	boot_id = esp_random();
	if (xTaskCreate(gateway_task, "gateway-udp", 6144, NULL, 9, NULL) != pdPASS) {
		return ESP_ERR_NO_MEM;
	}
	return ESP_OK;
}

void gateway_udp_get_stats(struct gateway_udp_stats *output)
{
	if (output) *output = stats;
}
