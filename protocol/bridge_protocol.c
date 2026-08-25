/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#include "bridge_protocol.h"

#include <string.h>

static const uint8_t bridge_magic[4] = {'S', 'V', 'B', '1'};

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

uint32_t sv_crc32_ieee(const uint8_t *data, size_t length)
{
	uint32_t crc = UINT32_MAX;
	for (size_t i = 0; i < length; ++i) {
		crc ^= data[i];
		for (unsigned bit = 0; bit < 8; ++bit) {
			uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
			crc = (crc >> 1) ^ (0xedb88320u & mask);
		}
	}
	return ~crc;
}

void sv_bridge_encode(uint8_t output[SV_BRIDGE_ENVELOPE_SIZE],
	const struct sv_bridge_message *message)
{
	memset(output, 0, SV_BRIDGE_ENVELOPE_SIZE);
	memcpy(output, bridge_magic, sizeof(bridge_magic));
	output[4] = SV_BRIDGE_VERSION;
	output[5] = message->kind;
	output[6] = message->flags;
	output[7] = message->payload_length <= SV_BRIDGE_PAYLOAD_SIZE ?
		message->payload_length : SV_BRIDGE_PAYLOAD_SIZE;
	write_le16(&output[8], message->sequence);
	write_le16(&output[10], message->request_id);
	memcpy(&output[12], message->payload, output[7]);
	write_le32(&output[76], sv_crc32_ieee(output, 76));
}

bool sv_bridge_decode(struct sv_bridge_message *message,
	const uint8_t input[SV_BRIDGE_ENVELOPE_SIZE])
{
	if (memcmp(input, bridge_magic, sizeof(bridge_magic)) != 0 ||
		input[4] != SV_BRIDGE_VERSION || input[5] > SV_BRIDGE_STATUS_EVENT ||
		input[7] > SV_BRIDGE_PAYLOAD_SIZE ||
		read_le32(&input[76]) != sv_crc32_ieee(input, 76)) {
		return false;
	}

	memset(message, 0, sizeof(*message));
	message->kind = input[5];
	message->flags = input[6];
	message->payload_length = input[7];
	message->sequence = read_le16(&input[8]);
	message->request_id = read_le16(&input[10]);
	memcpy(message->payload, &input[12], message->payload_length);
	return true;
}
