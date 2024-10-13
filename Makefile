CC := clang
LD := ld.lld

CFLAGS := -ffreestanding -MMD -mno-red-zone -std=c11 \
	-target x86_64-unknown-windows
LDFLAGS := -flavor link -subsystem:efi_application -entry:efi_main

SRCS := main.c

default: all

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

pkernel: main.o
	$(LD) $(LDFLAGS) $< -out:/root/virtual_machine/disk/$@ -verbose

-include $(SRCS:.c=.d)

install:
	cp /root/virtual_machine/disk/pkernel /boot/pkernel

.PHONY: clean all default install

all: pkernel 
