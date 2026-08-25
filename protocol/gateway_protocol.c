/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#include "gateway_protocol.h"

#include "bridge_protocol.h"

#include <string.h>

static const uint8_t gateway_magic[4] = {'S', 'V', 'W', '1'};

static uint16_t read_le16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_le16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

size_t sv_gateway_encode(uint8_t output[SV_GATEWAY_MAX_DATAGRAM],
	const struct sv_gateway_message *message)
{
	uint16_t payload_length = message->payload_length;
	if (message->kind < SV_GATEWAY_DISCOVER || message->kind > SV_GATEWAY_REPORT_BATCH ||
		payload_length > SV_GATEWAY_MAX_PAYLOAD ||
		message->report_count > SV_GATEWAY_MAX_REPORTS ||
		(message->kind == SV_GATEWAY_REPORT_BATCH &&
		 payload_length != message->report_count * SV_SMOL_REPORT_SIZE) ||
		(message->kind != SV_GATEWAY_REPORT_BATCH &&
		 (payload_length != 0 || message->report_count != 0))) {
		return 0;
	}

	const size_t total = SV_GATEWAY_HEADER_SIZE + payload_length + 4u;
	memset(output, 0, total);
	memcpy(output, gateway_magic, sizeof(gateway_magic));
	output[4] = SV_GATEWAY_VERSION;
	output[5] = message->kind;
	output[6] = message->flags;
	output[7] = message->report_count;
	memcpy(&output[8], message->gateway_id, sizeof(message->gateway_id));
	write_le32(&output[16], message->boot_id);
	write_le32(&output[20], message->sequence);
	write_le16(&output[24], payload_length);
	write_le16(&output[26], SV_GATEWAY_HEADER_SIZE);
	memcpy(&output[SV_GATEWAY_HEADER_SIZE], message->payload, payload_length);
	write_le32(&output[SV_GATEWAY_HEADER_SIZE + payload_length],
		sv_crc32_ieee(output, SV_GATEWAY_HEADER_SIZE + payload_length));
	return total;
}

bool sv_gateway_decode(struct sv_gateway_message *message,
	const uint8_t *input, size_t length)
{
	if (length < SV_GATEWAY_HEADER_SIZE + 4u ||
		memcmp(input, gateway_magic, sizeof(gateway_magic)) != 0 ||
		input[4] != SV_GATEWAY_VERSION || input[5] < SV_GATEWAY_DISCOVER ||
		input[5] > SV_GATEWAY_REPORT_BATCH || input[7] > SV_GATEWAY_MAX_REPORTS ||
		read_le16(&input[26]) != SV_GATEWAY_HEADER_SIZE) {
		return false;
	}

	const uint16_t payload_length = read_le16(&input[24]);
	if (payload_length > SV_GATEWAY_MAX_PAYLOAD ||
		length != SV_GATEWAY_HEADER_SIZE + payload_length + 4u ||
		read_le32(&input[SV_GATEWAY_HEADER_SIZE + payload_length]) !=
			sv_crc32_ieee(input, SV_GATEWAY_HEADER_SIZE + payload_length) ||
		(input[5] == SV_GATEWAY_REPORT_BATCH &&
		 payload_length != input[7] * SV_SMOL_REPORT_SIZE) ||
		(input[5] != SV_GATEWAY_REPORT_BATCH &&
		 (payload_length != 0 || input[7] != 0))) {
		return false;
	}

	memset(message, 0, sizeof(*message));
	message->kind = input[5];
	message->flags = input[6];
	message->report_count = input[7];
	memcpy(message->gateway_id, &input[8], sizeof(message->gateway_id));
	message->boot_id = read_le32(&input[16]);
	message->sequence = read_le32(&input[20]);
	message->payload_length = payload_length;
	memcpy(message->payload, &input[SV_GATEWAY_HEADER_SIZE], payload_length);
	return true;
}
