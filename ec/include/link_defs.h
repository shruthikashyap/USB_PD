/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Symbols from linker definitions
 */

#ifndef __CROS_EC_LINK_DEFS_H
#define __CROS_EC_LINK_DEFS_H

#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "task.h"
#include "test_util.h"

/* Console commands */
extern const struct console_command __cmds[];
extern const struct console_command __cmds_end[];

/* Extension commands. */
extern const void *__extension_cmds;
extern const void *__extension_cmds_end;

/* Hooks */
extern const struct hook_data __hooks_init[];
extern const struct hook_data __hooks_init_end[];
extern const struct hook_data __hooks_pre_freq_change[];
extern const struct hook_data __hooks_pre_freq_change_end[];
extern const struct hook_data __hooks_freq_change[];
extern const struct hook_data __hooks_freq_change_end[];
extern const struct hook_data __hooks_sysjump[];
extern const struct hook_data __hooks_sysjump_end[];
extern const struct hook_data __hooks_chipset_pre_init[];
extern const struct hook_data __hooks_chipset_pre_init_end[];
extern const struct hook_data __hooks_chipset_startup[];
extern const struct hook_data __hooks_chipset_startup_end[];
extern const struct hook_data __hooks_chipset_resume[];
extern const struct hook_data __hooks_chipset_resume_end[];
extern const struct hook_data __hooks_chipset_suspend[];
extern const struct hook_data __hooks_chipset_suspend_end[];
extern const struct hook_data __hooks_chipset_shutdown[];
extern const struct hook_data __hooks_chipset_shutdown_end[];
extern const struct hook_data __hooks_chipset_reset[];
extern const struct hook_data __hooks_chipset_reset_end[];
extern const struct hook_data __hooks_ac_change[];
extern const struct hook_data __hooks_ac_change_end[];
extern const struct hook_data __hooks_lid_change[];
extern const struct hook_data __hooks_lid_change_end[];
extern const struct hook_data __hooks_pwrbtn_change[];
extern const struct hook_data __hooks_pwrbtn_change_end[];
extern const struct hook_data __hooks_charge_state_change[];
extern const struct hook_data __hooks_charge_state_change_end[];
extern const struct hook_data __hooks_battery_soc_change[];
extern const struct hook_data __hooks_battery_soc_change_end[];
extern const struct hook_data __hooks_tick[];
extern const struct hook_data __hooks_tick_end[];
extern const struct hook_data __hooks_second[];
extern const struct hook_data __hooks_second_end[];

/* Deferrable functions and firing times*/
extern const struct deferred_data __deferred_funcs[];
extern const struct deferred_data __deferred_funcs_end[];
extern uint64_t __deferred_until[];
extern uint64_t __deferred_until_end[];

/* I2C fake devices for unit testing */
extern const struct test_i2c_xfer __test_i2c_xfer[];
extern const struct test_i2c_xfer __test_i2c_xfer_end[];

/* Host commands */
extern const struct host_command __hcmds[];
extern const struct host_command __hcmds_end[];

/* MKBP events */
extern const struct mkbp_event_source __mkbp_evt_srcs[];
extern const struct mkbp_event_source __mkbp_evt_srcs_end[];

/* IRQs (interrupt handlers) */
extern const struct irq_priority __irqprio[];
extern const struct irq_priority __irqprio_end[];
extern const void *__irqhandler[];

/* Shared memory buffer.  Use via shared_mem.h interface. */
extern uint8_t __shared_mem_buf[];

/* Image sections. */
extern const void *__ro_end;
extern const void *__data_start;
extern const void *__data_end;

#endif /* __CROS_EC_LINK_DEFS_H */
