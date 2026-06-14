CC := cc
ASSEMBLER := ./tools/fasm

CFLAGS := -ffreestanding -fno-stack-check -fno-stack-protector
CFLAGS += -mno-red-zone -maccumulate-outgoing-args
CFLAGS += -fno-pic -mcmodel=kernel -fno-pie -fshort-wchar

