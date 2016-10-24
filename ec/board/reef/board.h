/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Reef board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* FIXME: Gross hack. Remove it once proto boards are obsolete. */
#define IS_PROTO 1

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/* Battery */
#define CONFIG_BATTERY_CUT_OFF
#define CONFIG_BATTERY_PRESENT_CUSTOM
#define CONFIG_BATTERY_SMART

/* Charger */
#define CONFIG_CHARGE_MANAGER
#define CONFIG_CHARGER
#define CONFIG_CHARGER_V2
#define CONFIG_CHARGER_BD99955
#define CONFIG_CHARGER_DISCHARGE_ON_AC
#define CONFIG_CHARGER_INPUT_CURRENT 512
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_BAT_PCT 1
#define CONFIG_CHARGER_LIMIT_POWER_THRESH_CHG_MW 15000
#define CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON 1
#define CONFIG_CHARGER_NARROW_VDC
#define CONFIG_USB_CHARGER

/* USB PD config */
#define CONFIG_CASE_CLOSED_DEBUG_EXTERNAL
#define CONFIG_USB_PD_ALT_MODE
#define CONFIG_USB_PD_ALT_MODE_DFP
#define CONFIG_USB_PD_CUSTOM_VDM
#define CONFIG_USB_PD_DUAL_ROLE
#define CONFIG_USB_PD_LOGGING
#define CONFIG_USB_PD_LOG_SIZE 512
#define CONFIG_USB_PD_PORT_COUNT 2
#define CONFIG_USB_PD_VBUS_DETECT_CHARGER
#define CONFIG_USB_PD_TCPM_MUX  /* for both PS8751 and ANX3429 */
#define CONFIG_USB_PD_TCPM_ANX74XX
#define CONFIG_USB_PD_TCPM_PS8751
#define CONFIG_USB_PD_TCPM_TCPCI
#define CONFIG_USB_PD_TRY_SRC
#define CONFIG_USB_POWER_DELIVERY

#define CONFIG_USBC_SS_MUX
#define CONFIG_USBC_SS_MUX_DFP_ONLY
#define CONFIG_USBC_VCONN
#define CONFIG_USBC_VCONN_SWAP

/* SoC / PCH */
#define CONFIG_LPC
#define CONFIG_CHIPSET_APOLLOLAKE
#define CONFIG_CHIPSET_RESET_HOOK
#undef CONFIG_PECI
#define CONFIG_POWER_BUTTON
#define CONFIG_POWER_BUTTON_X86
#define CONFIG_POWER_COMMON

#define CONFIG_CMD_CHARGER_ADC_AMON_BMON
#define CONFIG_CHARGER_SENSE_RESISTOR		10
#define CONFIG_CHARGER_SENSE_RESISTOR_AC	10
#define BD99955_IOUT_GAIN_SELECT \
		BD99955_CMD_PMON_IOUT_CTRL_SET_IOUT_GAIN_SET_20V

#define CONFIG_CMD_CHARGER_PSYS
#define BD99955_PSYS_GAIN_SELECT \
		BD99955_CMD_PMON_IOUT_CTRL_SET_PMON_GAIN_SET_02UAW

/* EC */
#define CONFIG_ADC
#define CONFIG_BOARD_VERSION
#define CONFIG_BOARD_SPECIFIC_VERSION
#define CONFIG_BUTTON_COUNT 2
#define CONFIG_EXTPOWER_GPIO
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_MASTER
#define CONFIG_KEYBOARD_PROTOCOL_8042
#define CONFIG_KEYBOARD_COL2_INVERTED
#define CONFIG_LED_COMMON
#define CONFIG_LID_SWITCH
/* #define CONFIG_LOW_POWER_IDLE */
#define CONFIG_LTO
#define CONFIG_POWER_SIGNAL_INTERRUPT_STORM_DETECT_THRESHOLD 30
#define CONFIG_PWM
/* #define CONFIG_TEMP_SENSOR */
/* #define CONFIG_THROTTLE_AP */
#define CONFIG_SCI_GPIO GPIO_PCH_SCI_L
#define CONFIG_UART_HOST 0
#define CONFIG_VBOOT_HASH
#define CONFIG_WIRELESS
#define CONFIG_WLAN_POWER_ACTIVE_LOW
#define  WIRELESS_GPIO_WLAN_POWER GPIO_WIRELESS_GPIO_WLAN_POWER

#define CONFIG_FLASH_SIZE 524288
#define CONFIG_SPI_FLASH_W25Q40	/* FIXME: Should be GD25LQ40? */

/*
 * Enable 1 slot of secure temporary storage to support
 * suspend/resume with read/write memory training.
 */
#define CONFIG_VSTORE
#define CONFIG_VSTORE_SLOT_COUNT 1

/* Optional feature - used by nuvoton */
#define NPCX_UART_MODULE2    1 /* 0:GPIO10/11 1:GPIO64/65 as UART */
#define NPCX_JTAG_MODULE2    0 /* 0:GPIO21/17/16/20 1:GPIOD5/E2/D4/E5 as JTAG*/
/* FIXME(dhendrix): these pins are just normal GPIOs on Reef. Do we need
 * to change some other setting to put them in GPIO mode? */
#define NPCX_TACH_SEL2       0 /* 0:GPIO40/A4 1:GPIO93/D3 as TACH */

/* I2C ports */
#define I2C_PORT_GYRO                   NPCX_I2C_PORT1
#define I2C_PORT_LID_ACCEL              NPCX_I2C_PORT2
#define I2C_PORT_ALS                    NPCX_I2C_PORT2
#define I2C_PORT_BATTERY                NPCX_I2C_PORT3
#define I2C_PORT_CHARGER                NPCX_I2C_PORT3

/*
 * FIXME: This #define is necessary for the BMI160 driver. The driver doesn't
 * actually use the value, so we should eventually re-factor the driver to
 * be less confusing.
 */
#define I2C_PORT_ACCEL			I2C_PORT_GYRO

/* Sensors */
#define CONFIG_ACCELGYRO_BMI160
#define   CONFIG_MAG_BMI160_BMM150
#define   BMM150_I2C_ADDRESS BMM150_ADDR0	/* 8-bit address */
#define   CONFIG_MAG_CALIBRATE
#define CONFIG_ACCEL_KX022
#define CONFIG_ALS
#define CONFIG_ALS_OPT3001
#define OPT3001_I2C_ADDR OPT3001_I2C_ADDR1
/* FIXME: Need to add BMP280 barometer */
/* #define CONFIG_LID_ANGLE */	/* FIXME(dhendrix): maybe? */
/* #define CONFIG_LID_ANGLE_SENSOR_BASE 0 */	/* FIXME(dhendrix): maybe? */
/* #define CONFIG_LID_ANGLE_SENSOR_LID 2 */	/* FIXME(dhendrix): maybe? */

#undef DEFERRABLE_MAX_COUNT
#define DEFERRABLE_MAX_COUNT 15

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* ADC signal */
enum adc_channel {
	ADC_BOARD_ID = 2,
	ADC_CH_COUNT
};

enum pwm_channel {
	PWM_CH_LED_GREEN = 0,
	PWM_CH_LED_RED,
	/* Number of PWM channels */
	PWM_CH_COUNT
};

enum power_signal {
	X86_RSMRST_N = 0,
	X86_SLP_S0_N,
	X86_SLP_S3_N,
	X86_SLP_S4_N,
	X86_SUSPWRDNACK,

	X86_ALL_SYS_PG,		/* PMIC_EC_PWROK_OD */
	X86_PGOOD_PP3300,	/* GPIO_PP3300_PG */
	X86_PGOOD_PP5000,	/* GPIO_PP5000_PG */

	/* Number of X86 signals */
	POWER_SIGNAL_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY = 0,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

/* Light sensors */
enum als_id {
	ALS_OPT3001 = 0,

	ALS_COUNT
};

/* Motion sensors */
enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	BASE_MAG,
	LID_ACCEL,
};

/* start as a sink in case we have no other power supply/battery */
#define PD_DEFAULT_STATE PD_STATE_SNK_DISCONNECTED

/* TODO: determine the following board specific type-C power constants */
/* FIXME(dhendrix): verify all of the below PD_* numbers */
/*
 * delay to turn on the power supply max is ~16ms.
 * delay to turn off the power supply max is about ~180ms.
 */
#define PD_POWER_SUPPLY_TURN_ON_DELAY  30000  /* us */
#define PD_POWER_SUPPLY_TURN_OFF_DELAY 250000 /* us */

/* delay to turn on/off vconn */
#define PD_VCONN_SWAP_DELAY 5000 /* us */

/* Define typical operating power and max power */
#define PD_OPERATING_POWER_MW 15000
#define PD_MAX_POWER_MW       45000
#define PD_MAX_CURRENT_MA     3000
#define PD_MAX_VOLTAGE_MV     20000

/* Reset PD MCU */
void board_reset_pd_mcu(void);

int board_get_version(void);

void board_set_tcpc_power_mode(int port, int mode);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
