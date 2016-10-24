# -*- makefile -*-
# Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# NPCX chip specific files build
#

# NPCX SoC has a Cortex-M4F ARM core
CORE:=cortex-m
# Allow the full Cortex-M4 instruction set
CFLAGS_CPU+=-march=armv7e-m -mcpu=cortex-m4

# Required chip modules
chip-y=header.o clock.o gpio.o hwtimer.o jtag.o system.o uart.o

# Optional chip modules
chip-$(CONFIG_ADC)+=adc.o
chip-$(CONFIG_FANS)+=fan.o
chip-$(CONFIG_FLASH)+=flash.o
chip-$(CONFIG_I2C)+=i2c.o
chip-$(CONFIG_LPC)+=lpc.o
chip-$(CONFIG_PECI)+=peci.o
chip-$(CONFIG_HOSTCMD_SPS)+=shi.o
# pwm functions are implemented with the fan functions
chip-$(CONFIG_PWM)+=pwm.o
chip-$(CONFIG_SPI)+=spi.o
chip-$(CONFIG_WATCHDOG)+=watchdog.o
chip-$(HAS_TASK_KEYSCAN)+=keyboard_raw.o

# spi flash program fw for openocd
npcx-flash-fw=chip/npcx/spiflashfw/ec_npcxflash
npcx-flash-fw-bin=${out}/$(npcx-flash-fw).bin
PROJECT_EXTRA+=${npcx-flash-fw-bin}

# ECST tool is for filling the header used by booter of npcx EC
show_esct_cmd=$(if $(V),,echo '  ECST   ' $(subst $(out)/,,$@) ; )

# ECST options for header
bld_ecst=${out}/util/ecst -chip $(CHIP_VARIANT) -usearmrst -mode bt -ph -i $(1) -o $(2) -nohcrc \
-nofcrc -flashsize 8 -spimaxclk 50 -spireadmode dual 1> /dev/null

# Replace original one with the flat file including header
moveflat=mv -f $(1) $(2)

# Commands for ECST
cmd_ecst=$(show_esct_cmd)$(call moveflat,$@,$@.tmp);$(call bld_ecst,$@.tmp,$@)

# Commands to append npcx header in ec.RO.flat
cmd_org_ec_elf_to_flat = $(OBJCOPY) --set-section-flags .roshared=share \
                         -O binary $(patsubst %.flat,%.elf,$@) $@
cmd_npcx_ro_elf_to_flat=$(cmd_org_ec_elf_to_flat);$(cmd_ecst)
cmd_ec_elf_to_flat = $(if $(filter $(out)/RO/ec.RO.flat, $@), \
                     $(cmd_npcx_ro_elf_to_flat), $(cmd_org_ec_elf_to_flat) )
