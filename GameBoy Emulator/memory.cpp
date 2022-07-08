#include "memory.h"
#include "errors.h"
#include "globals.h"
#include "sound.h"

#include <fstream>
#include <string>
#include <mutex>

Memory::Memory() {
	

}

void Memory::Init(const char* rom_filename) {
	vram = (uint8_t*)(this->gb_mem + 0x8000);
	wram = (uint8_t*)(this->gb_mem + 0xc000);
	io_map = (IO_map*)(this->gb_mem + 0xff00);
	oam = this->gb_mem + 0xfe00;

	videoMode = 0;

	std::ifstream bootrom_file("bootrom.bin", std::ios::in | std::ios::binary | std::ios::ate);

	if (!bootrom_file.is_open())
		fatal(FATAL_BOOT_ROM_NOT_FOUND, __func__);

	std::streampos size = bootrom_file.tellg();
	if (size != 256)
		fatal(FATAL_INVALID_BOOT_ROM_SIZE, __func__);

	bootrom_file.seekg(0, std::ios::beg);
	bootrom_file.read((char*)this->boot_rom, 256);
	bootrom_file.close();

	
	memset(this->gb_mem, 0, sizeof(this->gb_mem));
	io_map->JOYP = 0xff;
	this->cart = new Cartridge(rom_filename);

}

void Memory::saveCartridgeState() {
	cart_ram_AccessMutex.lock();
	cart->saveState();
	cart_ram_AccessMutex.unlock();
}

uint8_t* Memory::getVram(void) {
	return this->vram;
}

IO_map* Memory::getIOMap(void) {
	return this->io_map;
}

uint8_t* Memory::getOam() {
	return this->oam;
}

uint8_t* Memory::translateAddr(uint16_t addr) {
	if (this->io_map->BRC == 0 && addr < 0x100) {		//intercept accesses to 0x0000 - 0x00ff (boot rom)
		return (uint8_t*)(addr + this->boot_rom);
	}

	return (uint8_t*)(addr + this->gb_mem);
}


//translate the gameboy address into a real memory address and read a byte
uint8_t Memory::read(uint16_t gb_address) {

	//cpu can't access vram during video mode 3
	/*if (gb_address >= 0x8000 && gb_address < 0xa000 && 
		((STAT_struct*)(&io_map->STAT))->lcd_mode > 2) {
		return 0xff;
	}*/

	if (this->io_map->BRC == 0 && gb_address < 0x100) {	//bootstrap rom
		return this->boot_rom[gb_address];
	}

	if ((gb_address >= 0 && gb_address <= 0x7fff) || (gb_address >= 0xa000 && gb_address <= 0xbfff)) {		//cartridge address
		cart_ram_AccessMutex.lock();
		uint8_t byte = this->cart->read(gb_address);
		cart_ram_AccessMutex.unlock();
		return byte;
	}

	return this->gb_mem[gb_address];
}

//translate the gameboy address into a real memory address and write a byte
void Memory::write(uint16_t gb_address, uint8_t value) {

	//cpu can't access vram during video mode 3
	/*if (gb_address >= 0x8000 && gb_address < 0xa000 &&
		((STAT_struct*)(&io_map->STAT))->lcd_mode > 2) {
		return;
	}*/

	if (this->io_map->BRC == 0 && gb_address < 0x100) {	//bootstrap rom
		return;
	}

	if ((gb_address >= 0 && gb_address <= 0x7fff) || (gb_address >= 0xa000 && gb_address <= 0xbfff)) {		//cartridge address
		cart_ram_AccessMutex.lock();
		this->cart->write(gb_address, value);
		cart_ram_AccessMutex.unlock();
		return;
	}

	//writing any value to the divider register resets it to 0
	if (gb_address == 0xff04) value = 0;

	this->gb_mem[gb_address] = value;

	if (gb_address >= 0xff10 && gb_address <= 0xff26) {		//audio registers
		_sound->updateReg(gb_address, value);
		return;
	}

	if (gb_address == 0xff46)
		oam_dma_copy();

}

//copy the memory to oam region instantly
void Memory::oam_dma_copy(void) {

	uint16_t src_addr = gb_mem[0xff46] << 8;
	uint16_t dst_addr = 0xfe00;		//always OAM
	for (int i = 0; i < 160; i++) {
		write(dst_addr++, read(src_addr++));
	}
}