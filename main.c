#include "efi.h"
#include "types.h"
#include "gop.h"

#include "console.h"

#include "acpi.h"

#include <stdint.h>

struct XSDP_t* XSDP;

struct SystemTable* system_table;
Handle* efi_handle;

struct LoadedImageProtocol* bootloader_image;

Handle kernel_image_handle;

Handle main_device;

EfiGraphicsOutputProtocol* graphics_output_protocol;	

void log(uint16_t* text){
	
	system_table->out->output_string(system_table->out,text);
	system_table->out->output_string(system_table->out,u"\n\r");

}

static void exit_boot_services(){

	struct MemoryDescriptor *mmap;
	efi_uint_t mmap_size = 4096;
	efi_uint_t mmap_key;
	efi_uint_t desc_size;
	uint32_t desc_version;
	efi_status_t status;

	while (1) {
		status = system_table->boot_table->allocate_pool(
			EFI_LOADER_DATA,
			mmap_size,
			(void **)&mmap);
		if(status != EFI_SUCCESS){
			log(u"Can't allocate memory for memory map");
		}

		status = system_table->boot_table->get_memory_map(
			&mmap_size,
			mmap,
			&mmap_key,
			&desc_size,
			&desc_version);
		if (status == EFI_SUCCESS){
			break;
		}


	}

	status = system_table->boot_table->exit_boot_services(efi_handle, 
			mmap_key);

	if(status != EFI_SUCCESS){
		log(u"ERROR boot service not closed");
		return;
	}

}


bool compare_efi_guid(EFI_GUID* guid1, EFI_GUID* guid2){

	if(guid1->data1 != guid2->data1){
		return false;
	}
	if(guid1->data2 != guid2->data2){
		return false;
	}
	if(guid1->data3 != guid2->data3){
		return false;
	}

	for(int i = 0; i<8;i++){
		if(guid1->data4[i] != guid2->data4[i]){
			return false;
		}
	}

	return true;
}

Status efi_main(
	Handle in_efi_handle, struct SystemTable *in_system_table)
{

	system_table = in_system_table;

	efi_handle = in_efi_handle;
	

	log(u"Pavon Kernel");


	struct GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	
	Status loaded_image_status = system_table->boot_table->open_protocol(efi_handle,
			&loaded_image_guid,
			(void **)&bootloader_image,
			efi_handle,
			0,
			EFI_OPEN_PROTOCOL_GET_PROTOCOL)	;

	if(loaded_image_status == EFI_SUCCESS){
		log(u"got loaded image");
	}
	
	struct GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	

	Status status = system_table->boot_table->open_protocol(efi_handle,
			&gop_guid,
			(void **)&graphics_output_protocol,
			efi_handle,
			0,
			EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL)	;

	if(status != EFI_SUCCESS){
		log(u"Can't get Graphics Output Protocol with open_protocol");
	}

	status = system_table->boot_table->handle_protocol(efi_handle, &gop_guid, 
			(void**)&graphics_output_protocol);

	if(status != EFI_SUCCESS){
		log(u"Can't get Graphics Output Protocol with handle_protocol");
	}

	status = system_table->boot_table->locate_protocol(&gop_guid,
			(void*)0, (void**)&graphics_output_protocol);

	if(status != EFI_SUCCESS){
		log(u"Can't get Graphics Output Protocol with locate_protocol");
	}
	

	//get the current mode

	EfiGraphicsOutputModeInformation *info;
	uint64_t size_of_info, number_of_modes, native_mode;
	if(graphics_output_protocol->mode == NULL){
		log(u"Graphics Output Protocol Mode is NULL");
		status = graphics_output_protocol->query_mode(graphics_output_protocol,
				0 , &size_of_info, &info);
	}else{
		status = graphics_output_protocol->query_mode(graphics_output_protocol,
				graphics_output_protocol->mode->mode , &size_of_info, &info);
	}

	if(status != EFI_SUCCESS){
		log(u"Can't query mode");
	}
	
	native_mode = graphics_output_protocol->mode->mode;
	number_of_modes = graphics_output_protocol->mode->max_mode;

	console_horizonal = graphics_output_protocol->mode->mode_info->horizontal_resolution;
	console_vertical = graphics_output_protocol->mode->mode_info->vertical_resolution;


	//get ACPI 2.0 table

	EFI_GUID acpi_guid = EFI_ACPI_20_TABLE_GUID;

	for(int i = 0; i < system_table->number_of_table_entries; i++){
		EfiConfigurationTable* table = &system_table->configuration_tables[i];
		
		if(compare_efi_guid(&table->vendor_guid,&acpi_guid)){
			log(u"Found ACPI 2.0 table");
			XSDP = table->vendor_table;
		}

	}

	log(u"Exiting....");
	
	system_table->out->clear_screen(system_table->out);	
	exit_boot_services();

//#########################################################
//#########################################################
//################ Full control ###########################
//#########################################################


	clear();

	print("Horizontal Resolution:");
	print_uint(console_horizonal);
	print("Vertical Resolution:");
	print_uint(console_vertical);


	//print(XSDP->signature);

	while(1){

	}

	return 0;
}


