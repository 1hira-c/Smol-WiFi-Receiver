/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef SMOL_SPI_BRIDGE_H
#define SMOL_SPI_BRIDGE_H

#include <stdint.h>

int spi_bridge_init(void);
uint32_t spi_bridge_crc_errors(void);

#endif
