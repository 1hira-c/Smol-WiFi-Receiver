/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#include "report_builder.h"
#include "rf_receiver.h"
#include "spi_bridge.h"
#include "test_generator.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	report_builder_init();
	int result = rf_receiver_init();
	if (result != 0) {
		LOG_ERR("RF receiver initialization failed: %d", result);
		return result;
	}
	result = spi_bridge_init();
	if (result != 0) {
		LOG_ERR("SPI bridge initialization failed: %d", result);
		return result;
	}
	test_generator_init();
	LOG_INF("Smol Wi-Fi nRF54L15 receiver ready");
	return 0;
}
