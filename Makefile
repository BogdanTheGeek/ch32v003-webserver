all : flash

TARGET := main
CH32FUN ?= ch32fun/ch32fun

TARGET_MCU := CH32V003

FSPATH := uip
FSFILES := $(shell find $(FSPATH)/fs/ -type f)

UIPFILES := $(filter-out $(FSPATH)/fsdata.c, $(wildcard uip/*.c))

ADDITIONAL_C_FILES += ./rv003usb/rv003usb.S ./rv003usb/rv003usb.c
ADDITIONAL_C_FILES += $(filter-out $(TARGET).c, $(wildcard *.c))
ADDITIONAL_C_FILES += $(UIPFILES)
EXTRA_CFLAGS:=-I./lib -I./rv003usb -I./uip


include $(CH32FUN)/ch32fun.mk

flash : cv_flash
clean : cv_clean

TTY ?= /dev/ttyACM10

$(FSPATH)/fsdata.c: $(FSFILES)
	@echo "Building filesystem ..."
	cd $(FSPATH) && ./makefsdata

slip:
ifeq ($(shell uname -s),Darwin)
	sudo ./tools/slip-macos/slip -b 115200 -l 192.168.190.1 -r 192.168.190.2 $(TTY)
else
	sudo slattach -L -p slip -s 115200 $(TTY) & \
	sudo ip addr add 192.168.190.1 peer 192.168.190.2/24 dev sl0 && \
   sudo ip link set mtu 1500 up dev sl0
endif

speedtest:
	curl -w "avg_speed: %{speed_download} bytes/s\n" -o /dev/null -s http://192.168.190.2/



