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
class Input;

class GameBoy {
public:
	GameBoy();
	bool Init();
	int nextInstruction();
	int execute();
	int prefixed_execute();
	bool* getSoundEnable();
	void setClockSpeed(float multiplier);
	void runFor(int cycles);
private:
	struct registers registers;
	
	Sound* sound;
	joypad joypadStatus;
	float clockSpeed;

	uint32_t time_clock;
	std::chrono::steady_clock::time_point realTimePoint;

	
	int handleInterrupt(void);
	void handleTimer(int cycles);
	void handleJoypad(void);
	void handleSerial(void);

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
