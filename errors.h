#ifndef __ERRORS_H__
#define __ERRORS_H__

#define ENULLPTR    0xFF // Null pointer
#define EINVAL      0x10 // Invalid argument or operation
#define EUNKNOWN    0x01 // Unknown error

// Memory errors
#define EPAGEFAULT  0x30 // Page Fault
#define ENOMEM      0x31 // Out of memory
#define EALIGN      0x32 // Memory alignment error

// Device errors
#define EGENDEV     0x40 // General Device Error
#define ENODEV      0x41 // No such device
#define EIO         0x42 // I/O error
#define EDEVBUSY    0x43 // Device busy

// ACPI/BIOS errors
#define EBADXSDP    0x20 // Invalid XSDP or ACPI table
#define EBADRSDP    0x21 // RSDP structure not found
#define EACPINOTSUP 0x22 // ACPI feature not supported
#define EBADDSDT    0x23 // No DSDT

// Filesystem errors
#define ENOENT      0x50 // No such file or directory
#define EROFS       0x51 // Read-only file system
#define EFSFULL     0x52 // File system full

// Network errors
#define ENETDOWN    0x60 // Network is down
#define ETIMEOUT    0x61 // Connection timed out
#define ECONNREFUSED 0x62 // Connection refused

// Process errors
#define ECHILD      0x70 // No child process
#define EPERM       0x71 // Operation not permitted
#define ESRCH       0x72 // No such process

#endif