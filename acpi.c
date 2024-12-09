#include "acpi.h"
#include <stdint.h>
#include "console.h"

struct FADT_t* FADT = NULL;
struct XSDP_t* XSDP = NULL;
struct DSDT_t* DSDT = NULL;

void acpi_find_FADT(){

}

bool acpi_compare_signature(char* signature1, char* signature2){
	if(signature1[0] != signature2[0])return false;
	if(signature1[1] != signature2[1])return false;
	if(signature1[2] != signature2[2])return false;
	if(signature1[3] != signature2[3])return false;
	return true;
}

void parse_FADT(){

	if(acpi_compare_signature(FADT->header.signature, "FACP")){
			uint32_t fadt_size = FADT->header.length;
			if(fadt_size != sizeof(struct FADT_t)){
				print("FADT size not equal");
				print_uint(FADT->header.length);
				print_uint(sizeof(struct FADT_t));
			}

			struct ACPISystemDescriptorTableHeader* header = (struct ACPISystemDescriptorTableHeader*)(FADT->X_Dsdt);
	
			header = (struct ACPISystemDescriptorTableHeader*)&FADT->X_Dsdt;
		if(acpi_compare_signature(header->signature, "DSDT")){
			print("Work DSDT");
		}else{
			print("DSDT not work");
		}
		if(FADT->X_Dsdt == 0){
			print("DSDT memory zero");
		}
		DSDT = (struct DSDT_t*)FADT->X_Dsdt;
		//print(DSDT->header.signature);

	}
}

void parse_XDSP(){

	struct XSDT_t* XSDT = (struct XSDT_t*)(XSDP->XSDT_address);
	if(acpi_compare_signature(XSDT->header.signature, "XSDT")){
		print("is XSDT table");
	}



	uint32_t number_of_entries_XSDT = (XSDT->header.length - sizeof(struct ACPISystemDescriptorTableHeader))/8;

	for(int i = 0; i < number_of_entries_XSDT; i++){
		 struct ACPISystemDescriptorTableHeader* myheader = (struct ACPISystemDescriptorTableHeader*)XSDT->entries[i];
		 //print(myheader->signature);
		 if(acpi_compare_signature(myheader->signature, "FACP")){
		 	print("Found FADT");
			FADT = (struct FADT_t*)myheader;
		 }
		 if(acpi_compare_signature(myheader->signature, "APIC")){
				print("Fount MADT with size");
				print_uint(myheader->length);
				
		 }
	}

}
