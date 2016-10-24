/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

#ifndef __CROS_EC_USB_CHARGE_H
#define __CROS_EC_USB_CHARGE_H

#include "common.h"

/* USB charger voltage */
#define USB_CHARGER_VOLTAGE_MV  5000
/* USB charger minimum current */
#define USB_CHARGER_MIN_CURR_MA 500

enum usb_charge_mode {
	/* Disable USB port. */
	USB_CHARGE_MODE_DISABLED,
	/* Set USB port to Standard Downstream Port, USB 2.0 mode. */
	USB_CHARGE_MODE_SDP2,
	/* Set USB port to Charging Downstream Port, BC 1.2. */
	USB_CHARGE_MODE_CDP,
	/* Set USB port to Dedicated Charging Port, BC 1.2. */
	USB_CHARGE_MODE_DCP_SHORT,
	/* Enable USB port (for dumb ports). */
	USB_CHARGE_MODE_ENABLED,

	USB_CHARGE_MODE_COUNT
};

/**
 * Set USB charge mode for the port.
 *
 * @param usb_port_id	Port to set.
 * @param mode		New mode for port.
 * @return EC_SUCCESS, or non-zero if error.
 */
int usb_charge_set_mode(int usb_port_id, enum usb_charge_mode mode);

/**
 * Return a bitmask of which USB ports are enabled.
 *
 * If bit (1 << i) is set, port <i> is enabled.  If it is clear, port <i> is
 * in USB_CHARGE_MODE_DISABLED.
 */
int usb_charge_ports_enabled(void);

#ifdef HAS_TASK_USB_CHG_P0
#define USB_CHG_EVENT_BC12 TASK_EVENT_CUSTOM(1)
#define USB_CHG_EVENT_VBUS TASK_EVENT_CUSTOM(2)
#define USB_CHG_EVENT_INTR TASK_EVENT_CUSTOM(4)
#endif

/*
 * Define USB_CHG_PORT_TO_TASK_ID() and TASK_ID_TO_USB_CHG__PORT() macros to
 * go between USB_CHG port number and task ID. Assume that TASK_ID_USB_CHG_P0,
 * is the lowest task ID and IDs are on a continuous range.
 */
#ifdef HAS_TASK_USB_CHG_P0
#define USB_CHG_PORT_TO_TASK_ID(port) (TASK_ID_USB_CHG_P0 + (port))
#define TASK_ID_TO_USB_CHG_PORT(id) ((id) - TASK_ID_USB_CHG_P0)
#else
#define USB_CHG_PORT_TO_TASK_ID(port) -1 /* dummy task ID */
#define TASK_ID_TO_USB_CHG_PORT(id) 0
#endif  /* HAS_TASK_USB_CHG_P0 */

/**
 * Returns true if the passed port is a power source.
 *
 * @param port  Port number.
 * @return      True if port is sourcing vbus.
 */
int usb_charger_port_is_sourcing_vbus(int port);

enum usb_switch {
	USB_SWITCH_CONNECT,
	USB_SWITCH_DISCONNECT,
	USB_SWITCH_RESTORE,
};

/**
 * Configure USB data switches on type-C port.
 *
 * @param port port number.
 * @param setting new switch setting to configure.
 */
void usb_charger_set_switches(int port, enum usb_switch setting);

/**
 * Notify USB_CHG task that VBUS level has changed.
 *
 * @param port port number.
 * @param vbus_level new VBUS level
 */
void usb_charger_vbus_change(int port, int vbus_level);

#endif  /* __CROS_EC_USB_CHARGE_H */
