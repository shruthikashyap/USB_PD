/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB Power delivery port management */

#ifndef __CROS_EC_USB_PD_TCPM_H
#define __CROS_EC_USB_PD_TCPM_H

/* Default retry count for transmitting */
#define PD_RETRY_COUNT 3

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT  (100*MSEC)

enum tcpc_cc_voltage_status {
	TYPEC_CC_VOLT_OPEN = 0,
	TYPEC_CC_VOLT_RA = 1,
	TYPEC_CC_VOLT_RD = 2,
	TYPEC_CC_VOLT_SNK_DEF = 5,
	TYPEC_CC_VOLT_SNK_1_5 = 6,
	TYPEC_CC_VOLT_SNK_3_0 = 7,
};

enum tcpc_cc_pull {
	TYPEC_CC_RA = 0,
	TYPEC_CC_RP = 1,
	TYPEC_CC_RD = 2,
	TYPEC_CC_OPEN = 3,
};

enum tcpm_transmit_type {
	TCPC_TX_SOP = 0,
	TCPC_TX_SOP_PRIME = 1,
	TCPC_TX_SOP_PRIME_PRIME = 2,
	TCPC_TX_SOP_DEBUG_PRIME = 3,
	TCPC_TX_SOP_DEBUG_PRIME_PRIME = 4,
	TCPC_TX_HARD_RESET = 5,
	TCPC_TX_CABLE_RESET = 6,
	TCPC_TX_BIST_MODE_2 = 7
};

enum tcpc_transmit_complete {
	TCPC_TX_COMPLETE_SUCCESS =   0,
	TCPC_TX_COMPLETE_DISCARDED = 1,
	TCPC_TX_COMPLETE_FAILED =    2,
};

struct tcpm_drv {
	/**
	 * Initialize TCPM driver and wait for TCPC readiness.
	 *
	 * @param port Type-C port number
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*init)(int port);

	/**
	 * Read the CC line status.
	 *
	 * @param port Type-C port number
	 * @param cc1 pointer to CC status for CC1
	 * @param cc2 pointer to CC status for CC2
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_cc)(int port, int *cc1, int *cc2);

	/**
	 * Read VBUS
	 *
	 * @param port Type-C port number
	 *
	 * @return 0 => VBUS not detected, 1 => VBUS detected
	 */
	int (*get_vbus_level)(int port);

	/**
	 * Set the CC pull resistor. This sets our role as either source or sink.
	 *
	 * @param port Type-C port number
	 * @param pull One of enum tcpc_cc_pull
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_cc)(int port, int pull);

	/**
	 * Set polarity
	 *
	 * @param port Type-C port number
	 * @param polarity 0=> transmit on CC1, 1=> transmit on CC2
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_polarity)(int port, int polarity);

	/**
	 * Set Vconn.
	 *
	 * @param port Type-C port number
	 * @param polarity Polarity of the CC line to read
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_vconn)(int port, int enable);

	/**
	 * Set PD message header to use for goodCRC
	 *
	 * @param port Type-C port number
	 * @param power_role Power role to use in header
	 * @param data_role Data role to use in header
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_msg_header)(int port, int power_role, int data_role);

	/**
	 * Set RX enable flag
	 *
	 * @param port Type-C port number
	 * @enable true for enable, false for disable
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*set_rx_enable)(int port, int enable);

	/**
	 * Read last received PD message.
	 *
	 * @param port Type-C port number
	 * @param payload Pointer to location to copy payload of message
	 * @param header of message
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*get_message)(int port, uint32_t *payload, int *head);

	/**
	 * Transmit PD message
	 *
	 * @param port Type-C port number
	 * @param type Transmit type
	 * @param header Packet header
	 * @param cnt Number of bytes in payload
	 * @param data Payload
	 *
	 * @return EC_SUCCESS or error
	 */
	int (*transmit)(int port, enum tcpm_transmit_type type, uint16_t header,
					const uint32_t *data);

	/**
	 * TCPC is asserting alert
	 *
	 * @param port Type-C port number
	 */
	void (*tcpc_alert)(int port);
};

enum tcpc_alert_polarity {
	TCPC_ALERT_ACTIVE_LOW,
	TCPC_ALERT_ACTIVE_HIGH,
};

struct tcpc_config_t {
	int i2c_host_port;
	int i2c_slave_addr;
	const struct tcpm_drv *drv;
	enum tcpc_alert_polarity pol;
};

/**
 * Returns the PD_STATUS_TCPC_ALERT_* mask corresponding to the TCPC ports
 * that are currently asserting ALERT.
 *
 * @return     PD_STATUS_TCPC_ALERT_* mask.
 */
uint16_t tcpc_get_alert_status(void);

/**
 * Initialize TCPC.
 *
 * @param port Type-C port number
 */
void tcpc_init(int port);

/**
 * TCPC is asserting alert
 *
 * @param port Type-C port number
 */
void tcpc_alert_clear(int port);

/**
 * Run TCPC task once. This checks for incoming messages, processes
 * any outgoing messages, and reads CC lines.
 *
 * @param port Type-C port number
 * @param evt Event type that woke up this task
 */
int tcpc_run(int port, int evt);

#endif /* __CROS_EC_USB_PD_TCPM_H */
