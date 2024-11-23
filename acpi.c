#include "acpi.h"
#include "errors.h"
#include "library.h"
#include "io.h"

struct FADT_t* FADT = NULL;
struct XSDP_t* XSDP = NULL;
struct DSDT_t* DSDT = NULL;

int validate_checksum(void* ptr, size_t length) {
    uint8_t sum = 0;
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

int acpi_find_XSDP()
{
	uint16_t ebda_segment = *((uint16_t*)0x040E);
    if (ebda_segment) {
        uint8_t* ebda_addr = (uint8_t*)(uintptr_t)(ebda_segment * 16); 
        for (size_t offset = 0; offset < 1024; offset += 16) { 
            if (compare_memory(ebda_addr + offset, "RSD PTR ", 8) == 0) { 
                struct XSDP_t* xsdp = (struct XSDP_t*)(ebda_addr + offset);
                if (validate_checksum(xsdp, xsdp->revision == 0 ? 20 : xsdp->length)) {
					XSDP = xsdp;
                }
            }
        }
    }

    for (uintptr_t addr = 0xE0000; addr <= 0xFFFFF; addr += 16) {
        if (memcmp((void*)addr, "RSD PTR ", 8) == 0) { 
            struct XSDP_t* xsdp = (struct XSDP_t*)addr;
            if (validate_checksum(xsdp, xsdp->revision == 0 ? 20 : xsdp->length)) {
                	XSDP = xsdp;
            }
        }
    }

	return -EBADXSDP;
}

bool acpi_compare_signature(char* signature1, char* signature2){
	if(signature1[0] != signature2[0])return false;
	if(signature1[1] != signature2[1])return false;
	if(signature1[2] != signature2[2])return false;
	if(signature1[3] != signature2[3])return false;
	return true;
}

// returns the address where it's found
// checks for both DSDT and FACP
int acpi_find_entries()
{
	if(XSDP == NULL)
		if(acpi_find_XSDP() == -EBADXSDP)
			return -EBADXSDP;

	struct XSDT_t* x = XSDP->XSDT_address;
	struct ACPISystemDescriptorTableHeader* acpi = x->header;

	size_t num_entries = (acpi->length - sizeof(struct ACPISystemDescriptorTableHeader)) / sizeof(uint64_t);
	for(size_t i = 0;i < num_entries;i++)
	{
		struct ACPISystemDescriptorTableHeader* entry = (struct ACPISystemDescriptorTableHeader*)(uint64_t)x->entries[i];
		if(entry != NULL && acpi_compare_signature(entry->signature, "DSDT") == true)
		{
			DSDT = entry;
			return 0;
		}

		if(entry != NULL && acpi_compare_signature(entry->signature, "FACP") == true)
		{
			FADT = entry;
			return 0;
		}
	}	
	return -EBADDSDT;
}

int acpi_shutdown()
{
	int sleep_type = 5, sleep_en;

	if(!(sleep_en = FADT->PM1aControlBlock & (1 << 13)))
	{
		return -EACPINOTSUP;
	}

	// TO DO: need to find sleep_type in AML
	outportl(FADT->PM1aControlBlock, sleep_type | sleep_en);
}