/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SMOL_RF_RECEIVER_H
#define SMOL_RF_RECEIVER_H

#include <stdbool.h>
#include <stdint.h>

struct rf_receiver_status {
	uint64_t receiver_id;
	uint64_t group_id;
	bool primary;
	bool pairing;
	bool radio_enabled;
	uint8_t tracker_count;
	uint32_t received;
	uint32_t crc_errors;
};

int rf_receiver_init(void);
void rf_receiver_get_status(struct rf_receiver_status *status);
uint64_t rf_receiver_tracker(uint8_t id);
int rf_receiver_set_group(bool secondary, uint64_t group_id);
int rf_receiver_start_pairing(void);
int rf_receiver_stop_pairing(void);
int rf_receiver_clear_trackers(void);
int rf_receiver_set_enabled(bool enabled);

#endif
