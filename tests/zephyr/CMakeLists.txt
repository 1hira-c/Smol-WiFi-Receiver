# SPDX-License-Identifier: MIT OR Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smol_protocol_ztest)

target_sources(app PRIVATE
  src/test_protocol.c
  ../../protocol/bridge_protocol.c
  ../../protocol/gateway_protocol.c
  ../../protocol/id48_codec.c
  ../../protocol/command_replay_cache.c
)
target_include_directories(app PRIVATE ../../protocol)
