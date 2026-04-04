all : build

TARGET:=prog_midi_v003
CH32FUN:=../rv003usb/ch32fun/ch32fun
TARGET_MCU:=CH32V003
DIST_DIR:=../dist

ADDITIONAL_C_FILES+=../rv003usb/rv003usb/rv003usb.S ../rv003usb/rv003usb/rv003usb.c
EXTRA_CFLAGS:=-I../rv003usb/lib -I../rv003usb/rv003usb

include $(CH32FUN)/ch32fun.mk

build : $(TARGET).bin
	@mkdir -p $(DIST_DIR)
	mv $(TARGET).bin $(TARGET).hex $(TARGET).lst $(TARGET).map $(TARGET).elf $(TARGET)_ext.bin $(DIST_DIR)/

flash : cv_flash
clean : cv_clean


