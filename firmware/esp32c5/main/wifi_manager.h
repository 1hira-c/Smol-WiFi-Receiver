/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0

int wifi_manager_init(void);
EventGroupHandle_t wifi_manager_event_group(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_setup_ap(void);
const char *wifi_manager_setup_ssid(void);
