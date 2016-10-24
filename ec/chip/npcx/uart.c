/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* UART module for Chrome EC */

#include "clock.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "lpc.h"
#include "registers.h"
#include "clock_chip.h"
#include "system.h"
#include "task.h"
#include "uart.h"
#include "util.h"

static int init_done;

int uart_init_done(void)
{
	return init_done;
}

void uart_tx_start(void)
{
	if (uart_is_enable_wakeup()) {
		/* disable MIWU */
		uart_enable_wakeup(0);
		/* Set pin-mask for UART */
		npcx_gpio2uart();
		/* enable uart again from MIWU mode */
		task_enable_irq(NPCX_IRQ_UART);
	}

	/* If interrupt is already enabled, nothing to do */
	if (NPCX_UICTRL & 0x20)
		return;

	/* Do not allow deep sleep while transmit in progress */
	disable_sleep(SLEEP_MASK_UART);

	/*
	 * Re-enable the transmit interrupt, then forcibly trigger the
	 * interrupt.  This works around a hardware problem with the
	 * UART where the FIFO only triggers the interrupt when its
	 * threshold is _crossed_, not just met.
	 */
	NPCX_UICTRL |= 0x20;

	task_trigger_irq(NPCX_IRQ_UART);
}

void uart_tx_stop(void)	/* Disable TX interrupt */
{
	NPCX_UICTRL &= ~0x20;

	/* Re-allow deep sleep */
	enable_sleep(SLEEP_MASK_UART);
}

void uart_tx_flush(void)
{
	/* Wait for transmit FIFO empty */
	while (!(NPCX_UICTRL & 0x01))
		;
	/* Wait for transmitting completed */
	while (NPCX_USTAT & 0x40)
		;
}

int uart_tx_ready(void)
{
	return NPCX_UICTRL & 0x01;	/*if TX FIFO is empty return 1*/
}

int uart_tx_in_progress(void)
{
	/* Transmit is in progress if the TX busy bit is set. */
	return NPCX_USTAT & 0x40;	/*BUSY bit , if busy return 1*/
}

int uart_rx_available(void)
{
	uint8_t ctrl = NPCX_UICTRL;
#ifdef CONFIG_LOW_POWER_IDLE
	/*
	 * Activity seen on UART RX pin while UART was disabled for deep sleep.
	 * The console won't see that character because the UART is disabled,
	 * so we need to inform the clock module of UART activity ourselves.
	 */
	if (ctrl & 0x02)
		clock_refresh_console_in_use();
#endif
	return ctrl & 0x02; /* If RX FIFO is empty return '0'*/
}

void uart_write_char(char c)
{
	/* Wait for space in transmit FIFO. */
	while (!uart_tx_ready())
		;

	NPCX_UTBUF = c;
}

int uart_read_char(void)
{
	return NPCX_URBUF;
}

static void uart_clear_rx_fifo(int channel)
{
	int scratch __attribute__ ((unused));
	if (channel == 0) { /* suppose '0' is EC UART*/
		/*if '1' that mean have a RX data on the FIFO register*/
		while ((NPCX_UICTRL & 0x02))
			scratch = NPCX_URBUF;
	}
}

/**
 * Interrupt handler for UART0
 */
void uart_ec_interrupt(void)
{
	/* Read input FIFO until empty, then fill output FIFO */
	uart_process_input();
	uart_process_output();
}
DECLARE_IRQ(NPCX_IRQ_UART, uart_ec_interrupt, 1);


static void uart_config(void)
{
	/* Configure pins from GPIOs to CR_UART */
	gpio_config_module(MODULE_UART, 1);
	/* Enable MIWU IRQ of UART*/
#if NPCX_UART_MODULE2
	task_enable_irq(NPCX_IRQ_WKINTG_1);
#else
	task_enable_irq(NPCX_IRQ_WKINTB_1);

#endif

	/* Fix baud rate to 115200 */
#if   (OSC_CLK == 50000000)
	NPCX_UPSR = 0x10;
	NPCX_UBAUD = 0x08;
#elif (OSC_CLK == 48000000)
	NPCX_UPSR = 0x08;
	NPCX_UBAUD = 0x0C;
#elif (OSC_CLK == 40000000)
	NPCX_UPSR = 0x30;
	NPCX_UBAUD = 0x02;
#elif (OSC_CLK == 33000000) /* APB2 is the same as core clock */
	NPCX_UPSR = 0x08;
	NPCX_UBAUD = 0x11;
#elif (OSC_CLK == 24000000)
	NPCX_UPSR = 0x60;
	NPCX_UBAUD = 0x00;
#elif (OSC_CLK == 15000000) /* APB2 is the same as core clock */
	NPCX_UPSR = 0x38;
	NPCX_UBAUD = 0x01;
#elif (OSC_CLK == 13000000) /* APB2 is the same as core clock */
	NPCX_UPSR = 0x30;
	NPCX_UBAUD = 0x01;
#else
#error "Unsupported Core Clock Frequency"
#endif


	/*
	 * 8-N-1, FIFO enabled.  Must be done after setting
	 * the divisor for the new divisor to take effect.
	 */
	NPCX_UFRS = 0x00;
	NPCX_UICTRL = 0x40; /* receive int enable only */
}

void uart_init(void)
{
	uint32_t mask = 0;

	/*
	 * Enable UART0 in run, sleep, and deep sleep modes. Enable the Host
	 * UART in run and sleep modes.
	 */
	mask = 0x10; /* bit 4 */
	clock_enable_peripheral(CGC_OFFSET_UART, mask, CGC_MODE_ALL);

	/* Set pin-mask for UART */
	npcx_gpio2uart();

	/* Configure UARTs (identically) */
	uart_config();

	/*
	 * Enable interrupts for UART0 only. Host UART will have to wait
	 * until the LPC bus is initialized.
	 */
	uart_clear_rx_fifo(0);
	task_enable_irq(NPCX_IRQ_UART);

	init_done = 1;
}
