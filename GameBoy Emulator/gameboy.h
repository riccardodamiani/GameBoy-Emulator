#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <cstdint>
#include <string>
#include <chrono>
#include <mutex>

#include "structures.h"
#include "renderer.h"
#include "sound.h"
class Cartridge;
class Lcd;
class Input;

class GameBoy {
public:
	GameBoy();
	bool Init(const char* rom_filename);
	int nextInstruction();
	int execute();
	int prefixed_execute();
	uint8_t* getVram(void);
	IO_map* getIOMap(void);
	uint8_t* getOam();
	void screenUpdate(int clocks);
	void saveState();
	bool* getSoundEnable();
private:
	struct registers registers;
	struct MBC mbc;

	uint8_t boot_rom[0x100];		//256 bytes
	uint8_t gb_mem[0x10000];	//memory mapped by 16 bit register (65536 bytes)
	uint8_t* vram;		//0x8000 - 0x9fff
	uint8_t* ext_ram;		//external bus for ram
	uint8_t* wram;	//work ram (0xc000 - 0xdfff)
	IO_map *io_map;		// input output memory map (0xff00 - 0xff7f)
	uint8_t* oam;		//(object attribute table) sprite information table (0xfe00 - 0xfe9f)
	Cartridge* cart;
	Sound* sound;
	joypad joypadStatus;
	scanlineStat frameStat[144];
	bool _saveState;
	std::mutex saveMutex;

	uint32_t time_clock;
	std::chrono::steady_clock::time_point realTimePoint;
	int videoMode;

	//Translate virtual gameboy addresses to actual memory addresses for the emulator and read or write the data.
	uint8_t read(uint16_t gb_address);
	void write(uint16_t gb_address, uint8_t value);
	void oam_dma_copy(void);
	
	uint8_t* translateAddr(uint16_t addr);
	int handleInterrupt(void);
	void handleTimer(int cycles);
	void handleJoypad(void);
	void handleSerial(void);
	void _internal_saveState();

	int RL_n(uint8_t& reg);
	int RR_n(uint8_t& reg);
	int RLC_n(uint8_t& reg);
	int RRC_n(uint8_t& reg);
	int SWAP_n(uint8_t& reg);
	int LD_r1_r2(uint8_t& reg1, uint8_t& reg2);
	int SLA_n(uint8_t& reg);
	int SRA_n(uint8_t& reg);
	int SBC_A_n(uint8_t& reg);
	int ADC_A_n(uint8_t& reg);
	int OR_n(uint8_t& reg);
	int AND_n(uint8_t& reg);
	int XOR_n(uint8_t& reg);
	int CP_n(uint8_t& reg);
	int SRL_n(uint8_t& reg);
	int SUB_n(uint8_t& reg);
	int ADD_n(uint8_t& reg);
};


#endif
