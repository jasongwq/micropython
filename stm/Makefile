include ../py/mkenv.mk

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# include py core make definitions
include ../py/py.mk

CMSIS_DIR=cmsis
STMPERIPH_DIR=stmperiph
STMUSB_DIR=stmusb
STMUSBD_DIR=stmusbd
STMUSBH_DIR=stmusbh
FATFS_DIR=fatfs
CC3K_DIR=cc3k
DFU=../tools/dfu.py

CROSS_COMPILE = arm-none-eabi-

CFLAGS_CORTEX_M4 = -Wall -mlittle-endian -mthumb -mthumb-interwork -nostartfiles -mcpu=cortex-m4 \
                   -fsingle-precision-constant -Wdouble-promotion -mfpu=fpv4-sp-d16 -mfloat-abi=hard
CFLAGS = -I. -I$(PY_SRC) -I$(CMSIS_DIR) -I$(STMPERIPH_DIR) -I$(STMUSB_DIR) -Wall -ansi -std=gnu99 $(CFLAGS_CORTEX_M4) -DSTM32F40_41xxx
CFLAGS += -I$(STMUSBD_DIR)
CFLAGS += -I$(STMUSBH_DIR)
CFLAGS += -I$(FATFS_DIR)
#CFLAGS += -I$(CC3K_DIR)

BOARD ?= PYBOARD4
ifeq ($(wildcard boards/$(BOARD)/.),)
$(error Invalid BOARD specified)
endif
CFLAGS += -Iboards/$(BOARD)

#Debugging/Optimization
ifeq ($(DEBUG), 1)
CFLAGS += -g -DPENDSV_DEBUG
COPT = -O0
else
COPT += -Os -DNDEBUG
endif

LDFLAGS = --nostdlib -T stm32f405.ld -Map=$(@:.elf=.map) --cref
LIBS =

# uncomment this if you want libgcc
#LIBS += $(shell $(CC) -print-libgcc-file-name)

SRC_C = \
	malloc0.c \
	string0.c \
	printf.c \
	gccollect.c \
	lexerfatfs.c \
	flash.c \
	storage.c \
	usb.c \
	pendsv.c \
	libmp.c \

SRC_S = \
	gchelper.s \

SRC_STMUSB = $(addprefix $(STMUSB_DIR)/,\
	usb_core.c \
	usb_bsp.c \
	usb_dcd.c \
	usb_dcd_int.c \
	usb_hcd.c \
	usb_hcd_int.c \
	)

SRC_STMUSBD = $(addprefix $(STMUSBD_DIR)/,\
	usbd_core.c \
	usbd_ioreq.c \
	usbd_req.c \
	usbd_usr.c \
	usbd_desc.c \
	usbd_pyb_core.c \
	usbd_pyb_core2.c \
	usbd_cdc_vcp.c \
	usbd_msc_bot.c  \
	usbd_msc_data.c \
	usbd_msc_scsi.c \
	usbd_storage_msd.c \
	)


SRC_FATFS = $(addprefix $(FATFS_DIR)/,\
	ff.c \
	diskio.c \
	ccsbcs.c \
	)

OBJ = $(PY_O) $(addprefix $(BUILD)/, $(SRC_C:.c=.o) $(SRC_S:.s=.o) $(SRC_STMUSB:.c=.o) $(SRC_STMUSBD:.c=.o) $(SRC_FATFS:.c=.o))

all: $(BUILD) $(BUILD)/libmp.a

$(BUILD)/libmp.a: $(OBJ)
	$(Q)ar -r $@ $(OBJ)

$(BUILD)/flash.dfu: $(BUILD)/flash0.bin $(BUILD)/flash1.bin
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(DFU) -b 0x08000000:$(BUILD)/flash0.bin -b 0x08020000:$(BUILD)/flash1.bin $@

$(BUILD)/flash0.bin: $(BUILD)/flash.elf
	$(Q)$(OBJCOPY) -O binary -j .isr_vector $^ $@

$(BUILD)/flash1.bin: $(BUILD)/flash.elf
	$(Q)$(OBJCOPY) -O binary -j .text -j .data $^ $@

$(BUILD)/flash.elf: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)
	$(Q)$(SIZE) $@

MAKE_PINS = boards/make-pins.py
BOARD_PINS = boards/$(BOARD)/pins.csv
AF_FILE = boards/stm32f4xx-af.csv
PREFIX_FILE = boards/stm32f4xx-prefix.c
GEN_PINS_SRC = $(BUILD)/pins_$(BOARD).c
GEN_PINS_HDR = $(BUILD)/pins.h

# Making OBJ use an order-only depenedency on the generated pins.h file
# has the side effect of making the pins.h file before we actually compile
# any of the objects. The normal dependency generation will deal with the
# case when pins.h is modified. But when it doesn't exist, we don't know
# which source files might need it.
$(OBJ): | $(BUILD)/pins.h

# Use a pattern rule here so that make will only call make-pins.py once to make
# both pins_$(BOARD).c and pins.h
$(BUILD)/%_$(BOARD).c $(BUILD)/%.h: boards/$(BOARD)/%.csv $(MAKE_PINS) $(AF_FILE) $(PREFIX_FILE)
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(MAKE_PINS) --board $(BOARD_PINS) --af $(AF_FILE) --prefix $(PREFIX_FILE) --hdr $(GEN_PINS_HDR) > $(GEN_PINS_SRC)

$(BUILD)/pins_$(BOARD).o: $(BUILD)/pins_$(BOARD).c
	$(call compile_c)

include ../py/mkrules.mk
