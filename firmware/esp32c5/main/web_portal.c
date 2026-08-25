/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "web_portal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "bridge_protocol.h"
#include "bridge_spi.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_udp.h"
#include "id48_codec.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "receiver_control_protocol.h"
#include "wifi_manager.h"

static const char *TAG = "web";
static httpd_handle_t server;

static const char index_html[] =
	"<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>"
	"<title>Smol Wi-Fi Receiver</title><style>body{font-family:sans-serif;max-width:42rem;margin:2rem auto;padding:0 1rem}"
	"input,button{font:inherit;padding:.5rem;margin:.25rem 0;width:100%;box-sizing:border-box}pre{background:#eee;padding:1rem}</style></head>"
	"<body><h1>Smol Wi-Fi Receiver</h1><p>Unofficial third-party receiver compatible with SlimeVR.</p><pre id=s>Loading...</pre><form id=f>"
	"<input name=ssid placeholder='5 GHz SSID' required><input name=password type=password placeholder='Wi-Fi password'>"
	"<input name=country value=JP maxlength=2 required><input name=server placeholder='Server IPv4 or hostname (optional)'>"
	"<input name=serverPort value=6969 type=number min=1 max=65535><button>Save and reboot</button></form>"
	"<script>fetch('/api/status').then(r=>r.json()).then(x=>s.textContent=JSON.stringify(x,null,2));"
	"f.onsubmit=async e=>{e.preventDefault();let o=Object.fromEntries(new FormData(f));o.serverPort=+o.serverPort;"
	"let r=await fetch('/api/config',{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(o)});"
	"alert(await r.text())}</script></body></html>";

static esp_err_t json_response(httpd_req_t *request, cJSON *json)
{
	char *text = cJSON_PrintUnformatted(json);
	cJSON_Delete(json);
	if (!text) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
	httpd_resp_set_type(request, "application/json");
	esp_err_t result = httpd_resp_sendstr(request, text);
	cJSON_free(text);
	return result;
}

static esp_err_t index_handler(httpd_req_t *request)
{
	httpd_resp_set_type(request, "text/html; charset=utf-8");
	return httpd_resp_send(request, index_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request)
{
	struct app_config config;
	struct bridge_stats bridge;
	struct gateway_udp_stats gateway;
	app_config_get(&config);
	bridge_spi_get_stats(&bridge);
	gateway_udp_get_stats(&gateway);
	cJSON *root = cJSON_CreateObject();
	cJSON_AddBoolToObject(root, "wifiConnected", wifi_manager_is_connected());
	cJSON_AddBoolToObject(root, "setupAp", wifi_manager_is_setup_ap());
	cJSON_AddStringToObject(root, "setupSsid", wifi_manager_setup_ssid());
	cJSON_AddStringToObject(root, "stationSsid", config.ssid);
	cJSON_AddStringToObject(root, "country", config.country);
	cJSON_AddStringToObject(root, "server", config.server);
	cJSON_AddNumberToObject(root, "serverPort", config.server_port);
	cJSON_AddBoolToObject(root, "serverConnected", gateway.server_connected);
	cJSON_AddNumberToObject(root, "datagramsSent", gateway.datagrams_sent);
	cJSON_AddNumberToObject(root, "reportsSent", gateway.reports_sent);
	cJSON_AddNumberToObject(root, "reportsDiscarded", gateway.reports_discarded);
	cJSON_AddNumberToObject(root, "udpReceiveErrors", gateway.receive_errors);
	cJSON_AddNumberToObject(root, "lastAckAgeMs", gateway.last_ack_age_ms);
	cJSON_AddNumberToObject(root, "spiTransfers", bridge.transfers);
	cJSON_AddNumberToObject(root, "spiTransferErrors", bridge.transfer_errors);
	cJSON_AddNumberToObject(root, "spiReports", bridge.reports);
	cJSON_AddNumberToObject(root, "spiDecodeErrors", bridge.decode_errors);
	cJSON_AddNumberToObject(root, "spiQueueDrops", bridge.queue_drops);
	cJSON_AddNumberToObject(root, "spiCommandTimeouts", bridge.command_timeouts);

	uint8_t command[] = {SV_CMD_GET_STATUS};
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	size_t response_length = sizeof(response);
	if (bridge_spi_command(command, sizeof(command), response, &response_length,
		pdMS_TO_TICKS(400)) == ESP_OK && response_length >= sizeof(struct sv_receiver_status_response)) {
		struct sv_receiver_status_response status;
		memcpy(&status, response, sizeof(status));
		cJSON *receiver = cJSON_AddObjectToObject(root, "receiver");
		char receiver_id[13];
		char group_id[13];
		sv_id48_format(receiver_id, status.receiver_id);
		sv_id48_format(group_id, status.group_id);
		cJSON_AddStringToObject(receiver, "receiverId", receiver_id);
		cJSON_AddStringToObject(receiver, "groupId", group_id);
		cJSON_AddBoolToObject(receiver, "primary", (status.flags & SV_RECEIVER_STATUS_PRIMARY) != 0);
		cJSON_AddBoolToObject(receiver, "pairing", (status.flags & SV_RECEIVER_STATUS_PAIRING) != 0);
		cJSON_AddBoolToObject(receiver, "radioEnabled", (status.flags & SV_RECEIVER_STATUS_RADIO_ENABLED) != 0);
		cJSON_AddNumberToObject(receiver, "trackers", status.stored_trackers);
		cJSON_AddNumberToObject(receiver, "rfReceived", status.rf_received);
		cJSON_AddNumberToObject(receiver, "rfCrcErrors", status.rf_crc_errors);
		cJSON_AddNumberToObject(receiver, "queueDrops", status.report_queue_drops);
		cJSON_AddNumberToObject(receiver, "spiCrcErrors", status.spi_crc_errors);
		cJSON_AddNumberToObject(receiver, "uptimeMs", status.uptime_ms);
	}
	return json_response(request, root);
}

static esp_err_t receive_json(httpd_req_t *request, cJSON **json)
{
	if (request->content_len == 0 || request->content_len > 1024) {
		(void)httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid body length");
		return ESP_FAIL;
	}
	char *body = calloc(1, request->content_len + 1);
	if (!body) {
		(void)httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
		return ESP_ERR_NO_MEM;
	}
	size_t received = 0;
	while (received < request->content_len) {
		int chunk = httpd_req_recv(request, &body[received], request->content_len - received);
		if (chunk <= 0) {
			free(body);
			(void)httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Incomplete body");
			return ESP_FAIL;
		}
		received += (size_t)chunk;
	}
	*json = cJSON_Parse(body);
	free(body);
	if (!*json) {
		(void)httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid JSON");
		return ESP_FAIL;
	}
	return ESP_OK;
}

static void reboot_task(void *argument)
{
	(void)argument;
	vTaskDelay(pdMS_TO_TICKS(250));
	esp_restart();
}

static bool supported_country(const char *country)
{
	static const char codes[] =
		"|01|AT|AU|BE|BG|BR|CA|CH|CN|CY|CZ|DE|DK|EE|ES|FI|FR|GB|GR|HK|HR|HU|"
		"IE|IN|IS|IT|JP|KR|LI|LT|LU|LV|MT|MX|NL|NO|NZ|PL|PT|RO|SE|SI|SK|TW|US|";
	char needle[5] = {'|', country[0], country[1], '|', 0};
	return strstr(codes, needle) != NULL;
}

static esp_err_t config_handler(httpd_req_t *request)
{
	cJSON *json;
	esp_err_t result = receive_json(request, &json);
	if (result != ESP_OK) return result;
	struct app_config config;
	app_config_get(&config);
	cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
	cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
	cJSON *country = cJSON_GetObjectItemCaseSensitive(json, "country");
	cJSON *server_name = cJSON_GetObjectItemCaseSensitive(json, "server");
	cJSON *port = cJSON_GetObjectItemCaseSensitive(json, "serverPort");
	if (!cJSON_IsString(ssid) || !cJSON_IsString(password) || !cJSON_IsString(country) ||
		!cJSON_IsString(server_name) || !cJSON_IsNumber(port)) {
		cJSON_Delete(json);
		return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing configuration fields");
	}
	if (strlen(ssid->valuestring) == 0 || strlen(ssid->valuestring) > 32 ||
		strlen(password->valuestring) > 64 || strlen(country->valuestring) != 2 ||
		strlen(server_name->valuestring) > 64 || port->valuedouble < 1 ||
		port->valuedouble > 65535 || port->valuedouble != (uint16_t)port->valuedouble) {
		cJSON_Delete(json);
		return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid configuration value");
	}
	strlcpy(config.ssid, ssid->valuestring, sizeof(config.ssid));
	strlcpy(config.password, password->valuestring, sizeof(config.password));
	strlcpy(config.country, country->valuestring, sizeof(config.country));
	for (size_t i = 0; i < 2; ++i) config.country[i] = (char)toupper((unsigned char)config.country[i]);
	if (!supported_country(config.country)) {
		cJSON_Delete(json);
		return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unsupported country code");
	}
	strlcpy(config.server, server_name->valuestring, sizeof(config.server));
	config.server_port = (uint16_t)port->valuedouble;
	config.configured = config.ssid[0] != '\0';
	cJSON_Delete(json);
	if (app_config_save(&config) != ESP_OK) {
		return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid or unsaved configuration");
	}
	httpd_resp_sendstr(request, "Saved; rebooting into 5 GHz-only STA mode");
	xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
	return ESP_OK;
}

static esp_err_t group_handler(httpd_req_t *request)
{
	cJSON *json;
	esp_err_t result = receive_json(request, &json);
	if (result != ESP_OK) return result;
	cJSON *secondary = cJSON_GetObjectItemCaseSensitive(json, "secondary");
	cJSON *group = cJSON_GetObjectItemCaseSensitive(json, "groupId");
	struct sv_receiver_set_group_request command = {.command = SV_CMD_SET_GROUP};
	if (!cJSON_IsBool(secondary) || !cJSON_IsString(group) ||
		!sv_id48_parse(command.group_id, group->valuestring)) {
		cJSON_Delete(json);
		return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Expected secondary and 12-digit groupId");
	}
	command.secondary = cJSON_IsTrue(secondary);
	cJSON_Delete(json);
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	size_t response_length = sizeof(response);
	result = bridge_spi_command((const uint8_t *)&command, sizeof(command), response,
		&response_length, pdMS_TO_TICKS(400));
	if (result != ESP_OK) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Receiver rejected group");
	return httpd_resp_sendstr(request, "OK");
}

static esp_err_t receiver_action_handler(httpd_req_t *request)
{
	uint8_t command[] = {(uint8_t)(uintptr_t)request->user_ctx};
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	size_t response_length = sizeof(response);
	if (bridge_spi_command(command, sizeof(command), response, &response_length,
		pdMS_TO_TICKS(400)) != ESP_OK) {
		return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Receiver command failed");
	}
	return httpd_resp_sendstr(request, "OK");
}

static esp_err_t trackers_handler(httpd_req_t *request)
{
	char query[64] = {0};
	char page_text[8] = "0";
	if (httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK) {
		(void)httpd_query_key_value(query, "page", page_text, sizeof(page_text));
	}
	uint8_t command[] = {SV_CMD_GET_TRACKER_PAGE, (uint8_t)strtoul(page_text, NULL, 10)};
	uint8_t response[SV_BRIDGE_PAYLOAD_SIZE];
	size_t response_length = sizeof(response);
	if (bridge_spi_command(command, sizeof(command), response, &response_length,
		pdMS_TO_TICKS(400)) != ESP_OK || response_length < 4) {
		return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Receiver query failed");
	}
	struct sv_receiver_tracker_page_response page = {0};
	memcpy(&page, response, response_length < sizeof(page) ? response_length : sizeof(page));
	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "page", page.page);
	cJSON *entries = cJSON_AddArrayToObject(root, "trackers");
	for (uint8_t i = 0; i < page.count && i < 8; ++i) {
		char address[13];
		sv_id48_format(address, page.entries[i].address);
		cJSON *entry = cJSON_CreateObject();
		cJSON_AddNumberToObject(entry, "id", page.entries[i].id);
		cJSON_AddStringToObject(entry, "address", address);
		cJSON_AddItemToArray(entries, entry);
	}
	return json_response(request, root);
}

static void dns_task(void *argument)
{
	(void)argument;
	while (!wifi_manager_is_setup_ap()) vTaskDelay(pdMS_TO_TICKS(100));
	int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in bind_address = {
		.sin_family = AF_INET,
		.sin_port = htons(53),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	if (socket_fd < 0 || bind(socket_fd, (struct sockaddr *)&bind_address, sizeof(bind_address)) != 0) {
		ESP_LOGE(TAG, "Unable to start captive DNS");
		if (socket_fd >= 0) close(socket_fd);
		vTaskDelete(NULL);
		return;
	}
	uint8_t packet[512];
	while (wifi_manager_is_setup_ap()) {
		struct sockaddr_in client;
		socklen_t client_length = sizeof(client);
		int length = recvfrom(socket_fd, packet, sizeof(packet), 0,
			(struct sockaddr *)&client, &client_length);
		if (length < 17 || packet[4] != 0 || packet[5] != 1) continue;
		size_t cursor = 12;
		while (cursor < (size_t)length && packet[cursor] != 0) cursor += packet[cursor] + 1u;
		if (cursor + 5u > (size_t)length || cursor + 16u > sizeof(packet)) continue;
		cursor += 5;
		packet[2] = 0x81; packet[3] = 0x80;
		packet[6] = 0; packet[7] = 1;
		const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4, 192, 168, 4, 1};
		memcpy(&packet[cursor], answer, sizeof(answer));
		(void)sendto(socket_fd, packet, cursor + sizeof(answer), 0,
			(struct sockaddr *)&client, client_length);
	}
	close(socket_fd);
	vTaskDelete(NULL);
}

static void register_uri(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *), uintptr_t context)
{
	httpd_uri_t endpoint = {.uri = uri, .method = method, .handler = handler, .user_ctx = (void *)context};
	ESP_ERROR_CHECK(httpd_register_uri_handler(server, &endpoint));
}

int web_portal_init(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.max_uri_handlers = 20;
	ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "HTTP server");
	register_uri("/", HTTP_GET, index_handler, 0);
	register_uri("/api/status", HTTP_GET, status_handler, 0);
	register_uri("/api/config", HTTP_POST, config_handler, 0);
	register_uri("/api/trackers", HTTP_GET, trackers_handler, 0);
	register_uri("/api/receiver/group", HTTP_POST, group_handler, 0);
	register_uri("/api/receiver/pair/start", HTTP_POST, receiver_action_handler, SV_CMD_START_PAIRING);
	register_uri("/api/receiver/pair/stop", HTTP_POST, receiver_action_handler, SV_CMD_STOP_PAIRING);
	register_uri("/api/receiver/clear", HTTP_POST, receiver_action_handler, SV_CMD_CLEAR_TRACKERS);
	register_uri("/api/receiver/reboot", HTTP_POST, receiver_action_handler, SV_CMD_REBOOT);
	register_uri("/generate_204", HTTP_GET, index_handler, 0);
	register_uri("/hotspot-detect.html", HTTP_GET, index_handler, 0);
	if (xTaskCreate(dns_task, "captive-dns", 3072, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
	return ESP_OK;
}
