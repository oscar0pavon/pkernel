CC := cc
ASSEMBLER := ./bin/fasm

CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector
CFLAGS += -fPIC -fshort-wchar -mno-red-zone -maccumulate-outgoing-args

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)

assembly_source := $(wildcard *.s)
assembly_objects := $(assembly_source:%.s=%.o)
assembly := $(filter-out start.o, $(assembly_objects))

all: pkernel

%.o : %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) -c $<

%.o : %.s
	@echo "Assembling $@"
	$(ASSEMBLER) $< $@

drivers.a: ./drivers/drivers.a
	make -C ./drivers

start.o: start.s
	$(ASSEMBLER) start.s start.o

pkernel: start.o $(OBJS) $(assembly) drivers.a
	@echo "Finish!"
	@echo "You have pkernel"
	ld start.o $(OBJS) $(assembly) ./drivers/drivers.a -T binary.ld -o pkernel

install:
	cp pkernel /root/pboot/virtual_machine/disk/pkernel
	cp pkernel /boot/pkernel

clean:
	rm -f *.o
	rm -f pkernel
	rm -f ./virtual_machine/disk/pkernel
	make -C ./drivers/ clean
	@echo "Clean"

$(LOG).SILENT:
