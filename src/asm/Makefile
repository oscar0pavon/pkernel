include ../include.Makefile

assembly_source := $(wildcard *.s)
assembly_objects := $(assembly_source:%.s=%_asm.o)

all: assembly.a

%_asm.o : %.s
	@echo "Assembling $@"
	$(ASSEMBLER) $< $@

assembly.a: $(assembly_objects)
	@echo "Archiving $@"
	ar rcs $@ $(assembly_objects)
	ar d assembly.a start_asm.o

clean:
	rm -f *.o
	rm -f assembly.a

$(LOG).SILENT:
