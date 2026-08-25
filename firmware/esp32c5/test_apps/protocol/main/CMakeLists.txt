# SPDX-License-Identifier: MIT OR Apache-2.0
idf_component_register(
    SRCS
        "test_protocol.c"
        "../../../../../protocol/bridge_protocol.c"
        "../../../../../protocol/gateway_protocol.c"
        "../../../../../protocol/id48_codec.c"
        "../../../../../protocol/command_replay_cache.c"
    INCLUDE_DIRS "../../../../../protocol"
    REQUIRES unity
)
