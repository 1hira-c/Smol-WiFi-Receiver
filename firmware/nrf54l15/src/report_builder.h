/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SMOL_REPORT_BUILDER_H
#define SMOL_REPORT_BUILDER_H

#include <stdbool.h>
#include <stdint.h>

#include "smol_report.h"

#define SMOL_MAX_TRACKERS 32u

void report_builder_init(void);
void report_builder_set_tracker(uint8_t id, uint64_t address);
void report_builder_clear_trackers(void);
void report_builder_enqueue(const uint8_t data[SV_SMOL_RECORD_SIZE], uint8_t rssi,
	uint8_t sequence, uint8_t flags, bool replace_sensor_data);
bool report_builder_next(uint8_t output[SV_SMOL_REPORT_SIZE]);
bool report_builder_pending(void);
uint32_t report_builder_drops(void);

#endif
