CC := clang
LD := ld.lld

CFLAGS := -ffreestanding -MMD -mno-red-zone -std=c11 \
	-target x86_64-unknown-windows
LDFLAGS := -flavor link -subsystem:efi_application -entry:efi_main

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)

.PHONY: all clean install

all: pkernel

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -c $<

kernel.elf:
	fasm kernel.s
	ld kernel.o -o kernel.elf

pkernel: $(OBJS) kernel.elf
	$(LD) $(LDFLAGS) ${OBJS} -out:/root/virtual_machine/disk/pkernel #-verbose 

#-include $(SRCS:.c=.d)

release:
	cp /root/virtual_machine/disk/pkernel /boot/pkernel

install:
	cp kernel.elf /root/virtual_machine/disk/kernel.elf	

clean:
	rm -f *.o
	rm -f *.d
	rm -f kernel.elf

