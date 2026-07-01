include include.Makefile

SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)

assembly_source := $(wildcard *.s)
assembly_objects := $(assembly_source:%.s=%_asm.o)


all: pkernel

%.o : %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) -c $<

%_asm.o : %.s
	@echo "Assembling $@"
	$(ASSEMBLER) $< $@

drivers.a:
	make -C ./drivers/

assembly.a:
	make -C ./asm/


pkernel: $(OBJS) drivers.a assembly.a
	@echo "Finish!"
	@echo "You have pkernel"
	ld $(OBJS) ./asm/assembly.a ./drivers/drivers.a -T binary.ld -o pkernel

install:
	cp pkernel ../virtual_machine/disk/pkernel
	cp pkernel /boot/pkernel
	@echo "Installed on /boot/pkernel"

clean:
	rm -f *.o
	rm -f pkernel
	rm -f ../virtual_machine/disk/pkernel
	make -C ./drivers/ clean
	make -C ./asm/ clean
	@echo "Clean"

$(LOG).SILENT:
