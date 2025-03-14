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

assembly := input_output.o
drivers := ps2_keyboard.o

input_output.o: input_output.s
	fasm input_output.s input_output.o


pkernel: binary_interface.o $(OBJS) $(assembly) $(drivers)
	ld binary_interface.o $(OBJS) $(assembly) $(drivers) -T binary.ld -o pkernel

install:
	cp pkernel /root/pboot/virtual_machine/disk/pkernel

clean:
	rm -f *.o
	rm -f pkernel
	rm -f ./virtual_machine/disk/pkernel

