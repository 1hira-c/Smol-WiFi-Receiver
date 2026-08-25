/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define APP_DEFAULT_SERVER_PORT 6969u

struct app_config {
	bool configured;
	char ssid[33];
	char password[65];
	char country[3];
	char server[65];
	uint16_t server_port;
};

int app_config_init(void);
void app_config_get(struct app_config *config);
int app_config_save(const struct app_config *config);
