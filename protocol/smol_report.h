/*
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#ifndef SMOL_REPORT_H
#define SMOL_REPORT_H

#include <stdint.h>

#define SV_SMOL_RECORD_SIZE 16u
#define SV_SMOL_DATA_RECORDS 3u
#define SV_SMOL_REPORT_SIZE 64u

#define SV_SMOL_METADATA_TYPE 250u
#define SV_SMOL_METADATA_MARKER 255u
#define SV_SMOL_METADATA_MAGIC 0xd6u
#define SV_SMOL_METADATA_VERSION 1u
#define SV_SMOL_METADATA_END_MAGIC 0x6du

#define SV_SMOL_META_SEQUENCE_VALID (1u << 0)
#define SV_SMOL_META_RF_V2          (1u << 1)
#define SV_SMOL_META_CRC_VALID      (1u << 2)

struct sv_smol_record_metadata {
	uint8_t sequence;
	uint8_t rssi;
	uint8_t flags;
};

struct sv_smol_record {
	uint8_t data[SV_SMOL_RECORD_SIZE];
	struct sv_smol_record_metadata metadata;
};

#endif
