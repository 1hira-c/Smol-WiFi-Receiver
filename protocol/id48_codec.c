/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "id48_codec.h"

#include <stddef.h>
#include <string.h>

static int hex_value(char value)
{
	if (value >= '0' && value <= '9') return value - '0';
	if (value >= 'a' && value <= 'f') return value - 'a' + 10;
	if (value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

void sv_id48_format(char output[SV_ID48_TEXT_SIZE],
	const uint8_t little_endian[SV_ID48_BYTE_SIZE])
{
	static const char hex[] = "0123456789abcdef";
	for (size_t index = 0; index < SV_ID48_BYTE_SIZE; ++index) {
		uint8_t value = little_endian[SV_ID48_BYTE_SIZE - 1u - index];
		output[index * 2u] = hex[value >> 4u];
		output[index * 2u + 1u] = hex[value & 0x0fu];
	}
	output[SV_ID48_TEXT_SIZE - 1u] = '\0';
}

bool sv_id48_parse(uint8_t little_endian[SV_ID48_BYTE_SIZE], const char *text)
{
	if (little_endian == NULL || text == NULL || strlen(text) != SV_ID48_TEXT_SIZE - 1u) {
		return false;
	}
	for (size_t index = 0; index < SV_ID48_BYTE_SIZE; ++index) {
		int high = hex_value(text[index * 2u]);
		int low = hex_value(text[index * 2u + 1u]);
		if (high < 0 || low < 0) return false;
		little_endian[SV_ID48_BYTE_SIZE - 1u - index] = (uint8_t)((high << 4u) | low);
	}
	return true;
}
