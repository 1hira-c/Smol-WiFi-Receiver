/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "smol_report.h"

struct bridge_stats {
	uint32_t transfers;
	uint32_t transfer_errors;
	uint32_t reports;
	uint32_t decode_errors;
	uint32_t queue_drops;
	uint32_t command_timeouts;
};

int bridge_spi_init(void);
bool bridge_spi_receive_report(uint8_t report[SV_SMOL_REPORT_SIZE], TickType_t timeout);
int bridge_spi_command(const uint8_t *request, size_t request_length,
	uint8_t *response, size_t *response_length, TickType_t timeout);
int bridge_spi_set_radio_enabled(bool enabled);
void bridge_spi_get_stats(struct bridge_stats *stats);
