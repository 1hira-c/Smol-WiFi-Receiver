/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "app_config.h"
#include "bridge_spi.h"
#include "esp_check.h"
#include "esp_log.h"
#include "gateway_udp.h"
#include "status_metrics.h"
#include "web_portal.h"
#include "wifi_manager.h"

static const char *TAG = "smol-gateway";

void app_main(void)
{
	ESP_ERROR_CHECK(app_config_init());
	ESP_ERROR_CHECK(bridge_spi_init());
	ESP_ERROR_CHECK(wifi_manager_init());
	ESP_ERROR_CHECK(gateway_udp_init());
	ESP_ERROR_CHECK(web_portal_init());
	ESP_ERROR_CHECK(status_metrics_init());
	ESP_LOGI(TAG, "SlimeVR Smol Wi-Fi gateway started");
}
