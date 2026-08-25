/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SMOL_RECEIVER_STORAGE_H
#define SMOL_RECEIVER_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "report_builder.h"

int receiver_storage_init(void);
uint64_t receiver_storage_group(void);
int receiver_storage_set_group(uint64_t group);
uint8_t receiver_storage_tracker_count(void);
uint64_t receiver_storage_tracker(uint8_t id);
int receiver_storage_set_tracker(uint8_t id, uint64_t address);
int receiver_storage_set_tracker_count(uint8_t count);
int receiver_storage_clear_trackers(void);

#endif
