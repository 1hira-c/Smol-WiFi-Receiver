/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct gateway_udp_stats {
	bool server_connected;
	uint32_t datagrams_sent;
	uint32_t reports_sent;
	uint32_t reports_discarded;
	uint32_t receive_errors;
	uint32_t last_ack_age_ms;
};

int gateway_udp_init(void);
void gateway_udp_get_stats(struct gateway_udp_stats *stats);
