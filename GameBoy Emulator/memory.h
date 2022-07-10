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
	uint8_t* getVram();
	uint8_t* getVramBank0();
	uint8_t* getVramBank1();
	IO_map* getIOMap();
	uint8_t* getOam();
	void saveCartridgeState();
	SDL_Color getBackgroundColor(int palette, int num);
	SDL_Color getSpriteColor(int palette, int num);
private:
	bool load_bootrom();

	uint8_t *boot_rom0;		//256 bytes. 0x0-0x100
	uint8_t* boot_rom1;		//1792 bytes. 0x200-0x8ff. GBC only
	uint8_t gb_mem[0x10000];	//memory mapped by 16 bit register (65536 bytes)
	uint8_t bg_palette_mem[64];	//8 background palettes with 4 colors per palette
	uint8_t sprite_palette_mem[64];	//8 sprites palettes with 4 colors per palette
	uint8_t *wram_banks[7];		//wram banks, only in CGB mode

	//In gbc mode all 2 banks of 0x2000 bytes of vram are used, 
	//in dmg mode only the first bank. Mapped at: 0x8000 - 0x9fff
	uint8_t **vram;

	uint8_t* ext_ram;		//external bus for ram
	uint8_t* wram;	//work ram (0xc000 - 0xdfff)
	IO_map* io_map;		// input output memory map (0xff00 - 0xff7f)
	uint8_t* oam;		//(object attribute table) sprite information table (0xfe00 - 0xfe9f)
	Cartridge* cart;
	uint8_t videoMode;

	std::mutex cart_ram_AccessMutex;
};

#endif
