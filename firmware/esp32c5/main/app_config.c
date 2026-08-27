/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "app_config.h"

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_NAMESPACE "smol-gateway"
#define CONFIG_SCHEMA_VERSION 1u
#define LEGACY_DYNAMIC_SERVER_PORT 6969u

static struct app_config current;
static portMUX_TYPE config_lock = portMUX_INITIALIZER_UNLOCKED;
static const char *TAG = "config";

static void defaults(void)
{
	memset(&current, 0, sizeof(current));
	memcpy(current.country, "JP", 3);
	current.server_port = APP_DEFAULT_SERVER_PORT;
}

static void read_string(nvs_handle_t handle, const char *key, char *value, size_t capacity)
{
	size_t length = capacity;
	if (nvs_get_str(handle, key, value, &length) != ESP_OK) {
		value[0] = '\0';
	}
}

int app_config_init(void)
{
	defaults();
	esp_err_t error = nvs_flash_init();
	if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		error = nvs_flash_init();
	}
	if (error != ESP_OK) return error;

	nvs_handle_t handle;
	if (nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return ESP_OK;
	uint8_t configured = 0;
	(void)nvs_get_u8(handle, "configured", &configured);
	current.configured = configured != 0;
	read_string(handle, "ssid", current.ssid, sizeof(current.ssid));
	read_string(handle, "password", current.password, sizeof(current.password));
	read_string(handle, "country", current.country, sizeof(current.country));
	read_string(handle, "server", current.server, sizeof(current.server));
	(void)nvs_get_u16(handle, "server_port", &current.server_port);
	uint8_t schema_version = 0;
	(void)nvs_get_u8(handle, "schema_version", &schema_version);
	nvs_close(handle);
	if (strlen(current.country) != 2) memcpy(current.country, "JP", 3);
	if (current.server_port == 0) current.server_port = APP_DEFAULT_SERVER_PORT;
	if (schema_version < CONFIG_SCHEMA_VERSION) {
		bool migrate_dynamic_port = current.server[0] == '\0' &&
			current.server_port == LEGACY_DYNAMIC_SERVER_PORT;
		if (migrate_dynamic_port) current.server_port = APP_DEFAULT_SERVER_PORT;
		error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
		if (error != ESP_OK) return error;
		if (migrate_dynamic_port) error = nvs_set_u16(handle, "server_port", current.server_port);
		if (error == ESP_OK) error = nvs_set_u8(handle, "schema_version", CONFIG_SCHEMA_VERSION);
		if (error == ESP_OK) error = nvs_commit(handle);
		nvs_close(handle);
		if (error != ESP_OK) return error;
		if (migrate_dynamic_port) {
			ESP_LOGI(TAG, "Migrated dynamic Bridge port from %u to %u",
				LEGACY_DYNAMIC_SERVER_PORT, APP_DEFAULT_SERVER_PORT);
		}
	}
	return ESP_OK;
}

void app_config_get(struct app_config *config)
{
	taskENTER_CRITICAL(&config_lock);
	*config = current;
	taskEXIT_CRITICAL(&config_lock);
}

int app_config_save(const struct app_config *config)
{
	if (config == NULL || strlen(config->ssid) > 32 || strlen(config->password) > 64 ||
		strlen(config->country) != 2 || strlen(config->server) > 64 || config->server_port == 0) {
		return ESP_ERR_INVALID_ARG;
	}
	nvs_handle_t handle;
	esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
	if (error != ESP_OK) return error;
	if ((error = nvs_set_u8(handle, "configured", config->configured ? 1 : 0)) == ESP_OK &&
		(error = nvs_set_str(handle, "ssid", config->ssid)) == ESP_OK &&
		(error = nvs_set_str(handle, "password", config->password)) == ESP_OK &&
		(error = nvs_set_str(handle, "country", config->country)) == ESP_OK &&
		(error = nvs_set_str(handle, "server", config->server)) == ESP_OK &&
		(error = nvs_set_u16(handle, "server_port", config->server_port)) == ESP_OK &&
		(error = nvs_set_u8(handle, "schema_version", CONFIG_SCHEMA_VERSION)) == ESP_OK) {
		error = nvs_commit(handle);
	}
	nvs_close(handle);
	if (error == ESP_OK) {
		taskENTER_CRITICAL(&config_lock);
		current = *config;
		taskEXIT_CRITICAL(&config_lock);
	}
	return error;
}
