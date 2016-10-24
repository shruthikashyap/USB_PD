/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Glados.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "util.h"

#define GPIO_CHARGE_LED_1 10
#define GPIO_CHARGE_LED_2 11

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

#define CRITICAL_LOW_BATTERY_PERCENTAGE 3
#define LOW_BATTERY_PERCENTAGE 10

#define LED_TOTAL_4SECS_TICKS 4
#define LED_TOTAL_2SECS_TICKS 2
#define LED_ON_1SEC_TICKS 1
#define LED_ON_2SECS_TICKS 2

const enum ec_led_id supported_led_ids[] = {
			EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_AMBER,
	LED_GREEN,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		break;
	case LED_RED:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		break;
	case LED_GREEN:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int glados_led_set_color_battery(enum led_color color)
{
	return bat_led_set_color(color);
}

/** * Called by hook task every 1 sec  */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		glados_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
