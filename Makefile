CCPREFIX = arm-none-eabi-

CC = $(CCPREFIX)gcc

OBJCOPY = $(CCPREFIX)objcopy

OBJDUMP = $(CCPREFIX)objdump

OPT ?= 2

DEBUG ?= -g

CPU_FAMILY ?= cortex-m0

CPU ?= STM32F051K6

WARN ?= -Wall

DEFINES ?= -D$(CPU) -DSTM32F0XX_MD -DUSE_STDPERIPH_DRIVER -D__ASSEMBLY__

INCLUDES ?= -I. -I./stm32_lib -I./cmsis_boot -I./cmsis_core -I./stm32_lib/inc

CFLAGS ?= -mcpu=$(CPU_FAMILY) -mthumb $(WARN) -ffunction-sections $(DEBUG) -O$(OPT) $(DEFINES) $(INCLUDES)

LIB = $(wildcard ./stm32_lib/src/*.c) $(wildcard ./cmsis_boot/*.c) $(wildcard ./cmsis_boot/startup/*.s) $(wildcard ./syscalls/*.c)

PROG ?= test

SRC ?= main.c

LDFLAGS ?= -mcpu=$(CPU_FAMILY) -mthumb $(DEBUG) -nostartfiles -Wl,-Map=$(PROG).map -Wl,--gc-sections -Wl,-T./link.ld

build: $(PROG).hex $(PROG).lst

.SUFFIXES: .xbm

.s.o:
	$(COMPILE.c) $(OUTPUT_OPTION) $<

$(PROG).elf: $(SRC:.c=.o) $(patsubst %.s,%.o,$(LIB:.c=.o)) $(patsubst %.xbm,%.o,$(wildcard *.xbm))
	$(LINK.c) $^ $(LDLIBS) -o $@

$(PROG).hex: $(PROG).elf
	$(OBJCOPY) -O ihex $< $@

$(PROG).lst: $(PROG).elf
	$(OBJDUMP) -St $< > $@

XBMFLAGS=-Dstatic= -Dunsigned=const -x c

.xbm.o:
	$(COMPILE.c) $(XBMFLAGS) $(OUTPUT_OPTION) $<

.xbm.h:
	head -n2 < $< > $@
	name="$$(sed -n '/define.*width/{s/_.*//g;s/.* //g;p}' $<)"; echo "extern const char $${name}_bits[][$${name}_width / 8];" >> $@

program: $(PROG).hex
	stm32flash -w $< /dev/ttyUSB0
