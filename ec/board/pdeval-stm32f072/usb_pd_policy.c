/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "i2c.h"

#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

#define PDO_FIXED_FLAGS (PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP)

/* Used to fake VBUS presence since no GPIO is available to read VBUS */
static int vbus_present;
//static int vbus_present = 1; // Shruthi

enum volt_idx {
	PDO_IDX_5V  = 0,
	PDO_IDX_12V = 1,
	PDO_IDX_20V = 2,
	PDO_IDX_COUNT
};

const uint32_t pd_src_pdo[] = {
	[PDO_IDX_5V] = PDO_FIXED(5000, 1000, PDO_FIXED_FLAGS),
	[PDO_IDX_12V]  = PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
	[PDO_IDX_20V]  = PDO_FIXED(20000, 5000, PDO_FIXED_FLAGS),
};
const int pd_src_pdo_cnt = ARRAY_SIZE(pd_src_pdo);
BUILD_ASSERT(ARRAY_SIZE(pd_src_pdo) == PDO_IDX_COUNT);

const uint32_t pd_snk_pdo[] = {
	PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
};
const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);

int pd_is_valid_input_voltage(int mv)
{
	return 1;
}

void pd_transition_voltage(int idx)
{
	int ex_data = idx - 1;

	/* No-operation: we are always 5V */
	// Notify C2000 either by setting certain GPIO or by sending I2C message
	switch (idx - 1) {
		case PDO_IDX_20V:
			CPRINTF("Shruthi: In PDO_IDX_20V\n");
			//gpio_set_level(GPIO_PP20000_EN, 1);
			//gpio_set_level(GPIO_PPVAR_VBUS_EN, 0);
			break;
		case PDO_IDX_12V:
			CPRINTF("Shruthi: In PDO_IDX_12V\n");
			//gpio_set_level(GPIO_PP12000_EN, 1);
			//gpio_set_level(GPIO_PP20000_EN, 0);
			//gpio_set_level(GPIO_PPVAR_VBUS_EN, 1);
			break;
		case PDO_IDX_5V:
			CPRINTF("Shruthi: In PDO_IDX_5V\n");
		default:
			CPRINTF("Shruthi: In PDO_IDX_5V\n");
			//gpio_set_level(GPIO_PP12000_EN, 0);
			//gpio_set_level(GPIO_PP20000_EN, 0);
			//gpio_set_level(GPIO_PPVAR_VBUS_EN, 1);
			break;
	}
	
	// Send I2C message to C2000 with voltage as data // Shruthi TBD
	// Change I2C address as required!
	if(i2c_write32(0, 0x9c, 0xAB, ex_data) != EC_SUCCESS)
		CPRINTS("Error sending I2C data to C2000\n");
	else
		CPRINTS("Success sending I2C data to C2000\n");
}

int pd_set_power_supply_ready(int port)
{
	/* Turn on the "up" LED when we output VBUS */
	gpio_set_level(GPIO_LED_U, 1);
	CPRINTS("Power supply ready/%d", port);
	return EC_SUCCESS; /* we are ready */
}

void pd_power_supply_reset(int port)
{
	/* Turn off the "up" LED when we shutdown VBUS */
	gpio_set_level(GPIO_LED_U, 0);
	/* Disable VBUS */
	CPRINTS("Disable VBUS", port);
}

void pd_set_input_current_limit(int port, uint32_t max_ma,
		uint32_t supply_voltage)
{
	//CPRINTS("USBPD current limit port %d max %d mA %d mV",
	//		port, max_ma, supply_voltage);
	/* do some LED coding of the power we can sink */
	if (max_ma) {
		if (supply_voltage > 6500)
			gpio_set_level(GPIO_LED_R, 1);
		else
			gpio_set_level(GPIO_LED_L, 1);
	} else {
		gpio_set_level(GPIO_LED_L, 0);
		gpio_set_level(GPIO_LED_R, 0);
	}
}

void typec_set_input_current_limit(int port, uint32_t max_ma,
		uint32_t supply_voltage)
{
	CPRINTS("TYPEC current limit port %d max %d mA %d mV",
			port, max_ma, supply_voltage);
	gpio_set_level(GPIO_LED_R, !!max_ma);
}

void button_event(enum gpio_signal signal)
{
	vbus_present = !vbus_present;
	CPRINTS("VBUS %d", vbus_present);
}

static int command_vbus_toggle(int argc, char **argv)
{
	vbus_present = !vbus_present;
	CPRINTS("VBUS %d", vbus_present);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(vbus, command_vbus_toggle,
		"",
		"Toggle VBUS detected",
		NULL);

static int command_i2c_test(int argc, char **argv)
{
	int ex_data = 0;
	int ret = -1;
	//uint8_t data[10] = {'\0'};
	//uint32_t rdo = 268537956; // 5V
	//uint32_t rdo = 537178412; // 12V
	//uint32_t rdo = 805818868; // 20V

	ccprintf("\nSHRUTHI KASHYAP\n");

#if 0
	if(pd_check_requested_voltage(rdo) == EC_SUCCESS)
	{
		ccprintf("Voltage check success\n");
	}
	else
	{
		ccprintf("Voltage check error\n");
	}
#endif
#if 0
	while(1)
	{
		sleep(1);
		gpio_set_level(GPIO_LED_U, 1);
		sleep(1);
		gpio_set_level(GPIO_LED_U, 0);
	}
#endif
#if 1
	//ret = i2c_write16(0, 0x48, 0, ex_data);
	//ccprintf("\nSHRUTHI success w32 = %d, ret = %d\n", ex_data, ret);

	//sleep(1);

	ret = i2c_read8(0, 0x9c, 0xAB, &ex_data);
	ccprintf("\nSHRUTHI success r = %d, ret = %d\n", ex_data, ret);

	//ret =  i2c_read_string(1, 0x9e, 0x12, &data[0], 4);
	//ccprintf("\nSHRUTHI success string 1 = %s, %c, %c, %c, %d, %d, %d, ret = %d\n", data, data[1], data[2], data[3], (int)data[1], (int)data[2], (int)data[3], ret);
#endif

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(test, command_i2c_test,
		"",
		"I2C test detected",
		NULL);

void test_shr_task(void)
{
	// Shruthi: Enable VBUS 10s after bootup
	sleep(10);
	vbus_present = 1;
	
	// Shruthi: Send a test VDM to port 0 20s after bootup
	sleep(20);
	pd_send_vdm(0, USB_VID_GOOGLE, VDO_CMD_PRICE_TEST, NULL, 0);
	ccprintf("\nSHRUTHI: VDM sent\n");
	
	/*	int ex_data = 25;
		int ret = -1;

		while(1)
		{
		sleep(1);
		ret = i2c_write32(0, 0x9c, 0x16, ex_data);
		ccprintf("\nSHRUTHI success w16 = %d, ret = %d\n", ex_data, ret);
		sleep(1);
		ret = i2c_read16(0, 0x9c, 0x16, &ex_data);
		}
		*/
}

int pd_snk_is_vbus_provided(int port)
{
	return vbus_present;
}

int pd_board_checks(void)
{
	return EC_SUCCESS;
}

int pd_check_power_swap(int port)
{
	/*
	 * Allow power swap as long as we are acting as a dual role device,
	 * otherwise assume our role is fixed (not in S0 or console command
	 * to fix our role).
	 */
	return pd_get_dual_role() == PD_DRP_TOGGLE_ON ? 1 : 0;
}

int pd_check_data_swap(int port, int data_role)
{
	/* Always allow data swap */
	return 1;
}

void pd_execute_data_swap(int port, int data_role)
{
}

void pd_check_pr_role(int port, int pr_role, int flags)
{
}

void pd_check_dr_role(int port, int dr_role, int flags)
{
}
/* ----------------- Vendor Defined Messages ------------------ */
const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int pd_custom_vdm(int port, int cnt, uint32_t *payload,
		uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	int is_rw;
	
	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	switch (cmd) {
		case VDO_CMD_VERSION:
			/* guarantee last byte of payload is null character */
			*(payload + cnt - 1) = 0;
			CPRINTF("version: %s\n", (char *)(payload+1));
			break;
		case VDO_CMD_READ_INFO:
		case VDO_CMD_SEND_INFO:
			/* copy hash */
			if (cnt == 7) {
				dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
				is_rw = VDO_INFO_IS_RW(payload[6]);

				CPRINTF("DevId:%d.%d SW:%d RW:%d\n",
						HW_DEV_ID_MAJ(dev_id),
						HW_DEV_ID_MIN(dev_id),
						VDO_INFO_SW_DBG_VER(payload[6]),
						is_rw);
			} else if (cnt == 6) {
				/* really old devices don't have last byte */
				pd_dev_store_rw_hash(port, dev_id, payload + 1,
						SYSTEM_IMAGE_UNKNOWN);
			}
			break;
		case VDO_CMD_PRICE_TEST: // Shruthi
			CPRINTF("Shruthi: VDM PRICE TEST\n");
			break;
	}

	return 0;
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static int dp_flags[CONFIG_USB_PD_PORT_COUNT];

static void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	/* board_set_usb_mux(port, TYPEC_MUX_NONE, pd_get_polarity(port)); */
}

static int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);
		return 0;
	}

	return -1;
}

static int svdm_dp_status(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			CMD_DP_STATUS | VDO_OPOS(opos));
	payload[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
			0, /* HPD level ... not applicable */
			0, /* exit DP? ... no */
			0, /* usb mode? ... no */
			0, /* multi-function ... no */
			(!!(dp_flags[port] & DP_FLAGS_DP_ON)),
			0, /* power low? ... no */
			(!!(dp_flags[port] & DP_FLAGS_DP_ON)));
	return 2;
};

static int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	/* board_set_usb_mux(port, TYPEC_MUX_DP, pd_get_polarity(port)); */
	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(MODE_DP_PIN_E, /* pin mode */
			1,             /* DPv1.3 signaling */
			2);            /* UFP connected */
	return 2;
};

static void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;
}

static int svdm_dp_attention(int port, uint32_t *payload)
{
	/* ack */
	return 1;
}

static void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
	/* gpio_set_level(PORT_TO_HPD(port), 0); */
}

static int svdm_enter_gfu_mode(int port, uint32_t mode_caps)
{
	/* Always enter GFU mode */
	return 0;
}

static void svdm_exit_gfu_mode(int port)
{
}

static int svdm_gfu_status(int port, uint32_t *payload)
{
	/*
	 * This is called after enter mode is successful, send unstructured
	 * VDM to read info.
	 */
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_READ_INFO, NULL, 0);
	return 0;
}

static int svdm_gfu_config(int port, uint32_t *payload)
{
	return 0;
}

static int svdm_gfu_attention(int port, uint32_t *payload)
{
	return 0;
}

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.post_config = &svdm_dp_post_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},
	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	}
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
