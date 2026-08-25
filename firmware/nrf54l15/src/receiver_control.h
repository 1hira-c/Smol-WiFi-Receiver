/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SMOL_RECEIVER_CONTROL_H
#define SMOL_RECEIVER_CONTROL_H

#include "bridge_protocol.h"

void receiver_control_init(void);
void receiver_control_handle(const struct sv_bridge_message *request,
	struct sv_bridge_message *response);

#endif
