#include "cartridge.h"
#include "errors.h"
#include "structures.h"
#include "renderer.h"
#include "globals.h"

#include <iostream>
#include <fstream>
#include <cstdint>
#include <string.h>
#include <string>

uint8_t Cartridge::bank1_reg = 0;
uint8_t Cartridge::bank2_reg = 1;
uint8_t Cartridge::ram_bank = 0;
bool Cartridge::ram_access = false;
uint8_t Cartridge::mode_reg = 0;

Cartridge::Cartridge(const char* rom_filename){

	std::ifstream file(rom_filename, std::ios::in | std::ios::binary | std::ios::ate);

	if (!file.is_open())
		fatal(FATAL_ROM_NOT_FOUND, __func__);

	std::streampos size;
	size = file.tellg();
	file.seekg(0, std::ios::beg);

	romPath = rom_filename;

	this->rom = (uint8_t*)calloc(size, 1);
	file.read((char*)this->rom, size);
	file.close();

	header = (cartridge_header*)&this->rom[0x100];
	verifyHeader(size);

	if (header->cartridgeType == 0 ||		//no mbc chip
		header->cartridgeType == 8 ||
		header->cartridgeType == 9) {
		this->romWrite = this->no_mbc_rom_write;
		this->romTranslateAddr = this->no_mbc_rom_translate_func;
		this->ramTranslateAddr = this->no_mbc_ram_translate_func;
	}
	else if(header->cartridgeType == 1 ||		//mbc1 chip
		header->cartridgeType == 2 ||
		header->cartridgeType == 3){

		Cartridge::bank1_reg = 1;
		Cartridge::bank2_reg = 0;

		this->romWrite = this->mbc1_rom_write;
		this->romTranslateAddr = this->mbc1_rom_translate_func;
		this->ramTranslateAddr = this->mbc1_ram_translate_func;
	}
	else if (header->cartridgeType == 0x11 ||		//mbc3 chip (no RTC)
		header->cartridgeType == 0x12 ||
		header->cartridgeType == 0x13) {
		Cartridge::bank1_reg = 0;
		Cartridge::bank2_reg = 1;

		this->romWrite = this->mbc3_rom_write;
		this->romTranslateAddr = this->mbc3_rom_translate_func;
		this->ramTranslateAddr = this->mbc3_ram_translate_func;
	}

	if (header->ramSize != 0) {
		ramSize = 0;
		if (header->ramSize == 1) {
			ramSize = 2048;
		}else if (header->ramSize == 2) {
			ramSize = 8192;
		}
		else if (header->ramSize == 3) {
			ramSize = 4 * 8192;
		}
		else if (header->ramSize == 4) {
			ramSize = 16 * 8192;
		}
		else if (header->ramSize == 5) {
			ramSize = 8 * 8192;
		}
		else {
			fatal(FATAL_INVALID_RAM_SIZE, __func__);
		}
#ifdef _DEBUG
		std::cout << "Ram size: " << ramSize << std::endl;
#endif
		this->ram = (uint8_t*)calloc(ramSize, 1);
		loadState();		//search for a save file and load it into memory
	}
	else {
		this->ram = nullptr;
	}
}


void Cartridge::saveState(void) {
	
	if (header->ramSize == 0 || this->ram == nullptr || ramSize <= 0) {
		_renderer->showMessage("This game doesn't support saving.", 2);
		return;
	}

	size_t index = this->romPath.find_last_of('.');
	std::string savePath;
	if (index != std::string::npos) {
		savePath = romPath.substr(0, index) + ".sv";
	}
	else {
		savePath = romPath + ".sv";
	}
	std::ofstream file(savePath, std::ios::out | std::ios::binary);
	if (!file) {
		std::cout << "Warning: unable to open .sv file for writing" << std::endl;
		_renderer->showMessage("Unable to save the game.", 2);
		return;
	}
	file.write((char*)ram, ramSize);
	file.close();

#ifdef _DEBUG
	std::cout << "Info: Cartridge ram saved successfully." << std::endl;
#endif
	_renderer->showMessage("Game saved!", 2);
}

void Cartridge::loadState(void) {

	if (header->ramSize == 0 || ram == nullptr) {
		return;
	}

	size_t index = this->romPath.find_last_of('.');
	std::string savePath;
	if (index != std::string::npos) {
		savePath = romPath.substr(0, index) + ".sv";
	}
	else {
		savePath = romPath + ".sv";
	}
	std::ifstream file(savePath, std::ios::in | std::ios::binary | std::ios::ate);
	if (!file) {
		std::cout << "Warning: unable to open .sv file" << std::endl;
		return;
	}
	size_t size = file.tellg();
	if (size != ramSize) {
		std::cout << "Warning: save file size not matching. Loading aborted." << std::endl;
		return;
	}
	file.seekg(0, std::ios::beg);
	file.read((char*)this->ram, size);
	file.close();
#ifdef _DEBUG
	std::cout << "Info: Cartridge ram loaded successfully." << std::endl;
#endif
}

void Cartridge::verifyHeader(int fileSize) {

	//check rom size
	if (header->romSize < 9) {
#ifdef _DEBUG
		std::cout << "Rom size code: " << (int)header->romSize << 
			"(" << (0x8000 << header->romSize) << " bytes)" << std::endl;
#endif
		if (fileSize > (0x8000 << header->romSize)) {
			fatal(FATAL_UNMATCHING_ROM_SIZE, __func__,
				"File size is " + std::to_string(fileSize) +
				" bytes, rom header says " + std::to_string((0x8000 << header->romSize)) + " bytes");
		}
	}
	else {
#ifdef _DEBUG
		std::cout << "Rom size code: " << header->romSize << std::endl;
#endif
		fatal(FATAL_UNSUPPORTED_ROM_SIZE, __func__);
	}

	//check cardridge type
#ifdef _DEBUG
	std::cout << "Cart type: " << (int)header->cartridgeType << 
		" (" << ((header->cartridgeType <= 0x22) ? 
			cardridge_type_info[header->cartridgeType] : "INVALID CODE") << 
		")" << std::endl;
#endif
	if ((header->cartridgeType > 0x3) && header->cartridgeType != 8
		&& header->cartridgeType != 9 && header->cartridgeType < 0x11
		&& header->cartridgeType > 0x13) {		//not a mbc1 or no mbc cartridge
		if (header->cartridgeType <= 22) {
			fatal(FATAL_UNSUPPORTED_MBC_CHIP, __func__, cardridge_type_info[header->cartridgeType]);
		}
		else {
			fatal(FATAL_UNSUPPORTED_MBC_CHIP, __func__, "INVALID CODE");
		}
	}

	//checks CGB flag
	if (header->cgbFlag == 0x80) {
#ifdef _DEBUG
		std::cout << "CGB flag: CGB and DMG compatible" << std::endl;
#endif
	}
	else if (header->cgbFlag == 0xc0) {
#ifdef _DEBUG
		std::cout << "CGB flag: only CGB compatible" << std::endl;
#endif
		fatal(FATAL_ROM_NOT_DMG_COMPATIBLE, __func__, "CGB header flag: " + std::to_string(header->cgbFlag));
	}
	else {
#ifdef _DEBUG
		std::cout << "CGB flag: DGB compatible" << std::endl;
#endif
	}

	//checks SGB flag
	if (header->sgbFlag == 0x0) {
#ifdef _DEBUG
		std::cout << "SGB flag: no SGB capable" << std::endl;
#endif
	}

	//checks header checksum
	uint8_t* ptr = header->title;
	uint8_t checksum = 0;
	for (ptr; ptr < (&header->headerChecksum); ptr++) {
		checksum -= (*ptr + 1);
	}
	if (checksum != header->headerChecksum) {
		fatal(FATAL_HEADER_CHECKSUM_DO_NOT_MATCH, __func__);
	}
#ifdef _DEBUG
	std::cout << "Header checksum: ok" << std::endl;
#endif


}

uint8_t Cartridge::read(uint16_t address) {

	if (address >= 0xa000 && address < 0xc000) {
		if (!ram_access)
			return 0;
		return ram[ramTranslateAddr(address)];
	}

	return rom[romTranslateAddr(address)];
}

void Cartridge::write(uint16_t address, uint8_t val) {

	if (address >= 0xa000 && address < 0xc000) {
		if (!ram_access)
			return;
		ram[ramTranslateAddr(address)] = val;
		return;
	}

	//rom writing for MBC control
	romWrite(address, val);
}


//NO MBC CHIP
void Cartridge::no_mbc_rom_write(uint16_t gb_addr, uint8_t val) {

	//enable and disable ram access
	if (gb_addr >= 0 && gb_addr <= 0x1fff) {
		if ((val & 0xf) == 0xa) {
			ram_access = true;
			return;
		}
		ram_access = false;
		return;
	}
	else if (gb_addr >= 0x2000 && gb_addr <= 0x3fff) {
		bank2_reg = val;
	}
	else if (gb_addr >= 0x4000 && gb_addr <= 0x5fff) {
		ram_bank = val;
	}
}

//return the traslated rom address
uint32_t Cartridge::no_mbc_rom_translate_func(uint16_t gb_addr) {

	if (gb_addr >= 0 && gb_addr <= 0x3fff) {
		return gb_addr + 0x4000 * bank1_reg;
	}

	return gb_addr + 0x4000 * (bank2_reg - 1);
}

//return the traslated ram address
uint32_t Cartridge::no_mbc_ram_translate_func(uint16_t gb_addr) {

	return (gb_addr - 0xa000 + 0x2000 * ram_bank);
}



//MBC1 CHIP: Max ram 32kB, max rom 2MB
uint32_t Cartridge::mbc1_rom_translate_func(uint16_t gb_addr) {
	if (gb_addr >= 0 && gb_addr <= 0x3fff) {
		if (!mode_reg) {		//mode 0: is used always bank 0
			return gb_addr;
		}
		//mode 1: used to access banks 0x20, 0x40 and 0x60
		return (gb_addr | ((bank2_reg << 5) << 14));
	}
	//0x4000 - 0x7fff range
	return (gb_addr & 0x3fff) | ((bank1_reg | (bank2_reg << 5)) << 14);
}

uint32_t Cartridge::mbc1_ram_translate_func(uint16_t gb_addr) {
	if (!mode_reg) {	//mode 0: bank2 register is ignored
		return (gb_addr & 0x1fff);
	}
	//mode 1: 
	return ((gb_addr & 0x1fff) | (bank2_reg << 13));
}

void Cartridge::mbc1_rom_write(uint16_t gb_addr, uint8_t val) {
	//enable and disable ram access
	if (gb_addr >= 0 && gb_addr <= 0x1fff) {
		if ((val & 0xf) == 0xa) {
			ram_access = true;
			return;
		}
		ram_access = false;
		return;
	}
	else if (gb_addr >= 0x2000 && gb_addr <= 0x3fff) {
		bank1_reg = (val & 0x1f);		//5 bits register. This register can't be 0
		if (bank1_reg == 0) bank1_reg = 1;
	}
	else if (gb_addr >= 0x4000 && gb_addr <= 0x5fff) {
		bank2_reg = val & 0x3;	//2 bit register
	}
	else if (gb_addr >= 0x6000 && gb_addr <= 0x7fff) {
		mode_reg = val & 0x1;	//1 bit mode register
	}
}

uint32_t Cartridge::mbc3_rom_translate_func(uint16_t gb_addr) {
	if (gb_addr >= 0 && gb_addr <= 0x3fff) {
		return gb_addr;		//always mapped as bank 0
	}
	//0x4000 - 0x7fff range
	return (gb_addr & 0x3fff) | (bank2_reg << 14);
}


uint32_t Cartridge::mbc3_ram_translate_func(uint16_t gb_addr) {
	return ((gb_addr & 0x1fff) | (ram_bank << 13));
}

void Cartridge::mbc3_rom_write(uint16_t gb_addr, uint8_t val) {
	//enable and disable ram access
	if (gb_addr >= 0 && gb_addr <= 0x1fff) {
		if ((val & 0xf) == 0xa) {
			ram_access = true;
			return;
		}
		ram_access = false;
		return;
	}
	else if (gb_addr >= 0x2000 && gb_addr <= 0x3fff) {
		bank2_reg = (val & 0x7f);		//7 bits register (max 128 banks). This register can't be 0 
		if (bank2_reg == 0) bank2_reg = 1;
	}
	else if (gb_addr >= 0x4000 && gb_addr <= 0x5fff) {
		ram_bank = val & 0x3;	//2 bit register (max 4 banks)
	}
}