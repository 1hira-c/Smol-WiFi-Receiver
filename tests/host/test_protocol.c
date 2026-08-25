/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bridge_protocol.h"
#include "command_replay_cache.h"
#include "gateway_protocol.h"
#include "golden_vectors.h"
#include "id48_codec.h"

static uint32_t crc32k(uint32_t crc, const uint8_t *data, size_t length)
{
	for (size_t index = 0; index < length; ++index) {
		crc ^= (uint32_t)data[index] << 24;
		for (unsigned bit = 0; bit < 8; ++bit) {
			crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x93a409ebu : crc << 1;
		}
	}
	return crc;
}

static void test_crc_and_rf_golden(void)
{
	assert(sv_crc32_ieee((const uint8_t *)"123456789", 9) == 0xcbf43926u);
	const uint8_t packet[] = SV_GOLDEN_RF_V2_BYTES;
	assert(sizeof(packet) == 28);
	uint32_t expected = (uint32_t)packet[24] | ((uint32_t)packet[25] << 8) |
		((uint32_t)packet[26] << 16) | ((uint32_t)packet[27] << 24);
	assert(packet[23] == 2);
	assert(crc32k(0x93a409ebu, packet, 24) == expected);
}

static void test_bridge_golden_and_malformed(void)
{
	const uint8_t expected[] = SV_GOLDEN_BRIDGE_COMMAND_BYTES;
	assert(sizeof(expected) == SV_BRIDGE_ENVELOPE_SIZE);
	struct sv_bridge_message command = {
		.kind = SV_BRIDGE_COMMAND,
		.payload_length = 2,
		.sequence = UINT16_MAX,
		.request_id = 0x1234,
		.payload = {SV_CMD_SET_RADIO_ENABLED, 0},
	};
	uint8_t encoded[SV_BRIDGE_ENVELOPE_SIZE];
	sv_bridge_encode(encoded, &command);
	assert(memcmp(encoded, expected, sizeof(encoded)) == 0);
	struct sv_bridge_message decoded;
	assert(sv_bridge_decode(&decoded, encoded));
	assert(decoded.sequence == UINT16_MAX && decoded.request_id == 0x1234);
	command.sequence++;
	sv_bridge_encode(encoded, &command);
	assert(sv_bridge_decode(&decoded, encoded) && decoded.sequence == 0);
	encoded[7] = 65;
	assert(!sv_bridge_decode(&decoded, encoded));
	memcpy(encoded, expected, sizeof(encoded));
	encoded[5] = 0xff;
	assert(!sv_bridge_decode(&decoded, encoded));
}

static void test_gateway_golden(void)
{
	const uint8_t expected[] = SV_GOLDEN_GATEWAY_HELLO_BYTES;
	assert(sizeof(expected) == 32);
	struct sv_gateway_message hello = {
		.kind = SV_GATEWAY_HELLO,
		.gateway_id = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
		.boot_id = 0x78563412,
		.sequence = UINT32_MAX,
	};
	uint8_t encoded[SV_GATEWAY_MAX_DATAGRAM];
	assert(sv_gateway_encode(encoded, &hello) == sizeof(expected));
	assert(memcmp(encoded, expected, sizeof(expected)) == 0);
	struct sv_gateway_message decoded;
	assert(sv_gateway_decode(&decoded, encoded, sizeof(expected)));
	encoded[10] ^= 1;
	assert(!sv_gateway_decode(&decoded, encoded, sizeof(expected)));
	hello.report_count = 1;
	assert(sv_gateway_encode(encoded, &hello) == 0);
}

static void test_command_idempotency(void)
{
	struct sv_command_replay_cache cache;
	sv_command_replay_init(&cache);
	struct sv_bridge_message response = {
		.kind = SV_BRIDGE_COMMAND_RESPONSE,
		.request_id = 65535,
		.payload_length = 2,
		.payload = {SV_CMD_CLEAR_TRACKERS, SV_STATUS_OK},
	};
	sv_command_replay_store(&cache, response.request_id, &response);
	struct sv_bridge_message replay;
	assert(sv_command_replay_find(&cache, 65535, &replay));
	assert(replay.payload[0] == SV_CMD_CLEAR_TRACKERS && replay.payload[1] == SV_STATUS_OK);
	assert(!sv_command_replay_find(&cache, 0, &replay));
}

static void test_id48_canonical_text(void)
{
	const uint8_t little_endian[] = {0xf5, 0xa5, 0xe8, 0x6f, 0x04, 0xa5};
	uint8_t decoded[SV_ID48_BYTE_SIZE] = {0};
	char text[SV_ID48_TEXT_SIZE];
	sv_id48_format(text, little_endian);
	assert(strcmp(text, "a5046fe8a5f5") == 0);
	assert(sv_id48_parse(decoded, "A5046FE8A5F5"));
	assert(memcmp(decoded, little_endian, sizeof(decoded)) == 0);
	assert(!sv_id48_parse(decoded, "a5046fe8a5fg"));
	assert(!sv_id48_parse(decoded, "a5046fe8a5f"));
}

int main(void)
{
	test_crc_and_rf_golden();
	test_bridge_golden_and_malformed();
	test_gateway_golden();
	test_command_idempotency();
	test_id48_canonical_text();
	puts("protocol tests passed");
	return 0;
}
