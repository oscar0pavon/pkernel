CC := cc
CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector -fPIC -fshort-wchar -mno-red-zone -maccumulate-outgoing-args

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)


all: pkernel

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -c $<

ps2_keyboard.o: ./drivers/ps2_keyboard.c
	$(CC)	$(CFLAGS) -c ./drivers/ps2_keyboard.c

binary_interface.o: binary_interface.s
	fasm binary_interface.s binary_interface.o

pboot.efi:
	make -C ./boot

pkernel: pboot.efi binary_interface.o kernel.o
	ld binary_interface.o kernel.o -T binary.ld -o pkernel


release:
	cp pkernel /boot
	cp pboot /boot

install:
	cp pkernel /root/virtual_machine/disk/
	cp pboot /root/virtual_machine/disk/

clean:
	make -C boot clean
	rm -f *.o
	rm -f pkernel
	rm -f pboot

