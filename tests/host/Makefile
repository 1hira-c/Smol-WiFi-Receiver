# SPDX-License-Identifier: MIT OR Apache-2.0
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
PROTOCOL := ../../protocol

.PHONY: test clean

test: protocol_test
	./protocol_test

protocol_test: test_protocol.c $(PROTOCOL)/bridge_protocol.c $(PROTOCOL)/gateway_protocol.c $(PROTOCOL)/id48_codec.c $(PROTOCOL)/command_replay_cache.c
	$(CC) $(CFLAGS) -I$(PROTOCOL) $^ -o $@

clean:
	rm -f protocol_test
