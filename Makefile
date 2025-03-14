CC := cc
CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector -fPIC -fshort-wchar -mno-red-zone -maccumulate-outgoing-args

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)

all: pkernel

%.o : %.c
	$(CC) $(CFLAGS) -c $<

ps2_keyboard.o: ./drivers/ps2_keyboard.c
	$(CC)	$(CFLAGS) -c ./drivers/ps2_keyboard.c

binary_interface.o: binary_interface.s
	fasm binary_interface.s binary_interface.o

library.o: boot/library.c
	$(CC)	$(CFLAGS) -c boot/library.c

pkernel: binary_interface.o $(OBJS) library.o
	ld binary_interface.o $(OBJS) library.o -T binary.ld -o pkernel

install:
	cp pkernel /root/pboot/virtual_machine/disk/pkernel

clean:
	rm -f *.o
	rm -f pkernel
	rm -f ./virtual_machine/disk/pkernel

