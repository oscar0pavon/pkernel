CC := cc
ASSEMBLER := ./bin/fasm

CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector
CFLAGS += -fPIC -fshort-wchar -mno-red-zone -maccumulate-outgoing-args

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)


assembly_source := $(wildcard *.s)
assembly_objects := $(assembly_source:%.s=%.o)
assembly := $(filter-out binary_interface.o, $(assembly_objects))

all: pkernel

%.o : %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) -c $<

%.o : %.s
	@echo "Assembling $@"
	$(ASSEMBLER) $< $@

ps2_keyboard.o: ./drivers/ps2_keyboard.c
	$(CC)	$(CFLAGS) -c ./drivers/ps2_keyboard.c

pci.o: ./drivers/pci.c
	$(CC)	$(CFLAGS) -c ./drivers/pci.c

pci_asm.o: ./drivers/pci_asm.s
	$(ASSEMBLER) ./drivers/pci_asm.s ./pci_asm.o

binary_interface.o: binary_interface.s
	$(ASSEMBLER) binary_interface.s binary_interface.o

# hexadecimal.o: hexadecimal.s
# 	$(ASSEMBLER) hexadecimal.s hexadecimal.o
#
# input_output.o: input_output.s
# 	$(ASSEMBLER) input_output.s input_output.o

#assembly := input_output.o hexadecimal.o pci_asm.o
assembly += pci_asm.o
drivers := ps2_keyboard.o pci.o




pkernel: binary_interface.o $(OBJS) $(assembly) $(drivers)
	@echo "Finish!"
	@echo "You have pkernel"
	ld binary_interface.o $(OBJS) $(assembly) $(drivers) -T binary.ld -o pkernel

install:
	cp pkernel /root/pboot/virtual_machine/disk/pkernel
	cp pkernel /boot/pkernel

clean:
	rm -f *.o
	rm -f pkernel
	rm -f ./virtual_machine/disk/pkernel

$(LOG).SILENT:
