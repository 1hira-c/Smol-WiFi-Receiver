/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "receiver_storage.h"

#include <errno.h>
#include <string.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(receiver_storage, LOG_LEVEL_INF);

#define NVS_PARTITION storage_partition
#define STORAGE_ID_GROUP 1u
#define STORAGE_ID_TRACKER_COUNT 2u
#define STORAGE_ID_TRACKER_BASE 16u

static struct nvs_fs fs;
static uint64_t group_id;
static uint64_t trackers[SMOL_MAX_TRACKERS];
static uint8_t tracker_count;

static int read_or_zero(uint16_t id, void *data, size_t length)
{
	int result = nvs_read(&fs, id, data, length);
	if (result == -ENOENT) {
		memset(data, 0, length);
		return 0;
	}
	return result < 0 ? result : 0;
}

int receiver_storage_init(void)
{
	struct flash_pages_info info;
	fs.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION);
	fs.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION);
	if (!device_is_ready(fs.flash_device) ||
		flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info) != 0) {
		return -ENODEV;
	}
	fs.sector_size = info.size;
	fs.sector_count = 4;
	int result = nvs_mount(&fs);
	if (result != 0) {
		LOG_ERR("NVS mount failed: %d", result);
		return result;
	}

	read_or_zero(STORAGE_ID_GROUP, &group_id, sizeof(group_id));
	read_or_zero(STORAGE_ID_TRACKER_COUNT, &tracker_count, sizeof(tracker_count));
	group_id &= 0xffffffffffffULL;
	if (tracker_count > SMOL_MAX_TRACKERS) {
		tracker_count = 0;
	}
	for (uint8_t id = 0; id < tracker_count; ++id) {
		read_or_zero((uint16_t)(STORAGE_ID_TRACKER_BASE + id), &trackers[id], sizeof(trackers[id]));
		trackers[id] &= 0xffffffffffffULL;
	}
	return 0;
}

uint64_t receiver_storage_group(void)
{
	return group_id;
}

int receiver_storage_set_group(uint64_t group)
{
	group_id = group & 0xffffffffffffULL;
	int result = nvs_write(&fs, STORAGE_ID_GROUP, &group_id, sizeof(group_id));
	return result < 0 ? result : 0;
}

uint8_t receiver_storage_tracker_count(void)
{
	return tracker_count;
}

uint64_t receiver_storage_tracker(uint8_t id)
{
	return id < tracker_count ? trackers[id] : 0;
}

int receiver_storage_set_tracker(uint8_t id, uint64_t address)
{
	if (id >= SMOL_MAX_TRACKERS) {
		return -EINVAL;
	}
	trackers[id] = address & 0xffffffffffffULL;
	int result = nvs_write(&fs, (uint16_t)(STORAGE_ID_TRACKER_BASE + id),
		&trackers[id], sizeof(trackers[id]));
	return result < 0 ? result : 0;
}

int receiver_storage_set_tracker_count(uint8_t count)
{
	if (count > SMOL_MAX_TRACKERS) {
		return -EINVAL;
	}
	tracker_count = count;
	int result = nvs_write(&fs, STORAGE_ID_TRACKER_COUNT, &tracker_count, sizeof(tracker_count));
	return result < 0 ? result : 0;
}

int receiver_storage_clear_trackers(void)
{
	for (uint8_t id = 0; id < tracker_count; ++id) {
		int result = nvs_delete(&fs, (uint16_t)(STORAGE_ID_TRACKER_BASE + id));
		if (result != 0 && result != -ENOENT) {
			return result;
		}
	}
	memset(trackers, 0, sizeof(trackers));
	return receiver_storage_set_tracker_count(0);
}
