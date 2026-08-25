/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "test_generator.h"

#include "report_builder.h"
#include "rf_receiver.h"

#include <zephyr/kernel.h>

#if defined(CONFIG_SMOL_TEST_GENERATOR)

#define TEST_TRACKERS 16u
#define RECORD_INTERVAL_US (1000000u / (TEST_TRACKERS * 100u))

K_SEM_DEFINE(generator_start, 0, 1);

static void generator_thread(void)
{
	uint8_t sequences[TEST_TRACKERS] = {0};
	k_sem_take(&generator_start, K_FOREVER);
	uint8_t tracker = 0;
	while (true) {
		uint8_t record[SV_SMOL_RECORD_SIZE] = {0};
		record[0] = 1;
		record[1] = tracker;
		record[2] = sequences[tracker];
		record[3] = (uint8_t)k_uptime_get_32();
		report_builder_enqueue(record, 42, sequences[tracker]++,
			SV_SMOL_META_SEQUENCE_VALID | SV_SMOL_META_RF_V2 | SV_SMOL_META_CRC_VALID,
			true);
		tracker = (uint8_t)((tracker + 1u) % TEST_TRACKERS);
		k_usleep(RECORD_INTERVAL_US);
	}
}

K_THREAD_DEFINE(test_generator_thread, 2048, generator_thread, NULL, NULL, NULL, 6, 0, 0);

void test_generator_init(void)
{
	(void)rf_receiver_set_enabled(false);
	for (uint8_t id = 0; id < TEST_TRACKERS; ++id) {
		report_builder_set_tracker(id, 0x535600000000ULL + id + 1u);
	}
	k_sem_give(&generator_start);
}

#else

void test_generator_init(void) { }

#endif
