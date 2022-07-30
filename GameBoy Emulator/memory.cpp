#include "memory.h"
#include "errors.h"
#include "globals.h"
#include "sound.h"
#include "ppu.h"

#include <fstream>
#include <string>
#include <mutex>

bool _GBC_Mode;

Memory::Memory() {
	

}

void Memory::Init(const char* rom_filename) {

	this->cart = new Cartridge(rom_filename);

	vram = (uint8_t**)(calloc(2, sizeof(uint8_t*)));
	vram[0] = (uint8_t*)(calloc(0x2000, sizeof(uint8_t)));
	vram[1] = (uint8_t*)(calloc(0x2000, sizeof(uint8_t)));

	//wram banks
	for (int i = 0; i < 7; i++) {
		wram_banks[i] = (uint8_t*)calloc(0x1000, 1);
	}

	wram = (uint8_t*)(this->gb_mem + 0xc000);
	io_map = (IO_map*)(this->gb_mem + 0xff00);
	oam = this->gb_mem + 0xfe00;

	videoMode = 0;
	load_bootrom();
	
	memset(this->gb_mem, 0, sizeof(this->gb_mem));
	io_map->JOYP = 0xff;

}

bool Memory::load_bootrom() {

	std::ifstream bootrom_file;
	
	boot_rom0 = (uint8_t*)malloc(256);

	if (!_GBC_Mode) {

		bootrom_file.open("bootrom.bin", std::ios::in | std::ios::binary | std::ios::ate);
		if (!bootrom_file.is_open()) 
			fatal(FATAL_BOOT_ROM_NOT_FOUND, __func__);

		std::streampos size = bootrom_file.tellg();
		if (size != 256) fatal(FATAL_INVALID_BOOT_ROM_SIZE, __func__);

		bootrom_file.seekg(0, std::ios::beg);
		bootrom_file.read((char*)boot_rom0, 256);
		bootrom_file.close();
	}
	else {

		bootrom_file.open("gbc_bootrom.bin", std::ios::in | std::ios::binary | std::ios::ate);
		if (!bootrom_file.is_open())
			fatal(FATAL_BOOT_ROM_NOT_FOUND, __func__);

		boot_rom1 = (uint8_t*)malloc(1792);

		std::streampos size = bootrom_file.tellg();
		//2304 bootrom have 256 empty bytes between the two sections
		if (size == 2304) {
			bootrom_file.seekg(0, std::ios::beg);
			bootrom_file.read((char*)boot_rom0, 256);
			bootrom_file.seekg(512, std::ios::beg);
			bootrom_file.read((char*)boot_rom1, 1792);
			bootrom_file.close();
		}
		else if (size == 2048) {		//no empty bytes between sections
			bootrom_file.seekg(0, std::ios::beg);
			bootrom_file.read((char*)boot_rom0, 256);
			bootrom_file.read((char*)boot_rom1, 1792);
			bootrom_file.close();
		}
		else fatal(FATAL_INVALID_BOOT_ROM_SIZE, __func__);
	}

	return true;
}

void Memory::saveCartridgeState() {
	cart_ram_AccessMutex.lock();
	cart->saveState();
	cart_ram_AccessMutex.unlock();
}

uint8_t* Memory::getVram(void) {
	if (_GBC_Mode) return this->vram[io_map->VBK & 0x1];
	return this->vram[0];
}

uint8_t* Memory::getVramBank0() {
	return this->vram[0];
}

uint8_t* Memory::getVramBank1() {
	return this->vram[1];
}

IO_map* Memory::getIOMap(void) {
	return this->io_map;
}

uint8_t* Memory::getOam() {
	return this->oam;
}


//translate the gameboy address into a real memory address and read a byte
uint8_t Memory::read(uint16_t gb_address) {

	//cpu can't access vram during video mode 3
	/*if (gb_address >= 0x8000 && gb_address < 0xa000 && 
		((STAT_struct*)(&io_map->STAT))->lcd_mode > 2) {
		return 0xff;
	}*/

	if (this->io_map->BRC == 0 && gb_address < 0x100) {	//first section bootstrap rom
		return this->boot_rom0[gb_address];
	}

	if (_GBC_Mode) {
		//bank 1-7 of wram
		if (gb_address >= 0xd000 && gb_address <= 0xdfff) {
			return wram_banks[(io_map->SVBK == 0 ? 1 : io_map->SVBK & 0x7) - 1][gb_address - 0xd000];
		}

		if (this->io_map->BRC == 0 && gb_address >= 0x200 && gb_address<= 0x8ff) {	//second section bootstrap rom
			return this->boot_rom1[gb_address - 0x200];
		}

		if (gb_address == 0xff69) {		//read a byte from the bg palette memory
			return bg_palette_mem[io_map->PLT.bg_palette_index];
		}
		if (gb_address == 0xff6b) {		//read a byte from the sprite palette memory
			return sprite_palette_mem[io_map->PLT.sprite_palette_index];
		}
	}

	if (gb_address >= 0x8000 && gb_address <= 0x9fff) {		//vram
		if (_GBC_Mode) return vram[io_map->VBK & 0x1][gb_address & 0x7fff];
		return vram[0][gb_address & 0x7fff];
	}

	if ((gb_address >= 0 && gb_address <= 0x7fff) || (gb_address >= 0xa000 && gb_address <= 0xbfff)) {		//cartridge address
		cart_ram_AccessMutex.lock();
		uint8_t byte = this->cart->read(gb_address);
		cart_ram_AccessMutex.unlock();
		return byte;
	}

	return this->gb_mem[gb_address];
}

SDL_Color Memory::getBackgroundColor(int palette, int num) {
	
	color_palette *gb_c = (color_palette*)&bg_palette_mem[(palette * 4 + num) * 2];
	SDL_Color c = { gb_c->red * 8.2, gb_c->green * 8.2, gb_c->blue * 8.2, 255 };
	return c;
}

const color_palette const* Memory::getBackgroundPalette() {
	return (color_palette*)bg_palette_mem;
}

SDL_Color Memory::getSpriteColor(int palette, int num) {
	color_palette* gb_c = (color_palette*)&sprite_palette_mem[(palette * 4 + num) * 2];
	SDL_Color c = { gb_c->red * 8.2, gb_c->green * 8.2, gb_c->blue * 8.2, 255 };
	return c;
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

	if (gb_address >= 0x8000 && gb_address <= 0x9fff) {		//vram
		if (_GBC_Mode) {
			vram[io_map->VBK & 0x1][gb_address & 0x7fff] = value;
			return;
		}
		vram[0][gb_address & 0x7fff] = value;
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
	if (gb_address == 0xff4d) {	//double speed register
		this->gb_mem[gb_address] = (this->gb_mem[gb_address] & 0xfe) | (value & 0x1);	//only bit 0 is writable
		return;
	}

	if (_GBC_Mode) {

		if (gb_address >= 0xd000 && gb_address <= 0xdfff) {
			wram_banks[(io_map->SVBK == 0 ? 1 : io_map->SVBK & 0x7) - 1][gb_address - 0xd000] = value;
		}

		if (gb_address == 0xff69) {		//write a byte to the bg palette memory
			bg_palette_mem[io_map->PLT.bg_palette_index] = value;
			io_map->PLT.bg_palette_index += io_map->PLT.bg_inc;
			return;
		}
		if (gb_address == 0xff6b) {		//write a byte to the sprite palette memory
			sprite_palette_mem[io_map->PLT.sprite_palette_index] = value;
			io_map->PLT.sprite_palette_index += io_map->PLT.sprite_inc;
			return;
		}
	}

	if (gb_address >= 0xff10 && gb_address <= 0xff26) {		//audio registers
		if (gb_address == 0xff26)
			this->gb_mem[gb_address] = (this->gb_mem[gb_address] & 0x0f) | (value & 0xf0);
		else this->gb_mem[gb_address] = value;

		_sound->updateReg(gb_address, value);
		return;
	}

	this->gb_mem[gb_address] = value;

	if (_GBC_Mode) {
		if (gb_address == 0xff55 && (io_map->LCDC & 0x80)) {		//hdma
			if (io_map->HDMA.transfer_mode == 0 && hdma_active == 1) {		//pause hdma
				hdma_active = 0;
				io_map->HDMA.transfer_mode = 1;
			}
			else
				activate_hdma();
		}
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

void Memory::activate_hdma(void) {
	
	//H-BLANK tranfer mode
	if (io_map->HDMA.transfer_mode) {
		hdma_active = 1;
		io_map->HDMA.transfer_mode = 0;	//the transfer is active
		return;
	}

	//General purpose DMA
	uint16_t src_addr = ((io_map->HDMA.HDMA2) | (io_map->HDMA.HDMA1 << 8)) & 0xfff0;
	uint16_t dst_addr = 0x8000 | ((((io_map->HDMA.HDMA4) | (io_map->HDMA.HDMA3 << 8)) & 0x1ff0));

	//copy junk in this range
	if (src_addr >= 0x8000 && src_addr <= 0x9fff) {
		for (int i = 0; i < (io_map->HDMA.transf_length + 1) * 16; i++) {
			write(dst_addr + i, 0);
		}
	}

	//copy data
	for (int i = 0; i < (io_map->HDMA.transf_length + 1) * 16; i++) {
		write(dst_addr + i, read(src_addr + i));
	}
	io_map->HDMA.transf_length = 0x7f;
	io_map->HDMA.transfer_mode = 1;	//transfer finished
}

void Memory::transfer_hdma() {
	if (!hdma_active)
		return;

	uint16_t src_addr = ((io_map->HDMA.HDMA2) | (io_map->HDMA.HDMA1 << 8)) & 0xfff0;
	uint16_t dst_addr = 0x8000 | ((((io_map->HDMA.HDMA4) | (io_map->HDMA.HDMA3 << 8)) & 0x1ff0));

	//copy data
	for (int i = 0; i < 16; i++) {
		write(dst_addr++, read(src_addr++));
	}

	//update hdma registers
	io_map->HDMA.HDMA2 = src_addr & 0xf0;
	io_map->HDMA.HDMA1 = (src_addr & 0xff00) >> 8;

	io_map->HDMA.HDMA4 = (dst_addr & 0xf0);
	io_map->HDMA.HDMA3 = (dst_addr & 0x1f00) >> 8;

	io_map->HDMA.transf_length--;
	if (io_map->HDMA.transf_length == 0x7f) {
		hdma_active = 0;
		io_map->HDMA.transfer_mode = 1;	//transfer finished
	}
}