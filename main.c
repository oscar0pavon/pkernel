#include "efi.h"
#include "types.h"
#include "gop.h"

#include "console.h"

#include "acpi.h"

#include <stdint.h>


struct SystemTable* system_table;
Handle* efi_handle;

struct LoadedImageProtocol* bootloader_image;

Handle kernel_image_handle;

Handle main_device;
struct FileSystemProtocol* root_file_system;
struct FileProtocol* root_directory;
struct FileProtocol* opened_kernel_file;

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


void load_elf(){

	struct GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	
	Status loaded_image_status = system_table->boot_table->open_protocol(efi_handle,
			&loaded_image_guid,
			(void **)&bootloader_image,
			efi_handle,
			0,
			EFI_OPEN_PROTOCOL_GET_PROTOCOL)	;

	if(loaded_image_status == EFI_SUCCESS){
		print("got bootloader image");
	}


	main_device = bootloader_image->device;

	struct GUID file_system_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

	system_table->boot_table->open_protocol(main_device,
			&file_system_guid,
			(void**)&root_file_system,
			efi_handle,
			0,
			EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL)	;

	efi_status_t open_volumen_status = 
		root_file_system->open_volume(root_file_system, &root_directory);

	if(open_volumen_status != EFI_SUCCESS){
		print("open volume error");
	}

	efi_status_t open_kernel_status = root_directory->open(
			root_directory,
			&opened_kernel_file,
			u"kernel.elf",
			EFI_FILE_MODE_READ,
			EFI_FILE_READ_ONLY
			);	

	if(open_kernel_status != EFI_SUCCESS){
		print("can't open kernel file");
	}

}

void get_graphics_output_protocol(){

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

}

void get_acpi_table(){

	//get ACPI 2.0 table

	EFI_GUID acpi_guid = EFI_ACPI_20_TABLE_GUID;

	for(int i = 0; i < system_table->number_of_table_entries; i++){
		EfiConfigurationTable* table = &system_table->configuration_tables[i];
		
		if(compare_efi_guid(&table->vendor_guid,&acpi_guid)){
			log(u"Found ACPI 2.0 table");
			XSDP = table->vendor_table;
		}

	}
}

Status efi_main(
	Handle in_efi_handle, struct SystemTable *in_system_table)
{

	system_table = in_system_table;

	efi_handle = in_efi_handle;
	

	log(u"Pavon Kernel");


	get_graphics_output_protocol();	
	system_table->out->clear_screen(system_table->out);	

	//now we can use print()
	clear();


	print("Loading kernel elf");
	load_elf();
	print("kernel loaded");

	while(1){

	}	

	get_acpi_table();

	exit_boot_services();

//#########################################################
//#########################################################
//################ Full control ###########################
//#########################################################



	print("Horizontal Resolution:");
	print_uint(console_horizonal);
	print("Vertical Resolution:");
	print_uint(console_vertical);


	struct XSDT_t* XSDT = (struct XSDT_t*)(XSDP->XSDT_address);
	if(acpi_compare_signature(XSDT->header.signature, "XSDT")){
		print("is XSDT table");
	}



	uint32_t number_of_entries_XSDT = (XSDT->header.length - sizeof(struct ACPISystemDescriptorTableHeader))/8;

	for(int i = 0; i < number_of_entries_XSDT; i++){
		 struct ACPISystemDescriptorTableHeader* myheader = (struct ACPISystemDescriptorTableHeader*)XSDT->entries[i];
		 print(myheader->signature);
		 if(acpi_compare_signature(myheader->signature, "FACP")){
		 	print("Found FADT");
		 }
	}

	while(1){

	}

	return 0;
}


