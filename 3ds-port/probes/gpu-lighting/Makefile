DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM
OUT ?= $(abspath ../../../3ds-work/probes/gpu-lighting)
CC := $(DEVKITARM)/bin/arm-none-eabi-gcc
TOOLS := $(DEVKITPRO)/tools/bin
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations
CFLAGS := -O2 -Wall -std=gnu11 $(ARCH) -fno-fast-math -frounding-math -ffp-contract=off -I$(DEVKITPRO)/libctru/include -I$(OUT)
.PHONY: all
all: $(OUT)/gpu-lighting.3dsx
$(OUT):
	mkdir -p $@
$(OUT)/vshader.shbin: lighting.v.pica | $(OUT)
	$(TOOLS)/picasso -o $@ $<
$(OUT)/vshader.s: $(OUT)/vshader.shbin
	$(TOOLS)/bin2s -a 4 -H $(OUT)/vshader_shbin.h $< > $@
$(OUT)/vshader.o: $(OUT)/vshader.s
	$(CC) $(ARCH) -c $< -o $@
$(OUT)/main.o: main.c $(OUT)/vshader.s
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/gpu-lighting.elf: $(OUT)/main.o $(OUT)/vshader.o
	$(CC) -specs=3dsx.specs $(ARCH) -Wl,--gc-sections $^ -L$(DEVKITPRO)/libctru/lib -lcitro3d -lctru -lm -o $@
$(OUT)/gpu-lighting.3dsx: $(OUT)/gpu-lighting.elf
	$(TOOLS)/3dsxtool $< $@
