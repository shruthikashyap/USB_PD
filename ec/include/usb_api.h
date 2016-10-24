/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB API definitions.
 *
 * This file includes definitions needed by common code that wants to control
 * the state of the USB peripheral, but doesn't need to know about the specific
 * implementation.
 */

#ifndef __CROS_EC_USB_API_H
#define __CROS_EC_USB_API_H

/*
 * Initialize the USB peripheral, enabling its clock and configuring the DP/DN
 * GPIOs correctly.  This function is called via an init hook (unless the board
 * defined CONFIG_USB_INHIBIT_INIT), but may need to be called again if
 * usb_release is called.  This function will call usb_connect by default
 * unless CONFIG_USB_INHIBIT_CONNECT is defined.
 */
void usb_init(void);

/* Check if USB peripheral is enabled. */
int usb_is_enabled(void);

/*
 * Enable the pullup on the DP line to signal that this device exists to the
 * host and to start the enumeration process.
 */
void usb_connect(void);

/*
 * Disable the pullup on the DP line.  This causes the device to be disconnected
 * from the host.
 */
void usb_disconnect(void);

/*
 * Disconnect from the host by calling usb_disconnect and then turn off the USB
 * peripheral, releasing its GPIOs and disabling its clock.
 */
void usb_release(void);

#ifdef CONFIG_USB_SELECT_PHY
/* Select which PHY to use. */
void usb_select_phy(uint32_t phy);

/* Get the current PHY */
uint32_t usb_get_phy(void);
#endif
#endif /* __CROS_EC_USB_API_H */
