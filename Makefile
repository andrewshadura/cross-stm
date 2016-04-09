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

X ?= 24

ifeq ($(X),16)
DEFINES += -DRCC_CFGR_PLLMULLX=RCC_CFGR_PLLMULL3
endif

ifeq ($(X),24)
DEFINES += -DRCC_CFGR_PLLMULLX=RCC_CFGR_PLLMULL2
endif

INCLUDES ?= -I. -I./stm32_lib -I./cmsis_boot -I./cmsis_core -I./stm32_lib/inc

CFLAGS ?= -mcpu=$(CPU_FAMILY) -mthumb $(WARN) -ffunction-sections $(DEBUG) -O$(OPT) $(DEFINES) $(INCLUDES)

LIB = $(wildcard ./stm32_lib/src/*.c) $(wildcard ./cmsis_boot/*.c) $(wildcard ./cmsis_boot/startup/*.s) $(wildcard ./syscalls/*.c)

PROG ?= cross

SRC ?= main.c

LDFLAGS ?= -mcpu=$(CPU_FAMILY) -mthumb $(DEBUG) -nostartfiles -Wl,-Map=$(PROG).map -Wl,--gc-sections -Wl,-T./link.ld

ALLCONFIGS ?= $(patsubst menu-%.xcf,%,$(wildcard menu-*.xcf))

CONFIGS = $(sort $(foreach config,$(ALLCONFIGS),$(word 1,$(subst -, ,$(config)))))
LANGUAGES = $(sort $(foreach config,$(ALLCONFIGS),$(word 2,$(subst -, ,$(config)))))

TARGETS = $(CONFIGS:%=$(PROG)-%.hex)

LISTINGS = $(TARGETS:.hex=.lst)

build: $(TARGETS) $(LISTINGS)

.SUFFIXES: .xbm

e:
	echo $(LANGUAGES) → $(CONFIGS), $(TARGETS)

.s.o:
	$(COMPILE.c) $(OUTPUT_OPTION) $<

$(PROG)-%.elf: $(SRC:%.c=%)-%.o $(patsubst %.s,%.o,$(LIB:.c=.o)) $(patsubst %.xbm,%.o,$(wildcard cross-*.xbm)) $(LANGUAGES:%=menu-\%-%.o) statusbar.o $(LANGUAGES:%=helper-%.o) flash_async.o
	$(LINK.c) $^ $(LDLIBS) -o $@

$(PROG)-%.hex: $(PROG)-%.elf
	$(OBJCOPY) -O ihex $< $@

$(PROG)-%.lst: $(PROG)-%.elf
	$(OBJDUMP) -St $< > $@

#	$(COMPILE.c) $(XBMFLAGS) $(OUTPUT_OPTION) helper-$(word 2,$(subst -, ,$(@:helper-%.o=%))).xbm

helper-%.o: $(wildcard helper-*.xbm)
	$(COMPILE.c) $(XBMFLAGS) $(OUTPUT_OPTION) helper-$*.xbm

main-%.o: main.c $(LANGUAGES:%=helper-%.h)
	$(COMPILE.c) -DCONFIG=$(word 1,$(subst -, ,$(@:main-%.o=%))) -DLANG=$(word 2,$(subst -, ,$(@:main-%.o=%))) $(OUTPUT_OPTION) $<

XBMFLAGS=-Dstatic= -Dunsigned=const -x c

.xbm.o:
	$(COMPILE.c) $(XBMFLAGS) $(OUTPUT_OPTION) $<

.xbm.h:
	head -n2 < $< > $@
	name="$$(sed -n '/define.*width/{s/_width.*//g;s/.* //g;p}' $<)"; echo "extern const char $${name}_bits[][$${name}_width / 8];" >> $@

cross-%.c: cross-%.xbm
	name="$$(sed -n '/define.*width/{s/_width.*//g;s/.* //g;p}' $<)"; xbmtopbm $< | ./sortpnm.tcl $$name > $@

cross-%.h: cross-%.c
	head -n2 < ${<:.c=.xbm} > $@
	sed -n -e 's,const,extern const,g' -e '/const/s, =.*$$,;,gp' $< >> $@

program: $(firstword $(TARGETS))
	stm32flash -R -w $< /dev/ttyUSB0

program-%: %.hex
	stm32flash -w $< /dev/ttyUSB0

export: $(TARGETS:$(PROG)-%.hex=export-%)

DATE:=$(shell date +\%F.\%H.\%M.\%S)

export-%: $(PROG)-%.hex
	cp $< /tmp/cross-$(X)-$(DATE)-$*.hex

clean:
	rm -f main-??-??.o $(patsubst %.s,%.o,$(LIB:.c=.o)) $(patsubst %.xbm,%.o,$(wildcard *.xbm))

.PHONY: cmsis_boot/system_stm32f0xx_temp.c
.SECONDARY: $(LIB:.c=.o)
