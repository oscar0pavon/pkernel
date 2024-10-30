#include "acpi.h"

struct FADT_t* FADT = NULL;
struct XSDP_t* XSDP = NULL;

void acpi_find_FADT(){

}

bool acpi_compare_signature(char* signature1, char* signature2){
	if(signature1[0] != signature2[0])return false;
	if(signature1[1] != signature2[1])return false;
	if(signature1[2] != signature2[2])return false;
	if(signature1[3] != signature2[3])return false;
	return true;
}
