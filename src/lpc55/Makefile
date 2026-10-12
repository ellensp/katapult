# lpc55 build rules

# Setup the toolchain
CROSS_PREFIX=arm-none-eabi-

dirs-y += src/lpc55 src/generic

CFLAGS += -mthumb -mcpu=cortex-m33 -Ilib/lpc55/include -Ilib/cmsis-core

CFLAGS_katapult.elf += -nostdlib -lgcc -lc_nano
CFLAGS_katapult.elf += -T $(OUT)src/generic/armcm_link.ld
$(OUT)katapult.elf: $(OUT)src/generic/armcm_link.ld

# Add source files
mcu-y = lpc55/main.c lpc55/gpio.c lpc55/flash.c
mcu-y += generic/armcm_irq.c generic/armcm_timer.c generic/crc16_ccitt.c

src-y += generic/armcm_canboot.c $(mcu-y)
src-$(CONFIG_USBSERIAL) += lpc55/usbserial.c generic/usb_cdc.c

BUILDBINARY_FLAGS = --lpc55
