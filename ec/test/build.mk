# -*- makefile -*-
# Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# on-board test binaries build
#

#test-list-y=pingpong timer_calib timer_dos timer_jump mutex utils
test-list-y=usb_pd
#disable: powerdemo

test-list-$(BOARD_BDS)+=
test-list-$(BOARD_PIT)+=kb_scan stress

# Samus has board-specific chipset code, and the tests don't
# compile with it. Disable them for now.
test-list-$(BOARD_SAMUS)=

# Ryu has issues when building tests
test-list-$(BOARD_RYU)=

# llama has issues when building tests
test-list-$(BOARD_LLAMA)=

# So does Cr50
test-list-$(BOARD_CR50)=

# For some tests, we are running out of RAM. Disable them for now.
test-list-$(BOARD_GLADOS_PD)=
test-list-$(BOARD_CHELL_PD)=
test-list-$(BOARD_OAK_PD)=
test-list-$(BOARD_SAMUS_PD)=

# Emulator tests
ifneq ($(TEST_LIST_HOST),)
test-list-host=$(TEST_LIST_HOST)
else
test-list-host=mutex pingpong utils kb_scan kb_mkbp lid_sw power_button hooks
test-list-host+=thermal flash queue kb_8042 extpwr_gpio console_edit system
test-list-host+=sbs_charging host_command
test-list-host+=bklight_lid bklight_passthru interrupt timer_dos button
test-list-host+=math_util motion_lid sbs_charging_v2 battery_get_params_smart
test-list-host+=lightbar inductive_charging usb_pd fan charge_manager
test-list-host+=charge_manager_drp_charging charge_ramp nvmem
endif

battery_get_params_smart-y=battery_get_params_smart.o
bklight_lid-y=bklight_lid.o
bklight_passthru-y=bklight_passthru.o
button-y=button.o
charge_manager-y=charge_manager.o
charge_manager_drp_charging-y=charge_manager.o
charge_ramp-y+=charge_ramp.o
console_edit-y=console_edit.o
extpwr_gpio-y=extpwr_gpio.o
flash-y=flash.o
hooks-y=hooks.o
host_command-y=host_command.o
inductive_charging-y=inductive_charging.o
interrupt-y=interrupt.o
interrupt-scale=10
kb_8042-y=kb_8042.o
kb_mkbp-y=kb_mkbp.o
kb_scan-y=kb_scan.o
lid_sw-y=lid_sw.o
math_util-y=math_util.o
motion_lid-y=motion_lid.o
mutex-y=mutex.o
nvmem-y=nvmem.o
pingpong-y=pingpong.o
power_button-y=power_button.o
powerdemo-y=powerdemo.o
queue-y=queue.o
sbs_charging-y=sbs_charging.o
sbs_charging_v2-y=sbs_charging_v2.o
stress-y=stress.o
system-y=system.o
thermal-y=thermal.o
timer_calib-y=timer_calib.o
timer_dos-y=timer_dos.o
usb_pd-y=usb_pd.o
utils-y=utils.o
battery_get_params_smart-y=battery_get_params_smart.o
lightbar-y=lightbar.o
fan-y=fan.o
