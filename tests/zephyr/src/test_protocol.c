/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include <zephyr/ztest.h>

#include "bridge_protocol.h"
#include "command_replay_cache.h"
#include "gateway_protocol.h"
#include "golden_vectors.h"
#include "id48_codec.h"

ZTEST(smol_protocol, test_bridge_sequence_wrap_and_length)
{
	struct sv_bridge_message input = {
		.kind = SV_BRIDGE_COMMAND,
		.payload_length = 1,
		.sequence = UINT16_MAX,
		.request_id = 9,
		.payload = {SV_CMD_GET_STATUS},
	};
	uint8_t encoded[SV_BRIDGE_ENVELOPE_SIZE];
	const uint8_t golden[] = SV_GOLDEN_BRIDGE_COMMAND_BYTES;
	struct sv_bridge_message output;
	sv_bridge_encode(encoded, &input);
	input.payload_length = 2;
	input.payload[0] = SV_CMD_SET_RADIO_ENABLED;
	input.payload[1] = 0;
	input.request_id = 0x1234;
	sv_bridge_encode(encoded, &input);
	zassert_mem_equal(encoded, golden, sizeof(golden));
	input.payload_length = 1;
	input.payload[0] = SV_CMD_GET_STATUS;
	input.request_id = 9;
	sv_bridge_encode(encoded, &input);
	zassert_true(sv_bridge_decode(&output, encoded));
	zassert_equal(output.sequence, UINT16_MAX);
	input.sequence++;
	sv_bridge_encode(encoded, &input);
	zassert_true(sv_bridge_decode(&output, encoded));
	zassert_equal(output.sequence, 0);
	encoded[7] = 65;
	zassert_false(sv_bridge_decode(&output, encoded));
}

ZTEST(smol_protocol, test_command_replay_is_idempotent)
{
	struct sv_command_replay_cache cache;
	sv_command_replay_init(&cache);
	struct sv_bridge_message response = {.kind = SV_BRIDGE_COMMAND_RESPONSE, .request_id = 42};
	response.payload[0] = SV_CMD_CLEAR_TRACKERS;
	response.payload_length = 1;
	sv_command_replay_store(&cache, 42, &response);
	response.payload[0] = 0;
	zassert_true(sv_command_replay_find(&cache, 42, &response));
	zassert_equal(response.payload[0], SV_CMD_CLEAR_TRACKERS);
}

ZTEST(smol_protocol, test_gateway_crc_rejection)
{
	struct sv_gateway_message input = {.kind = SV_GATEWAY_HELLO, .boot_id = 1};
	uint8_t encoded[SV_GATEWAY_MAX_DATAGRAM];
	size_t length = sv_gateway_encode(encoded, &input);
	zassert_equal(length, 32);
	struct sv_gateway_message output;
	zassert_true(sv_gateway_decode(&output, encoded, length));
	encoded[8] ^= 1;
	zassert_false(sv_gateway_decode(&output, encoded, length));
}

ZTEST(smol_protocol, test_shared_gateway_and_report_golden_vectors)
{
	const uint8_t hello_golden[] = SV_GOLDEN_GATEWAY_HELLO_BYTES;
	const uint8_t report_golden[] = SV_GOLDEN_SMOL_REPORT_BYTES;
	struct sv_gateway_message hello = {
		.kind = SV_GATEWAY_HELLO,
		.gateway_id = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
		.boot_id = 0x78563412,
		.sequence = UINT32_MAX,
	};
	uint8_t encoded[SV_GATEWAY_MAX_DATAGRAM];
	zassert_equal(sv_gateway_encode(encoded, &hello), sizeof(hello_golden));
	zassert_mem_equal(encoded, hello_golden, sizeof(hello_golden));
	zassert_equal(sizeof(report_golden), SV_SMOL_REPORT_SIZE);
}

ZTEST(smol_protocol, test_id48_uses_canonical_numeric_order)
{
	const uint8_t little_endian[] = {0xf5, 0xa5, 0xe8, 0x6f, 0x04, 0xa5};
	uint8_t decoded[SV_ID48_BYTE_SIZE];
	char text[SV_ID48_TEXT_SIZE];
	sv_id48_format(text, little_endian);
	zassert_equal(strcmp(text, "a5046fe8a5f5"), 0);
	zassert_true(sv_id48_parse(decoded, "A5046FE8A5F5"));
	zassert_mem_equal(decoded, little_endian, sizeof(decoded));
}

ZTEST_SUITE(smol_protocol, NULL, NULL, NULL, NULL, NULL);
