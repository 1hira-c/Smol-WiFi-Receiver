/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "bridge_spi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define STA_CONNECT_TIMEOUT_MS 30000
#define SETUP_PASSWORD "smol-wifi-setup"

static const char *TAG = "wifi";
static EventGroupHandle_t events;
static esp_netif_t *sta_netif;
static esp_netif_t *ap_netif;
static bool setup_ap;
static bool wifi_started;
static char setup_ssid[24];

static void event_handler(void *argument, esp_event_base_t base, int32_t id, void *data)
{
	(void)argument;
	(void)data;
	if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
		xEventGroupClearBits(events, WIFI_CONNECTED_BIT);
		if (!setup_ap) (void)esp_wifi_connect();
	} else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
		xEventGroupSetBits(events, WIFI_CONNECTED_BIT);
	}
}

static int start_setup_ap(void)
{
	struct app_config config;
	app_config_get(&config);
	setup_ap = true;
	(void)bridge_spi_set_radio_enabled(false);
	if (wifi_started) {
		ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));
		ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
		wifi_started = false;
	}
	if (sta_netif) {
		esp_netif_destroy_default_wifi(sta_netif);
		sta_netif = NULL;
	}
	ap_netif = esp_netif_create_default_wifi_ap();
	if (!ap_netif) return ESP_ERR_NO_MEM;
	uint8_t mac[6];
	ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
	snprintf(setup_ssid, sizeof(setup_ssid), "SmolReceiver-Setup-%02X%02X", mac[4], mac[5]);
	wifi_config_t wifi = {0};
	strlcpy((char *)wifi.ap.ssid, setup_ssid, sizeof(wifi.ap.ssid));
	strlcpy((char *)wifi.ap.password, SETUP_PASSWORD, sizeof(wifi.ap.password));
	wifi.ap.ssid_len = strlen(setup_ssid);
	wifi.ap.channel = 1;
	wifi.ap.max_connection = 4;
	wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
	ESP_ERROR_CHECK(esp_wifi_set_country_code(config.country, true));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi));
	ESP_ERROR_CHECK(esp_wifi_start());
	wifi_started = true;
	ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_2G_ONLY));
	ESP_LOGW(TAG, "Setup AP %s started; receiver RF paused", setup_ssid);
	return ESP_OK;
}

static void connection_supervisor(void *argument)
{
	(void)argument;
	struct app_config config;
	app_config_get(&config);
	if (!config.configured) {
		(void)start_setup_ap();
		vTaskDelete(NULL);
		return;
	}
	EventBits_t bits = xEventGroupWaitBits(events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
		pdMS_TO_TICKS(STA_CONNECT_TIMEOUT_MS));
	if (!(bits & WIFI_CONNECTED_BIT)) (void)start_setup_ap();
	vTaskDelete(NULL);
}

int wifi_manager_init(void)
{
	events = xEventGroupCreate();
	if (!events) return ESP_ERR_NO_MEM;
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&init));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

	struct app_config config;
	app_config_get(&config);
	if (config.configured) {
		sta_netif = esp_netif_create_default_wifi_sta();
		if (!sta_netif) return ESP_ERR_NO_MEM;
		wifi_config_t wifi = {0};
		strlcpy((char *)wifi.sta.ssid, config.ssid, sizeof(wifi.sta.ssid));
		strlcpy((char *)wifi.sta.password, config.password, sizeof(wifi.sta.password));
		wifi.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
		ESP_ERROR_CHECK(esp_wifi_set_country_code(config.country, true));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
		ESP_ERROR_CHECK(esp_wifi_start());
		wifi_started = true;
		ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY));
		ESP_ERROR_CHECK(esp_wifi_connect());
	}
	if (xTaskCreate(connection_supervisor, "wifi-supervisor", 4096, NULL, 8, NULL) != pdPASS) {
		return ESP_ERR_NO_MEM;
	}
	return ESP_OK;
}

EventGroupHandle_t wifi_manager_event_group(void) { return events; }
bool wifi_manager_is_connected(void)
{
	return events && (xEventGroupGetBits(events) & WIFI_CONNECTED_BIT) != 0;
}
bool wifi_manager_is_setup_ap(void) { return setup_ap; }
const char *wifi_manager_setup_ssid(void) { return setup_ssid; }
