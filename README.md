![pkernel](pkernel.png)

pkernel is a kernel for x86_64

# pboot
For booting you need the bootloader
https://github.com/oscarpavon/pboot
pboot need to be in virtual_machine/disk/EFI/BOOT/BOOTX64.EFI

# Build
You only need GCC for building
```
make
```

# Building method
I use GCC for create a raw binary for loading for the bootloader from memory.

