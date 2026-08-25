/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#ifndef SLIMEVR_BRIDGE_PROTOCOL_H
#define SLIMEVR_BRIDGE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SV_BRIDGE_VERSION 1u
#define SV_BRIDGE_PAYLOAD_SIZE 64u
#define SV_BRIDGE_ENVELOPE_SIZE 80u

#define SV_BRIDGE_FLAG_MORE_PENDING (1u << 0)
#define SV_BRIDGE_FLAG_ERROR        (1u << 1)

enum sv_bridge_kind {
	SV_BRIDGE_IDLE = 0,
	SV_BRIDGE_RECEIVER_REPORT = 1,
	SV_BRIDGE_COMMAND = 2,
	SV_BRIDGE_COMMAND_RESPONSE = 3,
	SV_BRIDGE_STATUS_EVENT = 4,
};

enum sv_bridge_command {
	SV_CMD_GET_STATUS = 1,
	SV_CMD_GET_TRACKER_PAGE = 2,
	SV_CMD_SET_GROUP = 3,
	SV_CMD_START_PAIRING = 4,
	SV_CMD_STOP_PAIRING = 5,
	SV_CMD_CLEAR_TRACKERS = 6,
	SV_CMD_REBOOT = 7,
	SV_CMD_SET_RADIO_ENABLED = 8,
};

enum sv_bridge_status {
	SV_STATUS_OK = 0,
	SV_STATUS_BAD_REQUEST = 1,
	SV_STATUS_NOT_ALLOWED = 2,
	SV_STATUS_NOT_FOUND = 3,
	SV_STATUS_BUSY = 4,
	SV_STATUS_INTERNAL_ERROR = 5,
};

struct sv_bridge_message {
	uint8_t kind;
	uint8_t flags;
	uint8_t payload_length;
	uint16_t sequence;
	uint16_t request_id;
	uint8_t payload[SV_BRIDGE_PAYLOAD_SIZE];
};

uint32_t sv_crc32_ieee(const uint8_t *data, size_t length);
void sv_bridge_encode(uint8_t output[SV_BRIDGE_ENVELOPE_SIZE],
	const struct sv_bridge_message *message);
bool sv_bridge_decode(struct sv_bridge_message *message,
	const uint8_t input[SV_BRIDGE_ENVELOPE_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
