![pkernel](pkernel.png)

pkernel is a kernel for x86_64

- Print into the framebuffer
- Get PCI list
- xHCI driver for using USB keyboars

### TODO:
    Create a basic shell

# Configure

    ./configure

For using pboot, this is necesary for booting in the virtual machine

# Build
This only work on GNU/Linux  
You only need GCC for building. I included the Flat Assembler in ./tools directory

    make -j4

# Run and test
You can test with QEMU, there is a script called "run"

    ./run

You can test in real hardware too. Firstly you need to install pboot and pkerel in /boot 
Or your EFI partition /boot/efi

# Building method
I use GCC for create a raw binary and then link everything together with the (.ld) script 
Then i load from EFI memory. 

# Programming
The bootloader manage the framebuffer getting info, so it's passed like an argument to the kernel binary.  
I try to get the C code most simple possible, "less code better"


