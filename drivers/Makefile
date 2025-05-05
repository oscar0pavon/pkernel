CC := cc
ASSEMBLER := ./bin/fasm

CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector
CFLAGS += -fPIC -fshort-wchar -mno-red-zone -maccumulate-outgoing-args

source := $(wildcard *.c)
objets := $(source:c=o)

all: drivers.a

%.o : %.c
	@echo "Compiling $@"
	$(CC) $(CFLAGS) -c $<

drivers.a: $(objets)
	@echo "Archiving $@"
	ar rcs $@ $(objets)

clean:
	rm -f *.o

$(LOG).SILENT:
