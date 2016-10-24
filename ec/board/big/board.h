/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Big board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Optional features */
#define CONFIG_AP_HANG_DETECT
#define CONFIG_BATTERY_SMART
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BQ24735
#define CONFIG_CHIPSET_TEGRA
#define CONFIG_POWER_COMMON
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_HOST_COMMAND_STATUS
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_PROTOCOL_MKBP
#define CONFIG_SPI
#define CONFIG_SPI_PROTOCOL_V2
#define CONFIG_PWM
#define CONFIG_POWER_BUTTON
#define CONFIG_VBOOT_HASH
#define CONFIG_LED_COMMON
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CMD_BATTFAKE
#define CONFIG_LTO

#ifndef __ASSEMBLER__

/* Keyboard output port list */
#define KB_OUT_PORT_LIST GPIO_A, GPIO_B, GPIO_C

/* Single I2C port, where the EC is the master. */
#define I2C_PORT_MASTER 0
#define I2C_PORT_BATTERY I2C_PORT_MASTER
#define I2C_PORT_CHARGER I2C_PORT_MASTER

/* Timer selection */
#define TIM_CLOCK_MSB 3
#define TIM_CLOCK_LSB 9
#define TIM_POWER_LED 2
#define TIM_WATCHDOG  4

#include "gpio_signal.h"

enum power_signal {
	TEGRA_XPSHOLD = 0,
	TEGRA_SUSPEND_ASSERTED,

	/* Number of power signals */
	POWER_SIGNAL_COUNT
};

enum pwm_channel {
	PWM_CH_POWER_LED = 0,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

/* Charger module */
#define CONFIG_CHARGER_SENSE_RESISTOR 10 /* Charge sense resistor, mOhm */
#define CONFIG_CHARGER_SENSE_RESISTOR_AC 20 /* Input sensor resistor, mOhm */
/* Input current limit for 45W AC adapter:
 * 45W/19V*85%=2013mA, choose the closest charger setting = 2048mA
 */
#define CONFIG_CHARGER_INPUT_CURRENT 2048 /* mA, based on Link HW design */
#define CONFIG_CHARGER_CURRENT_LIMIT 3000 /* PL102 inductor 3.0A(3.8A) */

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
