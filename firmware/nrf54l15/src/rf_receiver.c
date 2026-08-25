/*
 * SlimeVR Code is placed under the MIT license.
 * Copyright (c) 2025 SlimeVR Contributors
 * SPDX-License-Identifier: MIT OR Apache-2.0
 */
#include "rf_receiver.h"

#include "receiver_storage.h"
#include "report_builder.h"

#include <errno.h>
#include <esb.h>
#include <hal/nrf_ficr.h>
#include <string.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(rf_receiver, LOG_LEVEL_INF);

#define DEVICE_ADDRESS_MASK 0xffffffffffffULL
#define RF_V2_PACKET_SIZE 28u
#define RF_V2_VERSION 2u
#define RF_V2_CRC_SEED 0x93a409ebu

static const uint8_t discovery_base_addr_0[4] = {0x62, 0x39, 0x8a, 0xf2};
static const uint8_t discovery_prefixes[8] = {0xfe, 0xff, 0x29, 0x27, 0x09, 0x02, 0xb2, 0xd6};

static struct esb_payload rx_payload;
static struct esb_payload pair_reply = ESB_CREATE_PAYLOAD(0, 0, 0, 0, 0, 0, 0, 0, 0);
static uint64_t receiver_id;
static uint64_t group_id;
static uint64_t tracker_addresses[SMOL_MAX_TRACKERS];
static uint8_t tracker_count;
static bool primary;
static bool pairing;
static bool radio_enabled = true;
static bool esb_initialized;
static uint32_t received_packets;
static uint32_t crc_errors;
static uint32_t config_generation;
static struct k_mutex state_mutex;

struct pairing_persist_record {
	uint8_t id;
	uint8_t tracker_count;
	uint64_t address;
	uint32_t generation;
};

K_MSGQ_DEFINE(pairing_persist_queue, sizeof(struct pairing_persist_record), 8, 4);

static void persist_pairing_work_handler(struct k_work *work);
K_WORK_DEFINE(persist_pairing_work, persist_pairing_work_handler);

static uint64_t read_device_address(void)
{
	return (((uint64_t)NRF_FICR->DEVICEADDR[1] << 32) |
		NRF_FICR->DEVICEADDR[0]) & DEVICE_ADDRESS_MASK;
}

static int start_hf_clock(void)
{
	struct onoff_manager *manager = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	struct onoff_client client;
	if (manager == NULL) {
		return -ENXIO;
	}
	sys_notify_init_spinwait(&client.notify);
	int result = onoff_request(manager, &client);
	if (result < 0) {
		return result;
	}
	int clock_result;
	while ((result = sys_notify_fetch_result(&client.notify, &clock_result)) == -EAGAIN) {
		k_yield();
	}
	return result == 0 ? clock_result : result;
}

static void derive_addresses(uint64_t group, uint8_t base1[4])
{
	uint8_t bytes[6];
	memcpy(bytes, &group, sizeof(bytes));
	for (uint8_t i = 0; i < 4; ++i) {
		base1[i] = (uint8_t)(bytes[i] + bytes[4]);
		if (base1[i] == 0x00 || base1[i] == 0x55 || base1[i] == 0xaa) {
			base1[i] = (uint8_t)(base1[i] + 8u);
		}
	}
}

static int add_tracker(uint8_t id, uint64_t address)
{
	address &= DEVICE_ADDRESS_MASK;
	if (id >= SMOL_MAX_TRACKERS || address == 0) {
		return -EINVAL;
	}
	if (tracker_addresses[id] != 0 && tracker_addresses[id] != address) {
		return -EADDRINUSE;
	}
	tracker_addresses[id] = address;
	if (id >= tracker_count) {
		tracker_count = (uint8_t)(id + 1u);
	}
	report_builder_set_tracker(id, address);
	return 0;
}

static void persist_pairing_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	struct pairing_persist_record record;
	while (k_msgq_get(&pairing_persist_queue, &record, K_NO_WAIT) == 0) {
		k_mutex_lock(&state_mutex, K_FOREVER);
		int result = 0;
		if (record.generation == config_generation) {
			result = receiver_storage_set_tracker(record.id, record.address);
			if (result == 0) result = receiver_storage_set_tracker_count(record.tracker_count);
		}
		k_mutex_unlock(&state_mutex);
		if (result != 0) LOG_ERR("Unable to persist paired tracker %u: %d", record.id, result);
	}
}

static void handle_rf_v2(const struct esb_payload *payload)
{
	if (payload->data[23] != RF_V2_VERSION) {
		return;
	}
	uint32_t expected_crc;
	memcpy(&expected_crc, &payload->data[24], sizeof(expected_crc));
	uint32_t actual_crc = crc32_k_4_2_update(RF_V2_CRC_SEED, payload->data, 24);
	if (expected_crc != actual_crc) {
		crc_errors++;
		uint8_t diagnostic[SV_SMOL_RECORD_SIZE] = {251, 255};
		report_builder_enqueue(diagnostic, payload->rssi, payload->data[22],
			SV_SMOL_META_RF_V2, false);
		return;
	}

	uint8_t id = payload->data[1];
	uint64_t address = 0;
	memcpy(&address, &payload->data[16], 6);
	address &= DEVICE_ADDRESS_MASK;
	if (id >= SMOL_MAX_TRACKERS || payload->data[0] > 223u || address == 0) {
		return;
	}

	if (primary) {
		if (id >= tracker_count || tracker_addresses[id] != address) {
			return;
		}
	} else if (add_tracker(id, address) != 0) {
		return;
	}

	received_packets++;
	report_builder_enqueue(payload->data, payload->rssi, payload->data[22],
		SV_SMOL_META_SEQUENCE_VALID | SV_SMOL_META_RF_V2 | SV_SMOL_META_CRC_VALID,
		true);
}

static void handle_pair_request(const struct esb_payload *payload)
{
	if (!primary || !pairing || payload->length != 8 || payload->data[1] != 0) {
		return;
	}
	uint8_t checksum = crc8_ccitt(0x07, &payload->data[2], 6);
	if (checksum == 0) {
		checksum = 8;
	}
	if (checksum != payload->data[0]) {
		return;
	}
	uint64_t address = 0;
	memcpy(&address, &payload->data[2], 6);
	address &= DEVICE_ADDRESS_MASK;
	if (address == 0) {
		return;
	}

	uint8_t id = tracker_count;
	for (uint8_t cursor = 0; cursor < tracker_count; ++cursor) {
		if (tracker_addresses[cursor] == address) {
			id = cursor;
			break;
		}
	}
	if (id >= SMOL_MAX_TRACKERS || add_tracker(id, address) != 0) {
		return;
	}
	struct pairing_persist_record persist = {
		.id = id,
		.tracker_count = tracker_count,
		.address = address,
		.generation = config_generation,
	};
	if (k_msgq_put(&pairing_persist_queue, &persist, K_NO_WAIT) == 0) {
		(void)k_work_submit(&persist_pairing_work);
	} else {
		LOG_ERR("Pairing persistence queue full");
		return;
	}

	pair_reply.pipe = 0;
	pair_reply.noack = false;
	pair_reply.data[0] = checksum;
	pair_reply.data[1] = id;
	memcpy(&pair_reply.data[2], &receiver_id, 6);
	(void)esb_write_payload(&pair_reply);
	LOG_INF("Paired tracker %u: %012llx", id, address);
}

static void receiver_esb_event_handler(struct esb_evt const *event)
{
	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}
	while (esb_read_rx_payload(&rx_payload) == 0) {
		if (rx_payload.pipe == 0) {
			handle_pair_request(&rx_payload);
		} else if (rx_payload.length == RF_V2_PACKET_SIZE) {
			handle_rf_v2(&rx_payload);
		}
	}
}

static void stop_radio(void)
{
	if (esb_initialized) {
		(void)esb_stop_rx();
		(void)esb_disable();
		esb_initialized = false;
	}
}

static int start_radio(void)
{
	if (!radio_enabled || esb_initialized) {
		return 0;
	}
	uint8_t base1[4];
	derive_addresses(group_id, base1);
	struct esb_config config = ESB_DEFAULT_CONFIG;
	config.mode = ESB_MODE_PRX;
	config.event_handler = receiver_esb_event_handler;
	config.tx_output_power = 8;
	config.retransmit_count = 0;
	config.selective_auto_ack = true;
	int result = esb_init(&config);
	if (result == 0) result = esb_set_base_address_0(discovery_base_addr_0);
	if (result == 0) result = esb_set_base_address_1(base1);
	if (result == 0) result = esb_set_prefixes(discovery_prefixes, ARRAY_SIZE(discovery_prefixes));
	if (result == 0) result = esb_enable_pipes(pairing ? (BIT(0) | BIT(1)) : BIT(1));
	if (result == 0) result = esb_start_rx();
	if (result != 0) {
		(void)esb_disable();
		return result;
	}
	esb_initialized = true;
	return 0;
}

int rf_receiver_init(void)
{
	k_mutex_init(&state_mutex);
	receiver_id = read_device_address();
	int result = receiver_storage_init();
	if (result != 0) {
		return result;
	}
	group_id = receiver_storage_group();
	if (group_id == 0) {
		group_id = receiver_id;
	}
	primary = group_id == receiver_id;
	tracker_count = receiver_storage_tracker_count();
	for (uint8_t id = 0; id < tracker_count; ++id) {
		tracker_addresses[id] = receiver_storage_tracker(id);
		report_builder_set_tracker(id, tracker_addresses[id]);
	}
	pairing = primary && tracker_count == 0;
	result = start_hf_clock();
	if (result == 0) {
		result = start_radio();
	}
	LOG_INF("Receiver %012llx group %012llx (%s)", receiver_id, group_id,
		primary ? "primary" : "secondary");
	return result;
}

void rf_receiver_get_status(struct rf_receiver_status *status)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	unsigned int key = irq_lock();
	*status = (struct rf_receiver_status) {
		.receiver_id = receiver_id,
		.group_id = group_id,
		.primary = primary,
		.pairing = pairing,
		.radio_enabled = radio_enabled,
		.tracker_count = tracker_count,
		.received = received_packets,
		.crc_errors = crc_errors,
	};
	irq_unlock(key);
	k_mutex_unlock(&state_mutex);
}

uint64_t rf_receiver_tracker(uint8_t id)
{
	unsigned int key = irq_lock();
	uint64_t address = id < tracker_count ? tracker_addresses[id] : 0;
	irq_unlock(key);
	return address;
}

int rf_receiver_set_group(bool secondary, uint64_t requested_group)
{
	requested_group &= DEVICE_ADDRESS_MASK;
	if (secondary && requested_group == 0) {
		return -EINVAL;
	}
	k_mutex_lock(&state_mutex, K_FOREVER);
	stop_radio();
	config_generation++;
	uint64_t previous_group = group_id;
	bool previous_primary = primary;
	bool previous_pairing = pairing;
	group_id = secondary ? requested_group : receiver_id;
	primary = !secondary;
	pairing = false;
	int result = receiver_storage_set_group(secondary ? group_id : 0);
	int clear_result = receiver_storage_clear_trackers();
	if (result == 0) result = clear_result;
	if (result != 0) {
		group_id = previous_group;
		primary = previous_primary;
		pairing = previous_pairing;
		(void)receiver_storage_set_group(previous_primary ? 0 : previous_group);
		(void)start_radio();
		k_mutex_unlock(&state_mutex);
		return result;
	}
	k_msgq_purge(&pairing_persist_queue);
	memset(tracker_addresses, 0, sizeof(tracker_addresses));
	tracker_count = 0;
	report_builder_clear_trackers();
	k_mutex_unlock(&state_mutex);
	/* receiver_control schedules a reboot after the response reaches the ESP. */
	return 0;
}

int rf_receiver_start_pairing(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	if (!primary) {
		k_mutex_unlock(&state_mutex);
		return -EPERM;
	}
	stop_radio();
	pairing = true;
	int result = start_radio();
	k_mutex_unlock(&state_mutex);
	return result;
}

int rf_receiver_stop_pairing(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	stop_radio();
	pairing = false;
	int result = start_radio();
	k_mutex_unlock(&state_mutex);
	return result;
}

int rf_receiver_clear_trackers(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	stop_radio();
	config_generation++;
	int result = receiver_storage_clear_trackers();
	k_msgq_purge(&pairing_persist_queue);
	memset(tracker_addresses, 0, sizeof(tracker_addresses));
	tracker_count = 0;
	pairing = primary;
	report_builder_clear_trackers();
	int radio_result = start_radio();
	if (result == 0) result = radio_result;
	k_mutex_unlock(&state_mutex);
	return result;
}

int rf_receiver_set_enabled(bool enabled)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	stop_radio();
	radio_enabled = enabled;
	int result = start_radio();
	k_mutex_unlock(&state_mutex);
	return result;
}
