/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "report_builder.h"

#include <string.h>
#include <zephyr/kernel.h>

#define REPORT_QUEUE_SIZE 128u
#define REGISTRATION_PERIOD_MS 100

static struct sv_smol_record queue[REPORT_QUEUE_SIZE];
static uint16_t read_index;
static uint16_t write_index;
static uint64_t tracker_addresses[SMOL_MAX_TRACKERS];
static uint8_t registration_cursor;
static int64_t last_registration_ms;
static uint32_t drops;
static struct k_spinlock lock;

static uint16_t advance(uint16_t index)
{
	return (uint16_t)((index + 1u) % REPORT_QUEUE_SIZE);
}

void report_builder_init(void)
{
	read_index = 0;
	write_index = 0;
	registration_cursor = 0;
	last_registration_ms = -REGISTRATION_PERIOD_MS;
	drops = 0;
}

void report_builder_set_tracker(uint8_t id, uint64_t address)
{
	if (id >= SMOL_MAX_TRACKERS) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	tracker_addresses[id] = address & 0xffffffffffffULL;
	k_spin_unlock(&lock, key);
}

void report_builder_clear_trackers(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	memset(tracker_addresses, 0, sizeof(tracker_addresses));
	read_index = write_index = 0;
	registration_cursor = 0;
	k_spin_unlock(&lock, key);
}

void report_builder_enqueue(const uint8_t data[SV_SMOL_RECORD_SIZE], uint8_t rssi,
	uint8_t sequence, uint8_t flags, bool replace_sensor_data)
{
	struct sv_smol_record record = {0};
	memcpy(record.data, data, sizeof(record.data));
	if (data[0] != 1u && data[0] != 4u) {
		record.data[15] = rssi;
	}
	record.metadata.sequence = sequence;
	record.metadata.rssi = rssi;
	record.metadata.flags = flags;

	k_spinlock_key_t key = k_spin_lock(&lock);
	if (replace_sensor_data) {
		for (uint16_t cursor = read_index; cursor != write_index; cursor = advance(cursor)) {
			if (queue[cursor].data[0] <= 223u && queue[cursor].data[1] == data[1]) {
				queue[cursor] = record;
				k_spin_unlock(&lock, key);
				return;
			}
		}
	}
	uint16_t next = advance(write_index);
	if (next == read_index) {
		drops++;
		k_spin_unlock(&lock, key);
		return;
	}
	queue[write_index] = record;
	write_index = next;
	k_spin_unlock(&lock, key);
}

static bool next_registration(uint8_t output[SV_SMOL_RECORD_SIZE])
{
	for (uint8_t offset = 0; offset < SMOL_MAX_TRACKERS; ++offset) {
		uint8_t id = (uint8_t)((registration_cursor + offset) % SMOL_MAX_TRACKERS);
		if (tracker_addresses[id] == 0) {
			continue;
		}
		memset(output, 0, SV_SMOL_RECORD_SIZE);
		output[0] = 255;
		output[1] = id;
		memcpy(&output[2], &tracker_addresses[id], 6);
		registration_cursor = (uint8_t)((id + 1u) % SMOL_MAX_TRACKERS);
		return true;
	}
	return false;
}

static bool has_registration(void)
{
	for (uint8_t id = 0; id < SMOL_MAX_TRACKERS; ++id) {
		if (tracker_addresses[id] != 0) {
			return true;
		}
	}
	return false;
}

bool report_builder_next(uint8_t output[SV_SMOL_REPORT_SIZE])
{
	struct sv_smol_record records[SV_SMOL_DATA_RECORDS] = {0};
	uint8_t queued = 0;
	const int64_t now = k_uptime_get();

	k_spinlock_key_t key = k_spin_lock(&lock);
	while (queued < SV_SMOL_DATA_RECORDS && read_index != write_index) {
		records[queued++] = queue[read_index];
		read_index = advance(read_index);
	}
	bool registration_due = has_registration() &&
		(now - last_registration_ms >= REGISTRATION_PERIOD_MS);
	if (queued == 0 && !registration_due) {
		k_spin_unlock(&lock, key);
		return false;
	}

	memset(output, 0, SV_SMOL_REPORT_SIZE);
	uint8_t valid_data = 0;
	for (uint8_t slot = 0; slot < queued; ++slot) {
		memcpy(&output[slot * SV_SMOL_RECORD_SIZE], records[slot].data, SV_SMOL_RECORD_SIZE);
		if (records[slot].data[0] <= 223u) {
			valid_data++;
		}
	}
	for (uint8_t slot = queued; slot < SV_SMOL_DATA_RECORDS; ++slot) {
		if (!next_registration(&output[slot * SV_SMOL_RECORD_SIZE])) {
			break;
		}
		registration_due = true;
	}
	if (registration_due) {
		last_registration_ms = now;
	}
	k_spin_unlock(&lock, key);

	uint8_t *metadata = &output[SV_SMOL_DATA_RECORDS * SV_SMOL_RECORD_SIZE];
	metadata[0] = SV_SMOL_METADATA_TYPE;
	metadata[1] = SV_SMOL_METADATA_MARKER;
	metadata[2] = SV_SMOL_METADATA_MAGIC;
	metadata[3] = SV_SMOL_METADATA_VERSION;
	metadata[4] = valid_data;
	for (uint8_t slot = 0; slot < queued; ++slot) {
		metadata[5 + slot * 3] = records[slot].metadata.sequence;
		metadata[6 + slot * 3] = records[slot].metadata.rssi;
		metadata[7 + slot * 3] = records[slot].metadata.flags;
	}
	metadata[15] = SV_SMOL_METADATA_END_MAGIC;
	return true;
}

bool report_builder_pending(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool pending = read_index != write_index ||
		(has_registration() &&
		 (k_uptime_get() - last_registration_ms >= REGISTRATION_PERIOD_MS));
	k_spin_unlock(&lock, key);
	return pending;
}

uint32_t report_builder_drops(void)
{
	return drops;
}
