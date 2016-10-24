/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Event handling in MKBP keyboard protocol
 */

#include "atomic.h"
#include "chipset.h"
#include "gpio.h"
#include "host_command.h"
#include "link_defs.h"
#include "mkbp_event.h"
#include "util.h"

static uint32_t events;

static void set_event(uint8_t event_type)
{
	atomic_or(&events, 1 << event_type);
}

static void clear_event(uint8_t event_type)
{
	atomic_clear(&events, 1 << event_type);
}

static int event_is_set(uint8_t event_type)
{
	return events & (1 << event_type);
}

/**
 * Assert host keyboard interrupt line.
 */
static void set_host_interrupt(int active)
{
	/* interrupt host by using active low EC_INT signal */
#ifdef CONFIG_MKBP_USE_HOST_EVENT
	if (active)
		host_set_single_event(EC_HOST_EVENT_MKBP);
#else
	gpio_set_level(GPIO_EC_INT_L, !active);
#endif
}

void mkbp_send_event(uint8_t event_type)
{
	set_event(event_type);

#ifdef CONFIG_MKBP_WAKEUP_MASK
	/* checking the event if AP is not in S0 */
	if (!chipset_in_state(CHIPSET_STATE_ON)) {
		uint32_t events;
		events = *(uint32_t *)host_get_memmap(EC_MEMMAP_HOST_EVENTS);
		/*
		 * interrupt the AP if it is a wakeup event
		 * which is defined in the white list.
		 */
		if ((events & CONFIG_MKBP_WAKEUP_MASK) ||
		    (event_type == EC_MKBP_EVENT_KEY_MATRIX))
			set_host_interrupt(1);

		return;
	}
#endif

	set_host_interrupt(1);
}

static int mkbp_get_next_event(struct host_cmd_handler_args *args)
{
	static int last;
	int i, data_size, evt;
	uint8_t *resp = args->response;
	const struct mkbp_event_source *src;

	do {
		/*
		 * Find the next event to service.  We do this in a round-robin
		 * way to make sure no event gets starved.
		 */
		for (i = 0; i < EC_MKBP_EVENT_COUNT; ++i)
			if (event_is_set((last + i) % EC_MKBP_EVENT_COUNT))
				break;

		if (i == EC_MKBP_EVENT_COUNT)
			return EC_RES_UNAVAILABLE;

		evt = (i + last) % EC_MKBP_EVENT_COUNT;
		last = evt + 1;

		/*
		 * Clear the event before retrieving the event data in case the
		 * event source wants to send the same event.
		 */
		clear_event(evt);

		for (src = __mkbp_evt_srcs; src < __mkbp_evt_srcs_end; ++src)
			if (src->event_type == evt)
				break;

		if (src == __mkbp_evt_srcs_end)
			return EC_RES_ERROR;

		resp[0] = evt; /* Event type */

		/*
		 * get_data() can return -EC_ERROR_BUSY which indicates that the
		 * next element in the keyboard FIFO does not match what we were
		 * called with.  For example, get_data is expecting a keyboard
		 * matrix, however the next element in the FIFO is a button
		 * event instead.  Therefore, we have to service that button
		 * event first.
		 */
		data_size = src->get_data(resp + 1);
		if (data_size == -EC_ERROR_BUSY)
			set_event(evt);
	} while (data_size == -EC_ERROR_BUSY);

	if (data_size < 0)
		return EC_RES_ERROR;
	args->response_size = 1 + data_size;

	if (!events)
		set_host_interrupt(0);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_GET_NEXT_EVENT,
		     mkbp_get_next_event,
		     EC_VER_MASK(0));
