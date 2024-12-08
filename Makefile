CC := clang
LD := ld.lld

CFLAGS := -ffreestanding -MMD -mno-red-zone -std=c11 \
	-target x86_64-unknown-windows
LDFLAGS := -flavor link -subsystem:efi_application -entry:efi_main

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)
OBJS := $(filter-out kernel.o, $(OBJS))

GCCFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector -fPIC -fshort-wchar -mno-red-zone -maccumulate-outgoing-args

.PHONY: all clean install

all: pkernel

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -c $<

ps2_keyboard.o: ./drivers/ps2_keyboard.c
	$(CC)	$(CFLAGS) -c ./drivers/ps2_keyboard.c

kernel.o: kernel.c
	cc $(GCCFLAGS) -c kernel.c -o kernel.o

kernel: kernel.o
	ld kernel.o -T binary.ld -o kernel

pkernel: $(OBJS) ps2_keyboard.o kernel
	$(LD) $(LDFLAGS) ${OBJS} ps2_keyboard.o -out:/root/virtual_machine/disk/pkernel #-verbose 

#-include $(SRCS:.c=.d)

release:
	cp /root/virtual_machine/disk/pkernel /boot/pkernel

install:
	cp kernel /root/virtual_machine/disk/

clean:
	rm -f *.o
	rm -f *.d
	rm -f *.bin
	rm -f *.elf
	rm -f *.so

