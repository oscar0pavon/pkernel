#include "efi.h"
#include "types.h"
#include "gop.h"

#include "console.h"

#include "acpi.h"

#include <stdint.h>

#include "elf.h"

#include "library.h"

#include "drivers/ps2_keyboard.h"

struct SystemTable* system_table;
Handle* efi_handle;

struct LoadedImageProtocol* bootloader_image;

Handle kernel_image_handle;

Handle main_device;
struct FileSystemProtocol* root_file_system;
struct FileProtocol* root_directory;
struct FileProtocol* opened_kernel_file;

EfiGraphicsOutputProtocol* graphics_output_protocol;	

struct ElfHeader kernel_elf_header;	
struct ElfProgramHeader* kernel_program_headers;
char * kernel_bin;

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

void load_elf(){

	efi_status_t open_kernel_status = root_directory->open(
			root_directory,
			&opened_kernel_file,
			u"fasm.elf",
			EFI_FILE_MODE_READ,
			EFI_FILE_READ_ONLY
			);	

	if(open_kernel_status != EFI_SUCCESS){
		print("can't open elf file");
	}
	print("elf file loaded");
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

struct ReserveMemory {
	const char *name;
	uint64_t begin;
	uint64_t end;
};

size_t reserves_count;
size_t reserve_capacity;
struct ReserveMemory* main_reserve;

static efi_status reserve_memory(uint64_t begin, uint64_t end){
	if(reserves_count == reserve_capacity){

	size_t new_size = 2 * reserves_count;
	struct ReserveMemory* new_reserve = 0;
	struct ReserveMemory* old_reserve = main_reserve;


	if(new_size == 0)	
		new_size = 16;

	system_table->boot_table->allocate_pool(
			EFI_LOADER_DATA, 
			new_size * sizeof(struct ReserveMemory), 
			(void **)&new_reserve
			);

	copy_memory(new_reserve, old_reserve,
			reserves_count * sizeof(struct ReserveMemory));

	main_reserve = new_reserve;
	reserve_capacity = new_size;
	
	if(old_reserve != 0){
		system_table->boot_table->free_pool((void*)old_reserve);
	}
	}	
	
	set_memory(&main_reserve[reserves_count],0,sizeof(struct ReserveMemory));
	main_reserve[reserves_count].name = "Kernel";
	main_reserve[reserves_count].begin = begin;
	main_reserve[reserves_count].end = end;
	reserves_count++;

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

static void get_image_size(
	struct ElfHeader* kernel_header,
	struct ElfProgramHeader* program_headers,
	uint64_t alignment,
	uint64_t *out_begin,
	uint64_t *out_end)
{
	*out_begin = UINT64_MAX;
	*out_end = 0;

	for (size_t i = 0; i < kernel_header->program_header_number_of_entries; ++i) {
		struct ElfProgramHeader *phdr = &program_headers[i];
		uint64_t phdr_begin, phdr_end;
		uint64_t align = alignment;

		if (phdr->p_type != PT_LOAD)
			continue;

		if (phdr->p_align > align)
			align = phdr->p_align;

		phdr_begin = phdr->p_vaddr;
		phdr_begin &= ~(align - 1);
		if (*out_begin > phdr_begin)
			*out_begin = phdr_begin;

		phdr_end = phdr->p_vaddr + phdr->p_memsz + align - 1;
		phdr_end &= ~(align - 1);
		if (*out_end < phdr_end)
			*out_end = phdr_end;
	}
}

void execute_elf(){

	efi_status_t status;
	read_fixed(opened_kernel_file, 0, 
			sizeof(struct ElfHeader), &kernel_elf_header);


	efi_status pool_status = system_table->boot_table->allocate_pool(EFI_LOADER_DATA,
			kernel_elf_header.program_header_number_of_entries * 
			kernel_elf_header.program_header_entry_size,
			(void**)kernel_program_headers)	;

	if(pool_status != EFI_SUCCESS){
		print("ERROR allocating pool");
	}

	read_fixed(opened_kernel_file,
			kernel_elf_header.program_header_offset, 
			kernel_elf_header.program_header_number_of_entries *
			kernel_elf_header.program_header_entry_size,
			(void*)&kernel_program_headers);

	print("Readed ELF headers");

	uint64_t page_size = 4096;
	uint64_t image_begin;
	uint64_t image_end;
	uint64_t image_size;
	uint64_t image_address;

	
	get_image_size(&kernel_elf_header,
			kernel_program_headers, 
			page_size, &image_begin, &image_end);

	image_size = image_end - image_begin;

	efi_status alloc = system_table->boot_table->allocate_pages(EFI_ALLOCATE_ANY_PAGES,
			EFI_LOADER_DATA, image_size / page_size,
			&image_address);
	if(alloc != EFI_SUCCESS){
		print("ERROR allocating any pages");
	}

	print("Allocated memory for ELF");

	//uint8_t my_stack_memory[image_size/page_size];
	//set_memory(my_stack_memory,0,image_size/page_size);

	for (size_t i = 0; i < kernel_elf_header.program_header_number_of_entries; ++i) {
		struct ElfProgramHeader *program_header = &kernel_program_headers[i];

		uint64_t program_header_address;

		if (program_header->p_type != PT_LOAD)
			continue;

		program_header_address = image_address + program_header->p_vaddr - image_begin;
		status = read_fixed(
			opened_kernel_file,
			program_header->p_offset,
			program_header->p_filesz,
			(void *)program_header_address);
		if (status != EFI_SUCCESS) {
			print("Failed to read elf segment in memory");
		}
	

		reserve_memory(program_header_address,
				program_header_address + program_header->p_memsz);


	}

	print("ELF loaded in memory");

	uint64_t kernel_image_entry = image_address + kernel_elf_header.e_entry - image_begin;	

	//void (ELFABI *entry)(struct ReserveMemory*, size_t);

	int (ELFABI*entry)();

	exit_boot_services();
	
	//entry = (void (ELFABI *)(struct ReserveMemory*, size_t))kernel_image_entry;

	entry = (int(ELFABI*)())kernel_image_entry;

	int elf_result = 0;
	elf_result = (*entry)();
	print("Kernel executed");
	print_uint(elf_result);

	while(1){

	}



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

void input_loop(){

	load_bin();


	char kernel_txt[3];
	set_memory(kernel_txt, 0, 3);
	read_fixed(opened_kernel_file,0,3,(void*)&kernel_txt);
	print("Readed kernel bin");

	print(kernel_txt);
	byte(*kernel_main)(); 
	kernel_main = kernel_txt;

	print("calling kernel.bin");
	
	print_byte_hex(10);
	print_byte_hex(PS2_KEYBOARD_D_RELEASED);

	exit_boot_services();

	print_in_line_buffer_number(12,"d");
	print_in_line_buffer_number(12,"d");
	print_in_line_buffer_number(12,"d");
	print_in_line_buffer_number(12,"d");
	print_in_line_buffer_number(12,"d");

	while(1){

		char restul = 'a';
		char buff[2];
		buff[0] = '\0';
		buff[1] = '\0';


		char input = ps2_keyboard_get_input(kernel_main);
		buff[0] = input;
		if(input != '\0'){
			print_in_line_buffer_number(12,buff);
		}


		print_in_line_number(9, buff);
	}

	print("executed successfully");

}

efi_status efi_main(Handle in_efi_handle, struct SystemTable *in_system_table)
{

	system_table = in_system_table;

	efi_handle = in_efi_handle;
	
	efi_log(u"Pavon Kernel");

	get_graphics_output_protocol();	
	system_table->out->clear_screen(system_table->out);	

	//now we can use print() for print to the frame buffer
	clear();

	//configure efi for load files from root file system	
	efi_get_loaded_image();
	efi_get_root_directory();
	//now we can load files


	load_elf();
	execute_elf();

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

	


	

	while(1){

	}

	return 0;
}


