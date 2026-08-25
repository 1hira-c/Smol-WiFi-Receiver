/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "command_replay_cache.h"

#include <string.h>

void sv_command_replay_init(struct sv_command_replay_cache *cache)
{
	memset(cache, 0, sizeof(*cache));
}

bool sv_command_replay_find(const struct sv_command_replay_cache *cache,
	uint16_t request_id, struct sv_bridge_message *response)
{
	for (uint8_t index = 0; index < SV_COMMAND_REPLAY_CACHE_SIZE; ++index) {
		if (cache->entries[index].valid && cache->entries[index].request_id == request_id) {
			*response = cache->entries[index].response;
			return true;
		}
	}
	return false;
}

void sv_command_replay_store(struct sv_command_replay_cache *cache,
	uint16_t request_id, const struct sv_bridge_message *response)
{
	cache->entries[cache->cursor].valid = true;
	cache->entries[cache->cursor].request_id = request_id;
	cache->entries[cache->cursor].response = *response;
	cache->cursor = (uint8_t)((cache->cursor + 1u) % SV_COMMAND_REPLAY_CACHE_SIZE);
}
