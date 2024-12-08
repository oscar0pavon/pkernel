#include "efi.h"
#include "types.h"
#include "gop.h"

#include "../console.h"

#include "acpi.h"

#include <stdint.h>

#include "library.h"


struct SystemTable* system_table;
Handle* efi_handle;

struct LoadedImageProtocol* bootloader_image;

Handle kernel_image_handle;

Handle main_device;
struct FileSystemProtocol* root_file_system;
struct FileProtocol* root_directory;
struct FileProtocol* opened_kernel_file;

EfiGraphicsOutputProtocol* graphics_output_protocol;	

char * kernel_bin;

inline void hang(){
	while(1){};
}

void efi_log(uint16_t* text){
	
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
			efi_log(u"Can't allocate memory for memory map");
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
		efi_log(u"ERROR boot service not closed");
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

void efi_get_loaded_image(){

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

}

void efi_get_root_directory(){

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

}

void load_bin(){

	efi_status_t open_kernel_status = root_directory->open(
			root_directory,
			&opened_kernel_file,
			u"kernel.bin",
			EFI_FILE_MODE_READ,
			EFI_FILE_READ_ONLY
			);	

	if(open_kernel_status != EFI_SUCCESS){
		print("can't open bin file");
	}
	print("bin loaded");

}


void load_kernel(){
	efi_status_t open_kernel_status = root_directory->open(
			root_directory,
			&opened_kernel_file,
			u"pkernel",
			EFI_FILE_MODE_READ,
			EFI_FILE_READ_ONLY
			);	

	if(open_kernel_status != EFI_SUCCESS){
		print("can't open kernel file");
	}
	print("kernel file loaded");

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
		efi_log(u"Can't get Graphics Output Protocol with open_protocol");
	}

	status = system_table->boot_table->handle_protocol(efi_handle, &gop_guid, 
			(void**)&graphics_output_protocol);

	if(status != EFI_SUCCESS){
		efi_log(u"Can't get Graphics Output Protocol with handle_protocol");
	}

	status = system_table->boot_table->locate_protocol(&gop_guid,
			(void*)0, (void**)&graphics_output_protocol);

	if(status != EFI_SUCCESS){
		efi_log(u"Can't get Graphics Output Protocol with locate_protocol");
	}
	

	//get the current mode

	EfiGraphicsOutputModeInformation *info;
	uint64_t size_of_info, number_of_modes, native_mode;
	if(graphics_output_protocol->mode == NULL){
		efi_log(u"Graphics Output Protocol Mode is NULL");
		status = graphics_output_protocol->query_mode(graphics_output_protocol,
				0 , &size_of_info, &info);
	}else{
		status = graphics_output_protocol->query_mode(graphics_output_protocol,
				graphics_output_protocol->mode->mode , &size_of_info, &info);
	}

	if(status != EFI_SUCCESS){
		efi_log(u"Can't query mode");
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
			efi_log(u"Found ACPI 2.0 table");
			XSDP = table->vendor_table;
		}

	}
}


efi_status_t read_fixed(
	struct FileProtocol *file,
	uint64_t offset,
	size_t size,
	void *dst)
{
	efi_status_t status = EFI_SUCCESS;
	unsigned char *buf = dst;
	size_t read = 0;

	status = file->set_position(file, offset);
	if (status != EFI_SUCCESS) {

		return status;
	}

	while (read < size) {
		efi_uint_t remains = size - read;

		status = file->read(file, &remains, (void *)(buf + read));
		if (status != EFI_SUCCESS) {
	
			return status;
		}

		read += remains;
	}

	return status;
}

void execute_kernel(){

	efi_status_t status;

	status = opened_kernel_file->set_position(opened_kernel_file, 0xFFFFFFFFFFFFFFFF)	;
	uint64_t kernel_file_size;
	status = opened_kernel_file->get_position(opened_kernel_file, &kernel_file_size);

	print("Kernel file size");
	print_uint(kernel_file_size);

	uint64_t* kernel_in_memory;

	efi_status pool_status = system_table->boot_table->allocate_pool(EFI_LOADER_DATA,
			kernel_file_size,
			(void**)kernel_in_memory)	;
	
	if(pool_status != EFI_SUCCESS){
		print("ERROR allocating pool");
	}else{
		print("Memory allocated for kernel");
	}

	

	status = read_fixed(opened_kernel_file, 0, 
			kernel_file_size, kernel_in_memory);
	if(status != EFI_SUCCESS){
		print("ERROR loading kernel in memory");
	}
	print("Kernel loaded");



	int (*kernel)(int);


	kernel = kernel_in_memory;

	int kernel_result = 0;
	kernel_result = (*kernel)(5);

	print("Kernel executed");

	print_uint(kernel_result);

	hang();	


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


void pboot(Handle in_efi_handle, SystemTable *in_system_table)
{

	system_table = in_system_table;

	efi_handle = in_efi_handle;
	
	efi_log(u"pboot");

	get_graphics_output_protocol();	
	system_table->out->clear_screen(system_table->out);	

	//now we can use print() for print to the frame buffer
	clear();

	//configure efi for load files from root file system	
	efi_get_loaded_image();
	efi_get_root_directory();
	//now we can load files



	load_kernel();
	execute_kernel();

	get_acpi_table();


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

	


	//we never come here	

	while(1){

	}

}


