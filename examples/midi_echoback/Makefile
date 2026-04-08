all : flash

TARGET:=midi_echoback

# CH32V006 - change to CH32V003 if needed
TARGET_MCU?=CH32V006

# Path to ch32fun framework
CH32FUN:=../../rv003usb/ch32fun/ch32fun

include $(CH32FUN)/ch32fun.mk

flash : cv_flash
clean : cv_clean
