#ifndef CARTRIDGE_H
#define CARTRIDGE_H

namespace {
	const char* cardridge_type_info[0x23] = {
		"ROM ONLY",		//0
		"MBC1",		//1
		"MBC1+RAM",		//2
		"MBC1+RAM+BATTERY",		//3
		"INVALID CODE",		//4
		"MBC2",		//5
		"MBC2+BATTERY",		//6
		"INVALID CODE",		//7
		"ROM+RAM",		//8
		"ROM+RAM+BATTERY",		//9
		"INVALID CODE",		//A
		"MMM01",		//B
		"MMM01+RAM",		//C
		"MMM01+RAM+BATTERY",		//D
		"INVALID CODE",		//E
		"MBC3+TIMER+BATTERY",		//F
		"MBC3+TIMER+RAM+BATTERY",		//10
		"MBC3",		//11
		"MBC3+RAM",		//12
		"MBC3+RAM+BATTERY",		//13
		"INVALID CODE",		//14
		"INVALID CODE",		//15
		"INVALID CODE",		//16
		"INVALID CODE",		//17
		"INVALID CODE",		//18
		"MBC5",		//19
		"MBC5+RAM",		//1A
		"MBC5+RAM+BATTERY",		//1B
		"MBC5+RUMBLE",		//1C
		"MBC5+RUMBLE+RAM",		//1D
		"MBC5+RUMBLE+RAM+BATTERY",		//1E
		"INVALID CODE",		//1F
		"MBC6",		//20
		"INVALID CODE",		//21
		"MBC7+SENSOR+RUMBLE+RAM+BATTERY"		//22
	};
	const char* supported_cardridge_types[] = {
		"ROM ONLY",		//0
		"MBC1",		//1
		"MBC1+RAM",		//2
		"MBC1+RAM+BATTERY",		//3
		"ROM+RAM",		//8
		"ROM+RAM+BATTERY",		//9
		"MBC3",		//11
		"MBC3+RAM",		//12
		"MBC3+RAM+BATTERY",		//13
	};
};

struct cartridge_header;

#include <cstdint>
#include <utility>
#include <string>

class Cartridge {
public:
	Cartridge(const char* rom_filename);
	uint8_t read(uint16_t address);
	void write(uint16_t address, uint8_t val);
	void saveState(void);
	
private:
	uint8_t* rom;
	uint8_t* ram;
	static uint8_t bank1_reg;
	static uint8_t bank2_reg;
	static uint8_t ram_bank;
	static bool ram_access;
	static uint8_t mode_reg;
	cartridge_header* header;
	std::string romPath;
	int ramSize;

	void loadState(void);
	void verifyHeader(int fileSize);
	uint32_t (*romTranslateAddr)(uint16_t gb_addr);
	uint32_t(*ramTranslateAddr)(uint16_t gb_addr);
	void(*romWrite)(uint16_t gb_addr, uint8_t val);
	static uint32_t no_mbc_rom_translate_func(uint16_t gb_addr);
	static uint32_t no_mbc_ram_translate_func(uint16_t gb_addr);
	static void no_mbc_rom_write(uint16_t gb_addr, uint8_t val);
	static uint32_t mbc1_rom_translate_func(uint16_t gb_addr);
	static uint32_t mbc1_ram_translate_func(uint16_t gb_addr);
	static void mbc1_rom_write(uint16_t gb_addr, uint8_t val);
	static uint32_t mbc3_rom_translate_func(uint16_t gb_addr);
	static uint32_t mbc3_ram_translate_func(uint16_t gb_addr);
	static void mbc3_rom_write(uint16_t gb_addr, uint8_t val);
};

#endif