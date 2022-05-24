#ifndef MEMORY_H
#define MEMORY_H

#include "structures.h"
#include "cartridge.h"

#include <cstdint>
#include <mutex>

class Memory {
public:
	Memory();
	void Init(const char* rom_filename);
	//Translate virtual gameboy addresses to actual memory addresses for the emulator and read or write the data.
	uint8_t read(uint16_t gb_address);
	void write(uint16_t gb_address, uint8_t value);
	void oam_dma_copy(void);
	uint8_t* translateAddr(uint16_t addr);
	uint8_t* getVram();
	IO_map* getIOMap();
	uint8_t* getOam();
	void saveCartridgeState();
private:
	uint8_t boot_rom[0x100];		//256 bytes
	uint8_t gb_mem[0x10000];	//memory mapped by 16 bit register (65536 bytes)
	uint8_t* vram;		//0x8000 - 0x9fff
	uint8_t* ext_ram;		//external bus for ram
	uint8_t* wram;	//work ram (0xc000 - 0xdfff)
	IO_map* io_map;		// input output memory map (0xff00 - 0xff7f)
	uint8_t* oam;		//(object attribute table) sprite information table (0xfe00 - 0xfe9f)
	Cartridge* cart;
	uint8_t videoMode;

	std::mutex cart_ram_AccessMutex;
};

#endif
