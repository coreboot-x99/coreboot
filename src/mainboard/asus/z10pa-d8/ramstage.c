/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/i2c_simple.h>

int platform_i2c_transfer(unsigned int bus, struct i2c_msg *segments, int count)
{
	// GENERIC_LINEAR_FRAMEBUFFER enables I2C compilation for the Aspeed VGA controller.
	// This is not supported (or needed) on the Z10PA-D8, so stub the function.
	return -1;
}
