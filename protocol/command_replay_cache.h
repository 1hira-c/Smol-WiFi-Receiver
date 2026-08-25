/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SLIMEVR_COMMAND_REPLAY_CACHE_H
#define SLIMEVR_COMMAND_REPLAY_CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include "bridge_protocol.h"

#define SV_COMMAND_REPLAY_CACHE_SIZE 8u

struct sv_command_replay_entry {
	bool valid;
	uint16_t request_id;
	struct sv_bridge_message response;
};

struct sv_command_replay_cache {
	struct sv_command_replay_entry entries[SV_COMMAND_REPLAY_CACHE_SIZE];
	uint8_t cursor;
};

void sv_command_replay_init(struct sv_command_replay_cache *cache);
bool sv_command_replay_find(const struct sv_command_replay_cache *cache,
	uint16_t request_id, struct sv_bridge_message *response);
void sv_command_replay_store(struct sv_command_replay_cache *cache,
	uint16_t request_id, const struct sv_bridge_message *response);

#endif
