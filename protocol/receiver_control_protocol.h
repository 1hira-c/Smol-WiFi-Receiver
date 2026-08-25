/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#ifndef SLIMEVR_RECEIVER_CONTROL_PROTOCOL_H
#define SLIMEVR_RECEIVER_CONTROL_PROTOCOL_H

#include <stdint.h>

#define SV_RECEIVER_CONTROL_VERSION 1u
#define SV_RECEIVER_STATUS_PRIMARY (1u << 0)
#define SV_RECEIVER_STATUS_PAIRING (1u << 1)
#define SV_RECEIVER_STATUS_RADIO_ENABLED (1u << 2)

#if defined(__GNUC__)
#define SV_PACKED __attribute__((packed))
#else
#define SV_PACKED
#endif

struct SV_PACKED sv_receiver_status_response {
	uint8_t command;
	uint8_t status;
	uint8_t protocol_version;
	uint8_t flags;
	uint8_t receiver_id[6];
	uint8_t group_id[6];
	uint16_t stored_trackers;
	uint32_t uptime_ms;
	uint32_t rf_received;
	uint32_t rf_crc_errors;
	uint32_t report_queue_drops;
	uint32_t spi_crc_errors;
};

struct SV_PACKED sv_receiver_set_group_request {
	uint8_t command;
	uint8_t secondary;
	uint8_t group_id[6];
};

struct SV_PACKED sv_receiver_tracker_entry {
	uint8_t id;
	uint8_t address[6];
};

struct SV_PACKED sv_receiver_tracker_page_response {
	uint8_t command;
	uint8_t status;
	uint8_t page;
	uint8_t count;
	struct sv_receiver_tracker_entry entries[8];
};

#endif
