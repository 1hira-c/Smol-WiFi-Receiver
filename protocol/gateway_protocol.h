/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#ifndef SLIMEVR_GATEWAY_PROTOCOL_H
#define SLIMEVR_GATEWAY_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "smol_report.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SV_GATEWAY_VERSION 1u
#define SV_GATEWAY_HEADER_SIZE 28u
#define SV_GATEWAY_MAX_REPORTS 8u
#define SV_GATEWAY_MAX_PAYLOAD (SV_GATEWAY_MAX_REPORTS * SV_SMOL_REPORT_SIZE)
#define SV_GATEWAY_MAX_DATAGRAM (SV_GATEWAY_HEADER_SIZE + SV_GATEWAY_MAX_PAYLOAD + 4u)

enum sv_gateway_kind {
	SV_GATEWAY_DISCOVER = 1,
	SV_GATEWAY_OFFER = 2,
	SV_GATEWAY_HELLO = 3,
	SV_GATEWAY_ACK = 4,
	SV_GATEWAY_REPORT_BATCH = 5,
};

struct sv_gateway_message {
	uint8_t kind;
	uint8_t flags;
	uint8_t report_count;
	uint8_t gateway_id[6];
	uint32_t boot_id;
	uint32_t sequence;
	uint16_t payload_length;
	uint8_t payload[SV_GATEWAY_MAX_PAYLOAD];
};

size_t sv_gateway_encode(uint8_t output[SV_GATEWAY_MAX_DATAGRAM],
	const struct sv_gateway_message *message);
bool sv_gateway_decode(struct sv_gateway_message *message,
	const uint8_t *input, size_t length);

#ifdef __cplusplus
}
#endif

#endif
