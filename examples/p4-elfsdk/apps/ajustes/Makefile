# APP -- Material/gfx (sin LVGL)
TOPDIR = ../../../..
CC = riscv32-unknown-elf-gcc
LD = riscv32-unknown-elf-ld
CFLAGS = -Os -fno-builtin -fno-pic -ffunction-sections -std=gnu11 \
         -march=rv32imaf_zicsr_zifencei -mabi=ilp32f -mno-relax \
         -msmall-data-limit=0 -Wall \
         -I$(TOPDIR) -I$(TOPDIR)/include -I$(TOPDIR)/fatfs \
         -I$(TOPDIR)/system/platforms/esp32p4 -I../lib
APP  = ajustes
OBJS = ajustes.o ../lib/gestos.o ../lib/ui.o ../lib/start.o
all: $(APP).elf
$(APP).elf: $(OBJS)
	$(LD) -r -o $@ $(OBJS)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
../lib/gestos.o: ../lib/gestos.c ../lib/gestos.h
	$(CC) $(CFLAGS) -c -o $@ $<
../lib/ui.o: ../lib/ui.c ../lib/ui.h
	$(CC) $(CFLAGS) -c -o $@ $<
../lib/start.o: ../lib/start.S
	$(CC) $(CFLAGS) -c -o $@ $<
clean:
	rm -f ajustes.o ../lib/gestos.o ../lib/ui.o ../lib/start.o ajustes.elf
.PHONY: all clean
