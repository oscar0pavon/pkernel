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


pkernel: $(OBJS)
	$(LD) $(LDFLAGS) ${OBJS} -out:/root/virtual_machine/disk/pkernel #-verbose 

#-include $(SRCS:.c=.d)

install:
	cp /root/virtual_machine/disk/pkernel /boot/pkernel

clean:
	rm *.o
	rm *.d

