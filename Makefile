CC := cc
ASSEMBLER := ./tools/fasm

CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector
CFLAGS += -mno-red-zone -maccumulate-outgoing-args
CFLAGS += -fno-pic -mcmodel=kernel -fno-pie -fshort-wchar

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)

assembly_source := $(wildcard *.s)
assembly_objects := $(assembly_source:%.s=%.o)
#assembly := $(filter-out start.o, $(assembly_objects))


all: pkernel

%.o : %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) -c $<

%.o : %.s
	@echo "Assembling $@"
	$(ASSEMBLER) $< $@

drivers.a:
	make -C ./drivers

# start.o: start.s
# 	$(ASSEMBLER) start.s start.o

pkernel: $(OBJS) $(assembly_objects) drivers.a
	@echo "Finish!"
	@echo "You have pkernel"
	ld $(OBJS) $(assembly_objects) ./drivers/drivers.a -T binary.ld -o pkernel

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
