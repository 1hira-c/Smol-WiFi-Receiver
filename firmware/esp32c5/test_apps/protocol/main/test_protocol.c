/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "unity.h"

#include <string.h>

#include "bridge_protocol.h"
#include "command_replay_cache.h"
#include "gateway_protocol.h"
#include "golden_vectors.h"
#include "id48_codec.h"

TEST_CASE("bridge CRC and malformed length", "[smol]")
{
	struct sv_bridge_message message = {.kind = SV_BRIDGE_COMMAND, .payload_length = 1};
	uint8_t encoded[SV_BRIDGE_ENVELOPE_SIZE];
	const uint8_t golden[] = SV_GOLDEN_BRIDGE_COMMAND_BYTES;
	struct sv_bridge_message decoded;
	message.sequence = UINT16_MAX;
	message.request_id = 0x1234;
	message.payload_length = 2;
	message.payload[0] = SV_CMD_SET_RADIO_ENABLED;
	message.payload[1] = 0;
	sv_bridge_encode(encoded, &message);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(golden, encoded, sizeof(golden));
	TEST_ASSERT_TRUE(sv_bridge_decode(&decoded, encoded));
	encoded[7] = 65;
	TEST_ASSERT_FALSE(sv_bridge_decode(&decoded, encoded));
}

TEST_CASE("command retries reuse cached response", "[smol]")
{
	struct sv_command_replay_cache cache;
	struct sv_bridge_message response = {.kind = SV_BRIDGE_COMMAND_RESPONSE, .request_id = 12};
	sv_command_replay_init(&cache);
	sv_command_replay_store(&cache, 12, &response);
	TEST_ASSERT_TRUE(sv_command_replay_find(&cache, 12, &response));
	TEST_ASSERT_EQUAL_UINT16(12, response.request_id);
}

TEST_CASE("gateway CRC rejects mutation", "[smol]")
{
	struct sv_gateway_message message = {.kind = SV_GATEWAY_HELLO};
	const uint8_t golden[] = SV_GOLDEN_GATEWAY_HELLO_BYTES;
	const uint8_t report_golden[] = SV_GOLDEN_SMOL_REPORT_BYTES;
	const uint8_t gateway_id[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
	memcpy(message.gateway_id, gateway_id, sizeof(gateway_id));
	message.boot_id = 0x78563412;
	message.sequence = UINT32_MAX;
	uint8_t encoded[SV_GATEWAY_MAX_DATAGRAM];
	size_t length = sv_gateway_encode(encoded, &message);
	TEST_ASSERT_EQUAL(sizeof(golden), length);
	TEST_ASSERT_EQUAL_HEX8_ARRAY(golden, encoded, sizeof(golden));
	TEST_ASSERT_EQUAL(SV_SMOL_REPORT_SIZE, sizeof(report_golden));
	struct sv_gateway_message decoded;
	TEST_ASSERT_TRUE(sv_gateway_decode(&decoded, encoded, length));
	encoded[9] ^= 1;
	TEST_ASSERT_FALSE(sv_gateway_decode(&decoded, encoded, length));
}

TEST_CASE("48-bit IDs use canonical numeric order", "[smol]")
{
	const uint8_t little_endian[] = {0xf5, 0xa5, 0xe8, 0x6f, 0x04, 0xa5};
	uint8_t decoded[SV_ID48_BYTE_SIZE];
	char text[SV_ID48_TEXT_SIZE];
	sv_id48_format(text, little_endian);
	TEST_ASSERT_EQUAL_STRING("a5046fe8a5f5", text);
	TEST_ASSERT_TRUE(sv_id48_parse(decoded, "A5046FE8A5F5"));
	TEST_ASSERT_EQUAL_HEX8_ARRAY(little_endian, decoded, sizeof(decoded));
}

void app_main(void)
{
	unity_run_menu();
}
