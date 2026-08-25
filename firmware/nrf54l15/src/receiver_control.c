/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "receiver_control.h"

#include "receiver_control_protocol.h"
#include "command_replay_cache.h"
#include "report_builder.h"
#include "rf_receiver.h"
#include "spi_bridge.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

static struct sv_command_replay_cache cache;

static void reboot_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	sys_reboot(SYS_REBOOT_COLD);
}

K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

void receiver_control_init(void)
{
	sv_command_replay_init(&cache);
}

static uint8_t status_from_errno(int error)
{
	if (error == 0) return SV_STATUS_OK;
	if (error == -EINVAL) return SV_STATUS_BAD_REQUEST;
	if (error == -EPERM) return SV_STATUS_NOT_ALLOWED;
	if (error == -ENOENT) return SV_STATUS_NOT_FOUND;
	if (error == -EBUSY) return SV_STATUS_BUSY;
	return SV_STATUS_INTERNAL_ERROR;
}

static void basic_response(struct sv_bridge_message *response, uint8_t command, int result)
{
	response->payload[0] = command;
	response->payload[1] = status_from_errno(result);
	response->payload_length = 2;
	if (result != 0) {
		response->flags |= SV_BRIDGE_FLAG_ERROR;
	}
}

static void get_status(struct sv_bridge_message *response)
{
	struct rf_receiver_status rf_status;
	rf_receiver_get_status(&rf_status);
	struct sv_receiver_status_response status = {
		.command = SV_CMD_GET_STATUS,
		.status = SV_STATUS_OK,
		.protocol_version = SV_RECEIVER_CONTROL_VERSION,
		.flags = (rf_status.primary ? SV_RECEIVER_STATUS_PRIMARY : 0) |
			(rf_status.pairing ? SV_RECEIVER_STATUS_PAIRING : 0) |
			(rf_status.radio_enabled ? SV_RECEIVER_STATUS_RADIO_ENABLED : 0),
		.stored_trackers = rf_status.tracker_count,
		.uptime_ms = (uint32_t)k_uptime_get(),
		.rf_received = rf_status.received,
		.rf_crc_errors = rf_status.crc_errors,
		.report_queue_drops = report_builder_drops(),
		.spi_crc_errors = spi_bridge_crc_errors(),
	};
	memcpy(status.receiver_id, &rf_status.receiver_id, 6);
	memcpy(status.group_id, &rf_status.group_id, 6);
	memcpy(response->payload, &status, sizeof(status));
	response->payload_length = sizeof(status);
}

static void get_tracker_page(const struct sv_bridge_message *request,
	struct sv_bridge_message *response)
{
	uint8_t page = request->payload_length >= 2 ? request->payload[1] : 0;
	struct sv_receiver_tracker_page_response result = {
		.command = SV_CMD_GET_TRACKER_PAGE,
		.status = SV_STATUS_OK,
		.page = page,
	};
	struct rf_receiver_status status;
	rf_receiver_get_status(&status);
	uint8_t start = (uint8_t)(page * ARRAY_SIZE(result.entries));
	for (uint8_t id = start; id < status.tracker_count && result.count < ARRAY_SIZE(result.entries); ++id) {
		uint64_t address = rf_receiver_tracker(id);
		if (address == 0) continue;
		result.entries[result.count].id = id;
		memcpy(result.entries[result.count].address, &address, 6);
		result.count++;
	}
	memcpy(response->payload, &result, sizeof(result));
	response->payload_length = sizeof(result);
}

static void execute(const struct sv_bridge_message *request,
	struct sv_bridge_message *response)
{
	memset(response, 0, sizeof(*response));
	response->kind = SV_BRIDGE_COMMAND_RESPONSE;
	response->request_id = request->request_id;
	if (request->payload_length == 0) {
		basic_response(response, 0, -EINVAL);
		return;
	}

	uint8_t command = request->payload[0];
	switch (command) {
	case SV_CMD_GET_STATUS:
		get_status(response);
		break;
	case SV_CMD_GET_TRACKER_PAGE:
		get_tracker_page(request, response);
		break;
	case SV_CMD_SET_GROUP: {
		if (request->payload_length < sizeof(struct sv_receiver_set_group_request)) {
			basic_response(response, command, -EINVAL);
			break;
		}
		struct sv_receiver_set_group_request set_group;
		memcpy(&set_group, request->payload, sizeof(set_group));
		uint64_t group = 0;
		memcpy(&group, set_group.group_id, 6);
		basic_response(response, command,
			rf_receiver_set_group(set_group.secondary != 0, group));
		break;
	}
	case SV_CMD_START_PAIRING:
		basic_response(response, command, rf_receiver_start_pairing());
		break;
	case SV_CMD_STOP_PAIRING:
		basic_response(response, command, rf_receiver_stop_pairing());
		break;
	case SV_CMD_CLEAR_TRACKERS:
		basic_response(response, command, rf_receiver_clear_trackers());
		break;
	case SV_CMD_SET_RADIO_ENABLED:
		basic_response(response, command,
			request->payload_length >= 2 ? rf_receiver_set_enabled(request->payload[1] != 0) : -EINVAL);
		break;
	case SV_CMD_REBOOT:
		basic_response(response, command, 0);
		break;
	default:
		basic_response(response, command, -EINVAL);
		break;
	}
}

void receiver_control_handle(const struct sv_bridge_message *request,
	struct sv_bridge_message *response)
{
	if (sv_command_replay_find(&cache, request->request_id, response)) return;
	execute(request, response);
	sv_command_replay_store(&cache, request->request_id, response);

	if (request->payload_length > 0 &&
		(request->payload[0] == SV_CMD_REBOOT || request->payload[0] == SV_CMD_SET_GROUP) &&
		response->payload_length > 1 && response->payload[1] == SV_STATUS_OK) {
		/* Leave enough time for SPIS to return the response before rebooting. */
		(void)k_work_reschedule(&reboot_work, K_MSEC(100));
	}
}
