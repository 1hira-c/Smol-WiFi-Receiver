/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SMOL_ID48_CODEC_H
#define SMOL_ID48_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#define SV_ID48_BYTE_SIZE 6u
#define SV_ID48_TEXT_SIZE 13u

/* Convert little-endian wire/storage bytes to canonical 12-digit hexadecimal. */
void sv_id48_format(char output[SV_ID48_TEXT_SIZE],
	const uint8_t little_endian[SV_ID48_BYTE_SIZE]);

/* Parse canonical hexadecimal into little-endian wire/storage bytes. */
bool sv_id48_parse(uint8_t little_endian[SV_ID48_BYTE_SIZE], const char *text);

#endif
