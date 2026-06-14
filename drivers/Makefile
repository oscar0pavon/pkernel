include ../include.Makefile

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
