#include "gameboy.h"
#include "structures.h"
#include "errors.h"
#include "cartridge.h"
#include "renderer.h"
#include "sound.h"
#include "input.h"
#include "globals.h"
#include "memory.h"
#include "ppu.h"

#include <iostream>
#include <fstream>
#include <cstdint>
#include <string.h>
#include <thread>
#include <chrono>


GameBoy::GameBoy(){

}

bool GameBoy::Init() {
	
	clockSpeed = 1;
	registers.clock_cnt = 0;
	time_clock = 0;
	doubleSpeed = 0;	//normal speed

	//init mem
	memset(&registers, 0, sizeof(registers));
	registers.pc = 0;

	//init joypad stuff
	registers.joyp_stat = 1;
	joypadStatus = {};

	return true;
}

/*bool* GameBoy::getSoundEnable() {
	return this->sound->getSoundEnable();
}*/


void GameBoy::setClockSpeed(float multiplier) {
	clockSpeed = multiplier;
}

void GameBoy::runFor(int cycles) {

	joypadStatus = _input->getJoypadState();		//get joypad state
	int clk = 0;
	while (clk < cycles*clockSpeed) {
		clk += nextInstruction();
	}
	_sound->UpdateSound(_memory->getIOMap());
}

int GameBoy::nextInstruction() {

	unsigned int m_cycles = 0;
	IO_map* io = _memory->getIOMap();

	m_cycles = handleInterrupt();
	int cycles;

	if (!registers.halted && !registers.stopped) {
		m_cycles += this->execute();
		cycles = (m_cycles * 4) >> doubleSpeed;

		//update divider register at a rate of 16384Hz 
		registers.div_cnt += cycles;
		if (registers.div_cnt >= 256) {
			registers.div_cnt -= 256;
			io->DIV++;
		}
	}
	else {
		m_cycles += 1;		//lcd and the timer still need the clock to work in halt mode
		cycles = (m_cycles * 4) >> doubleSpeed;

		//double speed
		if (_GBC_Mode && (io->KEY1 & 0x1)) {
			io->KEY1 = 0;
			registers.stopped = 0;
			doubleSpeed = !doubleSpeed;
		}
	}

	handleJoypad();
	if (!registers.stopped) {
		handleSerial();
		handleTimer(m_cycles * 4);
		_ppu->drawScanline(cycles);
	}

	registers.clock_cnt += m_cycles * 4;
	return cycles;
}


void GameBoy::handleTimer(int cycles) {
	IO_map* io_map = _memory->getIOMap();

	if (!(io_map->TAC & 0x4))
		return;

	registers.timer_clk += cycles;
	int div_flag = (io_map->TAC & 0x3);
	int divider = (div_flag == 0) ? 1024 : (4 << (div_flag*2));
	if (registers.timer_clk >= divider) {
		registers.timer_clk -= divider;
		io_map->TIMA++;
		if (io_map->TIMA == 0) {	//overflow
			io_map->TIMA = io_map->TMA;		//reload timer
			io_map->IF |= 0x4;		//timer IRQ asserted
		}
	}
	
}

void GameBoy::handleJoypad(void) {
	IO_map* io_map = _memory->getIOMap();

	if (registers.stopped) {
		if (joypadStatus.a || joypadStatus.b || joypadStatus.select || joypadStatus.start ||
			joypadStatus.right || joypadStatus.left || joypadStatus.up || joypadStatus.down) {
			registers.stopped = false;
			return;
		}
	}

	if ((io_map->JOYP & 0x30) == 0x30) {		//joypad output not selected
		io_map->JOYP = 0xff;
		return;
	}
	io_map->JOYP |= 0xcf;		//clears all bits except the control ones (4th and 5th)

	if (!(io_map->JOYP & 0x20)) {		//4th bit selected (start, select, a and b buttons)
		if (joypadStatus.a) io_map->JOYP &= ~(0x1);
		if (joypadStatus.b) io_map->JOYP &= ~(0x2);
		if (joypadStatus.select) io_map->JOYP &= ~(0x4);
		if (joypadStatus.start) io_map->JOYP &= ~(0x8);
	}

	if (!(io_map->JOYP & 0x10)) {			//5th bit selected (arrow buttons)
		if (joypadStatus.right) io_map->JOYP &= ~(0x1);
		if (joypadStatus.left) io_map->JOYP &= ~(0x2);
		if (joypadStatus.up) io_map->JOYP &= ~(0x4);
		if (joypadStatus.down) io_map->JOYP &= ~(0x8);
	}
	
	//if previous joypad stat is 1 and current is 0, IRQ is triggered
	if (registers.joyp_stat && !((io_map->JOYP & 0xf) == 0xf)) {
		io_map->IF |= 0x10;
	}
	registers.joyp_stat = ((io_map->JOYP&0xf) == 0xf);		//logical and of all bits of joypad buttons
}

int GameBoy::handleInterrupt(void) {
	IO_map* io_map = _memory->getIOMap();

	if (registers.IME_CC > 0) {
		if (--registers.IME_CC == 0) {
			registers.IME = registers.IME_U;
		}
	}

	if (registers.IME && (io_map->IF & io_map->IE)) {

		for (int i = 0; i < 5; i++) {
			if ((io_map->IF & (0x1 << i)) && (io_map->IE & (0x1 << i))) {
				registers.halted = 0;
				registers.stopped = 0;

				uint16_t interrupt_vect_addr = 0x40 + 0x8 * i;
				//PUSH PC
				_memory->write(registers.sp - 1, (registers.pc>>8) & 0xff);
				_memory->write(registers.sp - 2, registers.pc & 0xff);
				registers.sp -= 2;

				registers.IME = 0;		//disable interrupt
				io_map->IF &= ~(0x1 << i);	//clear interrupt flag
				registers.pc = interrupt_vect_addr;		//jump to the corrisponding interrupt vector
				return 5;
			}
		}
	}
	return 0;
}

//serial is ignored
void GameBoy::handleSerial(void) {

	//the serial registers are ignored since the transfer
	//can start only if the control byte is 0x80 or 0x81 and 
	//there is another gameboy connected to this one.
	//If the transfer doesn't start the control byte is never cleared
	//and the data byte stay the same.
	
}

//execute an instruction, update the pc and return the number of cycles used
int GameBoy::execute() {

	uint16_t pc = registers.pc;

	uint8_t opcode = _memory->read(pc);
	if (pc == 0xd8) {
		pc = pc;
	}
	switch (opcode) {
	case 0x0:		//NOP
	{
		registers.pc += 1;
		return 1;
	}
	case 0x1:		//LD BD, d16
	{
		registers.c = _memory->read(pc + 1);
		registers.b = _memory->read(pc + 2);
		registers.pc += 3;
		return 3;
	}
	case 0x02:		//LD (BC), A
	{
		uint16_t bc = (registers.b << 8) | registers.c;
		_memory->write(bc, registers.a);
		registers.pc += 1;
		return 2;
	}
	case 0x3:		//INC BC
	{
		uint16_t bc = (registers.b << 8) | registers.c;
		bc++;
		registers.b = (bc >> 8) & 0xff;
		registers.c = bc & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x4:		//INC B
	{
		registers.flag.h = ((registers.b & 0xf) == 0xf);

		registers.b++;
		registers.flag.z = (registers.b == 0);
		registers.flag.n = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x5:		//DEC B
	{
		registers.flag.h = ((registers.b & 0xf) == 0);	//needs a half carry?

		registers.b--;
		registers.flag.z = (registers.b == 0);
		registers.flag.n = 1;
		
		registers.pc += 1;
		return 1;
	}
	case 0x6:		//LD B, d8
	{
		registers.b = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0x7:		//RLCA
	{
		registers.flag.c = ((registers.a & 0x80) != 0);
		registers.a <<= 1;
		registers.a |= registers.flag.c;
		registers.flag.n = 0;
		registers.flag.h = 0;
		registers.flag.z = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x08:		//LD (a16), SP
	{
		uint16_t gb_addr = (_memory->read(pc + 1) | _memory->read(pc + 2) << 8);
		_memory->write(gb_addr, registers.sp & 0xff);
		_memory->write(gb_addr + 1, (registers.sp >> 8) & 0xff);
		registers.pc += 3;
		return 5;
	}
	case 0x9:		//ADD HL, BC
	{
		uint16_t bc = (registers.b << 8) | registers.c;
		uint16_t hl = (registers.h << 8) | registers.l;
		uint32_t sum = hl + bc;

		registers.flag.h = (((hl & 0xfff) + (bc & 0xfff)) > 0xfff);

		hl += bc;

		registers.flag.n = 0;
		registers.flag.c = (sum > 0xffff);

		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0xa:		//LD A, (BC)
	{
		uint16_t bc = (registers.b << 8) | registers.c;
		registers.a = _memory->read(bc);
		registers.pc += 1;
		return 2;
	}
	case 0xb:		//DEC BC
	{
		uint16_t bc = (registers.b << 8) | registers.c;
		bc--;
		registers.b = (bc >> 8) & 0xff;
		registers.c = bc & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0xc:		//INC C
	{
		registers.flag.h = ((registers.c & 0xf) == 0xf);

		registers.c++;
		registers.flag.z = (registers.c == 0);
		registers.flag.n = 0;
		
		registers.pc += 1;
		return 1;
	}
	case 0xd:		//DEC C
	{
		registers.flag.h = ((registers.c & 0xf) == 0);	//needs a half carry?

		registers.c--;
		registers.flag.z = (registers.c == 0);
		registers.flag.n = 1;

		registers.pc += 1;
		return 1;
	}
	case 0xe:	//LD C, d8
	{
		registers.c = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0xf:		//RRCA
	{
		registers.flag.c = ((registers.a & 0x1) != 0);
		registers.a >>= 1;
		registers.a |= (registers.flag.c << 7);
		registers.flag.n = 0;
		registers.flag.h = 0;
		registers.flag.z = 0;
		
		registers.pc += 1;
		return 1;
	}
	case 0x10:		//STOP d8
	{
		registers.stopped = true;
		_sound->Halt();
		registers.pc += 2;
		return 1;
	}
	case 0x11:		//LD DE, d16
	{
		registers.e = _memory->read(pc + 1);
		registers.d = _memory->read(pc + 2);
		registers.pc += 3;
		return 3;
	}
	case 0x12:		//LD (DE), A
	{
		uint16_t de = (registers.d << 8) | registers.e;
		_memory->write(de, registers.a);
		registers.pc += 1;
		return 2;
	}
	case 0x13:		//INC DE
	{
		uint16_t de = (registers.d << 8) | registers.e;
		de++;
		registers.d = (de >> 8) & 0xff;
		registers.e = de & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x14:		//INC D
	{
		registers.flag.h = ((registers.d & 0xf) == 0xf);

		registers.d++;
		registers.flag.z = (registers.d == 0);
		registers.flag.n = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x15:		//DEC D
	{
		registers.flag.h = ((registers.d & 0xf) == 0);	//needs a half carry?

		registers.d--;
		registers.flag.z = (registers.d == 0);
		registers.flag.n = 1;

		registers.pc += 1;
		return 1;
	}
	case 0x16:		//LD D, d8
	{
		registers.d = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0x17:		//RLA
	{
		uint8_t bit0 = registers.flag.c;
		registers.flag.c = ((registers.a & 0x80) != 0);
		registers.a <<= 1;
		registers.a |= bit0;
		registers.flag.n = 0;
		registers.flag.h = 0;
		registers.flag.z = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x18:		//JR d8
	{
		char jump = _memory->read(pc + 1);
		registers.pc = (unsigned)((short)registers.pc + jump);
		registers.pc += 2;
		return 3;
	}
	case 0x19:		//ADD HL, DE
	{
		uint16_t de = (registers.d << 8) | registers.e;
		uint16_t hl = (registers.h << 8) | registers.l;
		uint32_t sum = hl + de;

		registers.flag.h = (((hl & 0xfff) + (de & 0xfff)) > 0xfff);

		hl += de;

		registers.flag.n = 0;
		registers.flag.c = (sum > 0xffff);

		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x1a:		//LD A, (DE)
	{
		uint16_t de = (registers.d << 8) | registers.e;
		registers.a = _memory->read(de);
		registers.pc += 1;
		return 2;
	}
	case 0x1b:		//DEC DE
	{
		uint16_t de = (registers.d << 8) | registers.e;
		de--;
		registers.d = (de >> 8) & 0xff;
		registers.e = de & 0xff;

		registers.pc += 1;
		return 2;
	}
	case 0x1c:		//INC E
	{
		registers.flag.h = ((registers.e & 0xf) == 0xf);

		registers.e++;
		registers.flag.z = (registers.e == 0);
		registers.flag.n = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x1d:		//DEC E
	{
		registers.flag.h = ((registers.e & 0xf) == 0);	//needs a half carry?

		registers.e--;
		registers.flag.z = (registers.e == 0);
		registers.flag.n = 1;

		registers.pc += 1;
		return 1;
	}
	case 0x1e:		//LD E, d8
	{
		registers.e = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0x1f:		//RRA
	{
		uint8_t bit7 = registers.flag.c;
		registers.flag.c = ((registers.a & 0x1) != 0);
		registers.a >>= 1;
		registers.a |= (bit7 << 7);
		registers.flag.n = 0;
		registers.flag.h = 0;
		registers.flag.z = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x20:		//JR NZ, d8
	{
		char jump = _memory->read(pc + 1);
		if (!registers.flag.z) {
			registers.pc = (unsigned)((short)registers.pc + jump);
			registers.pc += 2;
			return 3;
		}
		registers.pc += 2;
		return 2;
	}

	case 0x21:		//LD HL, d16
	{
		registers.l = _memory->read(pc + 1);
		registers.h = _memory->read(pc + 2);
		registers.pc += 3;
		return 3;
	}
	case 0x22:		//LD (HL+), A
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.a);
		hl++;
		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x23:		//INC HL
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		hl++;
		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x24:		//INC H
	{
		registers.flag.h = ((registers.h & 0xf) == 0xf);

		registers.h++;
		registers.flag.z = (registers.h == 0);
		registers.flag.n = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x25:		//DEC H
	{
		registers.flag.h = ((registers.h & 0xf) == 0);	//needs a half carry?

		registers.h--;
		registers.flag.z = (registers.h == 0);
		registers.flag.n = 1;

		registers.pc += 1;
		return 1;
	}
	case 0x26:		//LD H, d8
	{
		registers.h = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0x27:		//DAA
	{
		uint8_t &a = registers.a;
		uint8_t cf = registers.flag.c;

		if (registers.flag.n) {		//subtraction in last math instruction
			//4 lower nibbles greater than 9
			if (((a & 0xf) > 9) || registers.flag.h) {
				registers.flag.c |= (a < 6);
				a -= 6;
			}
			//4 upper nibbles greater than 9
			if ((a > 0x9f) || cf) {
				a -= 0x60;
				registers.flag.c = 1;
			}
			else {
				registers.flag.c = 0;
			}
		}
		else {
			//4 lower nibbles greater than 9
			if (((a & 0xf) > 9) || registers.flag.h) {
				registers.flag.c |= (((uint16_t)a + 6) > 0xff);
				a += 6;
			}

			//4 upper nibbles greater than 9
			if ((a > 0x9f) || cf) {
				registers.flag.c = 1;
				a += 0x60;
			}
			else {
				registers.flag.c = 0;
			}
		}

		registers.flag.z = (registers.a == 0);
		registers.flag.h = 0;
		registers.pc += 1;
		return 1;
	}
	case 0x28:		//JR Z, d8
	{
		char jump = _memory->read(pc + 1);
		if (registers.flag.z) {
			registers.pc = (unsigned)((short)registers.pc + jump);
			registers.pc += 2;
			return 3;
		}
		registers.pc += 2;
		return 2;
	}
	case 0x29:		//ADD HL, HL
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint32_t sum = hl + hl;

		registers.flag.h = (((hl & 0xfff) + (hl & 0xfff)) > 0xfff);
		hl += hl;
		registers.flag.n = 0;
		registers.flag.c = (sum > 0xffff);

		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x2a:		//LD A, (HL+)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.a = _memory->read(hl);
		hl++;
		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x2b:		//DEC HL
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		hl--;
		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x2c:		//INC L
	{
		registers.flag.h = ((registers.l & 0xf) == 0xf);

		registers.l++;
		registers.flag.z = (registers.l == 0);
		registers.flag.n = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x2d:		//DEC L
	{
		registers.flag.h = ((registers.l & 0xf) == 0);	//needs a half carry?

		registers.l--;
		registers.flag.z = (registers.l == 0);
		registers.flag.n = 1;

		registers.pc += 1;
		return 1;
	}
	case 0x2e:		//LD L, d8
	{
		registers.l = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0x2f:		//CPL
	{
		registers.a = ~registers.a;
		registers.flag.n = 1;
		registers.flag.h = 1;
		registers.pc += 1;
		return 1;
	}
	case 0x30:		//JR NC d8
	{
		char jump = _memory->read(pc + 1);
		if (!registers.flag.c) {
			registers.pc = (unsigned)((short)registers.pc + jump);
			registers.pc += 2;
			return 3;
		}
		registers.pc += 2;
		return 2;
	}
	case 0x31:		//LD SP, d16
	{
		registers.sp = (_memory->read(pc + 1) | _memory->read(pc + 2) << 8);
		registers.pc += 3;
		return 3;
	}
	case 0x32:		//LD (HL-), A
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.a);
		hl--;
		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x33:		//INC SP
	{
		registers.sp++;
		registers.pc += 1;
		return 2;
	}
	case 0x34:		//INC (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);

		registers.flag.h = ((n & 0xf) == 0xf);
		n++;
		registers.flag.z = (n == 0);
		registers.flag.n = 0;
		_memory->write(hl, n);

		registers.pc += 1;
		return 3;
	}
	case 0x35:		//DEC (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);

		registers.flag.h = ((n & 0xf) == 0);	//needs a half carry?
		n--;
		registers.flag.z = (n == 0);
		registers.flag.n = 1;
		_memory->write(hl, n);

		registers.pc += 1;
		return 3;
	}
	case 0x36:		//LD (HL), d8
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(pc + 1);
		_memory->write(hl, n);

		registers.pc += 2;
		return 3;
	}
	case 0x37:		//SCF
	{
		registers.flag.c = 1;
		registers.flag.n = 0;
		registers.flag.h = 0;
		registers.pc += 1;
		return 1;
	}
	case 0x38:		//JR C d8
	{
		char jump = _memory->read(pc + 1);
		if (registers.flag.c) {
			registers.pc = (unsigned)((short)registers.pc + jump);
			registers.pc += 2;
			return 3;
		}
		registers.pc += 2;
		return 2;
	}
	case 0x39:		//ADD HL, SP
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint32_t sum = hl + registers.sp;

		registers.flag.h = (((hl & 0xfff) + (registers.sp & 0xfff)) > 0xfff);
		hl += registers.sp;
		registers.flag.n = 0;
		registers.flag.c = (sum > 0xffff);

		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x3a:		//LD A, (HL-)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.a = _memory->read(hl);
		hl--;
		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 1;
		return 2;
	}
	case 0x3b:		//DEC SP
	{
		registers.sp--;
		registers.pc += 1;
		return 2;
	}
	case 0x3c:		//INC A
	{
		registers.flag.h = ((registers.a & 0xf) == 0xf);

		registers.a++;
		registers.flag.z = (registers.a == 0);
		registers.flag.n = 0;

		registers.pc += 1;
		return 1;
	}
	case 0x3d:		//DEC A
	{
		registers.flag.h = ((registers.a & 0xf) == 0);	//needs a half carry?

		registers.a--;
		registers.flag.z = (registers.a == 0);
		registers.flag.n = 1;

		registers.pc += 1;
		return 1;
	}
	case 0x3e:		//LD A, d8
	{
		registers.a = _memory->read(pc + 1);
		registers.pc += 2;
		return 2;
	}
	case 0x3f:		//CCF
	{
		registers.flag.c = ~registers.flag.c;
		registers.flag.n = 0;
		registers.flag.h = 0;
		registers.pc += 1;
		return 1;
	}
	case 0x40:		//LD B, B
	{
		return LD_r1_r2(registers.b, registers.b);
	}
	case 0x41:		//LD B, C
	{
		return LD_r1_r2(registers.b, registers.c);
	}
	case 0x42:		//LD B, D
	{
		return LD_r1_r2(registers.b, registers.d);
	}
	case 0x43:		//LD B, E
	{
		return LD_r1_r2(registers.b, registers.e);
	}
	case 0x44:		//LD B, H
	{
		return LD_r1_r2(registers.b, registers.h);
	}
	case 0x45:		//LD B, L
	{
		return LD_r1_r2(registers.b, registers.l);
	}
	case 0x46:		//LD B, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.b = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x47:		//LD B, A
	{
		return LD_r1_r2(registers.b, registers.a);
	}
	case 0x48:		//LD C, B
	{
		return LD_r1_r2(registers.c, registers.b);
	}
	case 0x49:		//LD C, C
	{
		return LD_r1_r2(registers.c, registers.c);
	}
	case 0x4a:		//LD C, D
	{
		return LD_r1_r2(registers.c, registers.d);
	}
	case 0x4b:		//LD C, E
	{
		return LD_r1_r2(registers.c, registers.e);
	}
	case 0x4c:		//LD C, H
	{
		return LD_r1_r2(registers.c, registers.h);
	}
	case 0x4d:		//LD C, L
	{
		return LD_r1_r2(registers.c, registers.l);
	}
	case 0x4e:		//LD C, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.c = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x4f:		//LD C, A
	{
		return LD_r1_r2(registers.c, registers.a);
	}

	case 0x50:		//LD D, B
	{
		return LD_r1_r2(registers.d, registers.b);
	}
	case 0x51:		//LD D, C
	{
		return LD_r1_r2(registers.d, registers.c);
	}
	case 0x52:		//LD D, D
	{
		return LD_r1_r2(registers.d, registers.d);
	}
	case 0x53:		//LD D, E
	{
		return LD_r1_r2(registers.d, registers.e);
	}
	case 0x54:		//LD D, H
	{
		return LD_r1_r2(registers.d, registers.h);
	}
	case 0x55:		//LD D, L
	{
		return LD_r1_r2(registers.d, registers.l);
	}
	case 0x56:		//LD D, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.d = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x57:		//LD D, A
	{
		return LD_r1_r2(registers.d, registers.a);
	}

	case 0x58:		//LD E, B
	{
		return LD_r1_r2(registers.e, registers.b);
	}
	case 0x59:		//LD E, C
	{
		return LD_r1_r2(registers.e, registers.c);
	}
	case 0x5a:		//LD E, D
	{
		return LD_r1_r2(registers.e, registers.d);
	}
	case 0x5b:		//LD E, E
	{
		return LD_r1_r2(registers.e, registers.e);
	}
	case 0x5c:		//LD E, H
	{
		return LD_r1_r2(registers.e, registers.h);
	}
	case 0x5d:		//LD E, L
	{
		return LD_r1_r2(registers.e, registers.l);
	}
	case 0x5e:		//LD E, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.e = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x5f:		//LD E, A
	{
		return LD_r1_r2(registers.e, registers.a);
	}
	case 0x60:		//LD H, B
	{
		return LD_r1_r2(registers.h, registers.b);
	}
	case 0x61:		//LD H, C
	{
		return LD_r1_r2(registers.h, registers.c);
	}
	case 0x62:		//LD H, D
	{
		return LD_r1_r2(registers.h, registers.d);
	}
	case 0x63:		//LD H, E
	{
		return LD_r1_r2(registers.h, registers.e);
	}
	case 0x64:		//LD H, H
	{
		return LD_r1_r2(registers.h, registers.h);
	}
	case 0x65:		//LD H, L
	{
		return LD_r1_r2(registers.h, registers.l);
	}
	case 0x66:		//LD H, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.h = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x67:		//LD H, A
	{
		return LD_r1_r2(registers.h, registers.a);
	}
	case 0x68:		//LD L, B
	{
		return LD_r1_r2(registers.l, registers.b);
	}
	case 0x69:		//LD L, C
	{
		return LD_r1_r2(registers.l, registers.c);
	}
	case 0x6a:		//LD L, D
	{
		return LD_r1_r2(registers.l, registers.d);
	}
	case 0x6b:		//LD L, E
	{
		return LD_r1_r2(registers.l, registers.e);
	}
	case 0x6c:		//LD L, H
	{
		return LD_r1_r2(registers.l, registers.h);
	}
	case 0x6d:		//LD L, L
	{
		return LD_r1_r2(registers.l, registers.l);
	}
	case 0x6e:		//LD L, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.l = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x6f:		//LD L, A
	{
		return LD_r1_r2(registers.l, registers.a);
	}
	case 0x70:		//LD (HL), B
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.b);

		registers.pc += 1;
		return 2;
	}
	case 0x71:		//LD (HL), C
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.c);

		registers.pc += 1;
		return 2;
	}
	case 0x72:		//LD (HL), D
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.d);

		registers.pc += 1;
		return 2;
	}
	case 0x73:		//LD (HL), E
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.e);

		registers.pc += 1;
		return 2;
	}
	case 0x74:		//LD (HL), H
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.h);

		registers.pc += 1;
		return 2;
	}
	case 0x75:		//LD (HL), L
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.l);

		registers.pc += 1;
		return 2;
	}
	case 0x76:		//HALT
	{
		if (registers.IME) {		//interrupt are enabled
			registers.halted = 1;
		}
		registers.pc += 1;
		return 1;
	}
	case 0x77:		//LD (HL), A
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		_memory->write(hl, registers.a);
		
		registers.pc += 1;
		return 2;
	}
	case 0x78:		//LD A, B
	{
		return LD_r1_r2(registers.a, registers.b);
	}
	case 0x79:		//LD A, C
	{
		return LD_r1_r2(registers.a, registers.c);
	}
	case 0x7a:		//LD A, D
	{
		return LD_r1_r2(registers.a, registers.d);
	}
	case 0x7b:		//LD A, E
	{
		return LD_r1_r2(registers.a, registers.e);
	}
	case 0x7c:		//LD A, H
	{
		return LD_r1_r2(registers.a, registers.h);
	}
	case 0x7d:		//LD A, L
	{
		return LD_r1_r2(registers.a, registers.l);
	}
	case 0x7e:		//LD A, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.a = _memory->read(hl);
		registers.pc += 1;
		return 2;
	}
	case 0x7f:		//LD A, A
	{
		return LD_r1_r2(registers.a, registers.a);
	}
	case 0x80:		//ADD A, B
	{
		return ADD_n(registers.b);
	}
	case 0x81:		//ADD A, C
	{
		return ADD_n(registers.c);
	}
	case 0x82:		//ADD A, D
	{
		return ADD_n(registers.d);
	}
	case 0x83:		//ADD A, E
	{
		return ADD_n(registers.e);
	}
	case 0x84:		//ADD A, H
	{
		return ADD_n(registers.h);
	}
	case 0x85:		//ADD A, L
	{
		return ADD_n(registers.l);
	}
	case 0x86:		//ADD A, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		ADD_n(n);
		return 2;
	}
	case 0x87:		//ADD A
	{
		return ADD_n(registers.a);
	}
	case 0x88:		//ADC A, B
	{
		return ADC_A_n(registers.b);
	}
	case 0x89:		//ADC A, C
	{
		return ADC_A_n(registers.c);
	}
	case 0x8a:		//ADC A, D
	{
		return ADC_A_n(registers.d);
	}
	case 0x8b:		//ADC A, E
	{
		return ADC_A_n(registers.e);
	}
	case 0x8c:		//ADC A, H
	{
		return ADC_A_n(registers.h);
	}
	case 0x8d:		//ADC A, L
	{
		return ADC_A_n(registers.l);
	}
	case 0x8e:		//ADC A, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		ADC_A_n(n);
		return 2;
	}
	case 0x8f:		//ADC A, A
	{
		return ADC_A_n(registers.a);
	}
	case 0x90:		//SUB B
	{
		return SUB_n(registers.b);
	}
	case 0x91:		//SUB C
	{
		return SUB_n(registers.c);
	}
	case 0x92:		//SUB D
	{
		return SUB_n(registers.d);
	}
	case 0x93:		//SUB E
	{
		return SUB_n(registers.e);
	}
	case 0x94:		//SUB H
	{
		return SUB_n(registers.h);
	}
	case 0x95:		//SUB L
	{
		return SUB_n(registers.l);
	}
	case 0x96:		//SUB (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		SUB_n(n);
		return 2;
	}
	case 0x97:		//SUB A
	{
		return SUB_n(registers.a);
	}
	case 0x98:		//SBC A, B
	{
		return SBC_A_n(registers.b);
	}
	case 0x99:		//SBC A, C
	{
		return SBC_A_n(registers.c);
	}
	case 0x9a:		//SBC A, D
	{
		return SBC_A_n(registers.d);
	}
	case 0x9b:		//SBC A, E
	{
		return SBC_A_n(registers.e);
	}
	case 0x9c:		//SBC A, H
	{
		return SBC_A_n(registers.h);
	}
	case 0x9d:		//SBC A, L
	{
		return SBC_A_n(registers.l);
	}
	case 0x9e:		//SBC A, (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		SBC_A_n(n);
		return 2;
	}
	case 0x9f:		//SBC A, A
	{
		return SBC_A_n(registers.a);
	}
	case 0xa0:		//AND B
	{
		return AND_n(registers.b);
	}
	case 0xa1:		//AND C
	{
		return AND_n(registers.c);
	}
	case 0xa2:		//AND D
	{
		return AND_n(registers.d);
	}
	case 0xa3:		//AND E
	{
		return AND_n(registers.e);
	}
	case 0xa4:		//AND H
	{
		return AND_n(registers.h);
	}
	case 0xa5:		//AND L
	{
		return AND_n(registers.l);
	}
	case 0xa6:		//AND (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		AND_n(n);
		return 2;
	}
	case 0xa7:		//AND A
	{
		return AND_n(registers.a);
	}
	case 0xa8:		//XOR B
	{
		return XOR_n(registers.b);
	}
	case 0xa9:		//XOR C
	{
		return XOR_n(registers.c);
	}
	case 0xaa:		//XOR D
	{
		return XOR_n(registers.d);
	}
	case 0xab:		//XOR E
	{
		return XOR_n(registers.e);
	}
	case 0xac:		//XOR H
	{
		return XOR_n(registers.h);
	}
	case 0xad:		//XOR L
	{
		return XOR_n(registers.l);
	}
	case 0xae:		//XOR (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		XOR_n(n);
		return 2;
	}
	case 0xaf:		//XOR A
	{
		return XOR_n(registers.a);
	}
	case 0xb0:		//OR B
	{
		return OR_n(registers.b);
	}
	case 0xb1:		//OR C
	{
		return OR_n(registers.c);
	}
	case 0xb2:		//OR D
	{
		return OR_n(registers.d);
	}
	case 0xb3:		//OR E
	{
		return OR_n(registers.e);
	}
	case 0xb4:		//OR H
	{
		return OR_n(registers.h);
	}
	case 0xb5:		//OR L
	{
		return OR_n(registers.l);
	}
	case 0xb6:		//OR (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		OR_n(n);
		return 2;
	}
	case 0xb7:		//OR A
	{
		return OR_n(registers.a);
	}
	case 0xb8:		//CP B
	{
		return CP_n(registers.b);
	}
	case 0xb9:		//CP C
	{
		return CP_n(registers.c);
	}
	case 0xba:		//CP D
	{
		return CP_n(registers.d);
	}
	case 0xbb:		//CP E
	{
		return CP_n(registers.e);
	}
	case 0xbc:		//CP H
	{
		return CP_n(registers.h);
	}
	case 0xbd:		//CP L
	{
		return CP_n(registers.l);
	}
	case 0xbe:		//CP (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);

		CP_n(n);
		return 2;
	}
	case 0xbf:		//CP A
	{
		return CP_n(registers.a);
	}
	case 0xc0:		//RET NZ
	{
		if (!registers.flag.z) {
			registers.pc = (_memory->read(registers.sp) | (_memory->read(registers.sp + 1) << 8));
			registers.sp += 2;
			return 5;
		}

		registers.pc += 1;
		return 2;
	}
	case 0xc1:		//POP BC
	{
		registers.c = _memory->read(registers.sp);
		registers.b = _memory->read(registers.sp + 1);
		registers.sp += 2;
		registers.pc += 1;
		return 3;
	}
	case 0xc2:		//JP NZ d16
	{
		if (!registers.flag.z) {
			uint16_t addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
			registers.pc = addr;
			return 4;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xc3:		//JP d16
	{
		uint16_t addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
		registers.pc = addr;
		return 4;
	}
	case 0xc4:		//CALL NZ
	{
		if (!registers.flag.z) {
			uint16_t jump_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
			_memory->write(registers.sp - 2, ret_addr & 0Xff);

			registers.sp -= 2;
			registers.pc = jump_addr;
			return 6;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xc5:		//PUSH BC
	{
		_memory->write(registers.sp - 1, registers.b);
		_memory->write(registers.sp - 2, registers.c);
		registers.sp -= 2;
		registers.pc += 1;
		return 4;
	}
	
	case 0xc6:		//ADD A, d8
	{
		uint8_t n = _memory->read(pc + 1);
		ADD_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xc7:		//RST 0x0
	{
		uint16_t jump_addr = 0x0;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xc8:		//RET Z
	{
		if (registers.flag.z) {
			registers.pc = (_memory->read(registers.sp) | (_memory->read(registers.sp + 1) << 8));
			registers.sp += 2;
			return 5;
		}
		
		registers.pc += 1;
		return 2;
	}
	case 0xc9:		//RET
	{
		registers.pc = (_memory->read(registers.sp) | (_memory->read(registers.sp + 1) << 8));
		registers.sp += 2;
		return 4;
	}
	case 0xca:		//JP Z d16
	{
		if (registers.flag.z) {
			uint16_t addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
			registers.pc = addr;
			return 4;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xcb:		//PREFIX
	{
		registers.pc += 1;
		return prefixed_execute();
	}
	case 0xcc:		//CALL Z a16
	{
		if (registers.flag.z) {
			uint16_t jump_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
			_memory->write(registers.sp - 2, ret_addr & 0Xff);

			registers.sp -= 2;
			registers.pc = jump_addr;
			return 6;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xcd:		//CALL 
	{
		uint16_t jump_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));

		uint16_t ret_addr = pc + 3;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 6;
	}
	case 0xce:		//ADC A, d8
	{
		uint8_t n = _memory->read(pc + 1);
		ADC_A_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xcf:		//RST 0x8
	{
		uint16_t jump_addr = 0x8;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xd0:		//RET NC
	{
		if (!registers.flag.c) {
			registers.pc = (_memory->read(registers.sp) | (_memory->read(registers.sp + 1) << 8));
			registers.sp += 2;
			return 5;
		}
		
		registers.pc += 1;
		return 2;
	}
	case 0xd1:		//POP DE
	{
		registers.e = _memory->read(registers.sp);
		registers.d = _memory->read(registers.sp + 1);
		registers.sp += 2;
		registers.pc += 1;
		return 3;
	}
	case 0xd2:		//JP NC d16
	{
		if (!registers.flag.c) {
			uint16_t addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
			registers.pc = addr;
			return 4;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xd3:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xd3\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xd4:		//CALL NC a16
	{
		if (!registers.flag.c) {
			uint16_t jump_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
			_memory->write(registers.sp - 2, ret_addr & 0Xff);

			registers.sp -= 2;
			registers.pc = jump_addr;
			return 6;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xd5:		//PUSH DE
	{
		_memory->write(registers.sp - 1, registers.d);
		_memory->write(registers.sp - 2, registers.e);
		registers.sp -= 2;
		registers.pc += 1;
		return 4;
	}
	case 0xd6:		//SUB d8
	{
		uint8_t n = _memory->read(pc + 1);
		registers.flag.h = ((registers.a & 0xf) < (n & 0xf));
		registers.flag.c = (registers.a < n);

		registers.a -= n;
		registers.flag.z = (registers.a == 0);
		registers.flag.n = 1;

		registers.pc += 2;
		return 2;
	}
	case 0xd7:		//RST 0x10
	{
		uint16_t jump_addr = 0x10;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xd8:		//RET C
	{
		if (registers.flag.c) {
			registers.pc = (_memory->read(registers.sp) | (_memory->read(registers.sp + 1) << 8));
			registers.sp += 2;
			return 5;
		}

		registers.pc += 1;
		return 2;
	}
	case 0xd9:		//RETI
	{
		registers.pc = (_memory->read(registers.sp) | (_memory->read(registers.sp + 1) << 8));
		registers.sp += 2;
		registers.IME = 1;
		return 4;
	}
	case 0xda:		//JP C a16
	{
		if (registers.flag.c) {
			uint16_t addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
			registers.pc = addr;
			return 4;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xdb:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xdb\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xdc:		//CALL C a16
	{
		if (registers.flag.c) {
			uint16_t jump_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
			_memory->write(registers.sp - 2, ret_addr & 0Xff);

			registers.sp -= 2;
			registers.pc = jump_addr;
			return 6;
		}
		registers.pc += 3;
		return 3;
	}
	case 0xdd:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xdd\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xde:		//SBC A, d8
	{
		uint8_t n = _memory->read(pc + 1);
		SBC_A_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xdf:		//RST 0x18
	{
		uint16_t jump_addr = 0x18;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xe0:		//LDH (n), A
	{
		uint16_t gb_addr = 0xff00 + _memory->read(pc + 1);
		_memory->write(gb_addr, registers.a);
		registers.pc += 2;
		return 3;
	}
	case 0xe1:			//POP HL
	{
		registers.l = _memory->read(registers.sp);
		registers.h = _memory->read(registers.sp + 1);
		registers.sp += 2;
		registers.pc += 1;
		return 3;
	}
	case 0xe2:		//LD (C), A
	{
		uint16_t gb_addr = 0xff00 + registers.c;
		_memory->write(gb_addr, registers.a);
		registers.pc += 1;
		return 2;
	}
	case 0xe3:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xe3\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xe4:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xe4\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xe5:		//PUSH HL
	{
		_memory->write(registers.sp - 1, registers.h);
		_memory->write(registers.sp - 2, registers.l);
		registers.sp -= 2;
		registers.pc += 1;
		return 4;
	}
	case 0xe6:		//AND d8
	{
		uint8_t n = _memory->read(pc + 1);
		AND_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xe7:		//RST 0x20
	{
		uint16_t jump_addr = 0x20;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xe8:		//ADD SP, r8
	{
		char n = _memory->read(pc + 1);
		registers.flag.h = ((registers.sp & 0xf) + (((uint8_t)n) & 0xf) > 0xf);
		registers.flag.c = ((registers.sp & 0xff) + ((uint8_t)n) > 0xff);
		registers.sp = (unsigned)((short)registers.sp + n);

		registers.flag.z = 0;
		registers.flag.n = 0;
		registers.pc += 2;
		return 4;
	}
	case 0xe9:		//JP HL
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.pc = hl;
		return 1;
	}
	case 0xea:		//LD (d16), A
	{
		uint16_t gb_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
		_memory->write(gb_addr, registers.a);
		registers.pc += 3;
		return 4;
	}
	case 0xeb:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xeb\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xec:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xec\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xed:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xed\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xee:		//XOR d8
	{
		uint8_t n = _memory->read(pc + 1);
		XOR_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xef:		//RST 28H
	{
		uint16_t jump_addr = 0x28;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xf0:		//LDH A, d8
	{
		uint16_t gb_addr = 0xff00 + _memory->read(pc + 1);
		registers.a = _memory->read(gb_addr);
		registers.pc += 2;
		return 3;
	}
	case 0xf1:		//POP AF
	{
		*((uint8_t*)&registers.flag) = _memory->read(registers.sp );
		registers.a = _memory->read(registers.sp + 1);

		registers.sp += 2;
		registers.pc += 1;
		return 3;
	}
	case 0xf2:		//LD A,(C)
	{
		uint16_t gb_addr = 0xff00 + registers.c;
		registers.a = _memory->read(gb_addr);
		registers.pc += 1;
		return 2;
	}
	case 0xf3:		//DI
	{
		registers.IME_U = 0;
		registers.IME_CC = 2;
		registers.pc += 1;
		return 1;
	}
	case 0xf4:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xf4\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xf5:		//PUSH AF
	{
		uint8_t flag = *((uint8_t*)&registers.flag);
		_memory->write(registers.sp - 1, registers.a);
		_memory->write(registers.sp - 2, flag);
		registers.sp -= 2;
		registers.pc += 1;
		return 4;
	}
	case 0xf6:		//OR d8
	{
		uint8_t n = _memory->read(pc + 1);
		OR_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xf7:		//RST 0x30
	{
		uint16_t jump_addr = 0x30;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	case 0xf8:		//LD HL, SP + r8
	{
		uint8_t n = _memory->read(pc + 1);
		uint16_t hl = registers.sp + n;

		registers.flag.z = 0;
		registers.flag.n = 0;
		registers.flag.h = (((registers.sp & 0xf) + (n & 0xf)) > 0xf);
		registers.flag.c = (((registers.sp & 0xff) + n) > 0xff);

		registers.h = (hl >> 8) & 0xff;
		registers.l = hl & 0xff;
		registers.pc += 2;
		return 3;
	}
	case 0xf9:		//LD SP, HL
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		registers.sp = hl;
		registers.pc += 1;
		return 2;
	}
	case 0xfa:		//LD A, (d16)
	{
		uint16_t gb_addr = (_memory->read(pc + 1) | (_memory->read(pc + 2) << 8));
		registers.a = _memory->read(gb_addr);
 		registers.pc += 3;
		return 4;
	}
	case 0xfb:		//EI
	{
		registers.IME_U = 1;
		registers.IME_CC = 2;
		registers.pc += 1;
		return 1;
	}
	case 0xfc:				//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xfc\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xfd:				//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xfd\nPC = " + std::to_string(registers.pc));
		break;
	}
	case 0xfe:		//CP, d8
	{
		uint8_t d = _memory->read(pc + 1);
		registers.flag.z = (registers.a == d);
		registers.flag.n = 1;
		registers.flag.h = ((registers.a & 0xf) < (d & 0xf));
		registers.flag.c = (registers.a < d);
		registers.pc += 2;
		return 2;
	}
	case 0xff:		//RST 0x38
	{
		uint16_t jump_addr = 0x38;

		uint16_t ret_addr = pc + 1;
		_memory->write(registers.sp - 1, (ret_addr >> 8) & 0xff);
		_memory->write(registers.sp - 2, ret_addr & 0Xff);

		registers.sp -= 2;
		registers.pc = jump_addr;
		return 4;
	}
	default:
 		std::cout << "\nInstruction not implemented: 0x" << std::hex << (int)opcode;
		fatal(FATAL_INSTRUCTION_NOT_IMPLEMENTED, __func__);
	}

	return 0;
}


int GameBoy::prefixed_execute() {
	uint16_t pc = registers.pc;
	uint8_t opcode = _memory->read(pc);

	switch (opcode) {
	case 0x00:		//RLC B
	{
		return RLC_n(registers.b);
	}
	case 0x01:		//RLC C
	{
		return RLC_n(registers.c);
	}
	case 0x02:		//RLC D
	{
		return RLC_n(registers.d);
	}
	case 0x03:		//RLC E
	{
		return RLC_n(registers.e);
	}
	case 0x04:		//RLC H
	{
		return RLC_n(registers.h);
	}
	case 0x05:		//RLC L
	{
		return RLC_n(registers.l);
	}
	case 0x06:		//RLC (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		RLC_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x07:		//RLC A
	{
		return RLC_n(registers.a);
	}
	case 0x08:		//RRC B
	{
		return RRC_n(registers.b);
	}
	case 0x09:		//RRC C
	{
		return RRC_n(registers.c);
	}
	case 0x0a:		//RRC D
	{
		return RRC_n(registers.d);
	}
	case 0x0b:		//RRC E
	{
		return RRC_n(registers.e);
	}
	case 0x0c:		//RRC H
	{
		return RRC_n(registers.h);
	}
	case 0x0d:		//RRC L
	{
		return RRC_n(registers.l);
	}
	case 0x0e:		//RRC (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		RRC_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0xf:		//RRC A
	{
		return RRC_n(registers.a);
	}
	case 0x10:		//RL B
	{
		return RL_n(registers.b);
	}
	case 0x11:		//RL C
	{
		return RL_n(registers.c);
	}
	case 0x12:		//RL D
	{
		return RL_n(registers.d);
	}
	case 0x13:		//RL E
	{
		return RL_n(registers.e);
	}
	case 0x14:		//RL H
	{
		return RL_n(registers.h);
	}
	case 0x15:		//RL L
	{
		return RL_n(registers.l);
	}
	case 0x16:		//RL (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		RL_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x17:		//RL A
	{
		return RL_n(registers.a);
	}
	case 0x18:		//RR B
	{
		return RR_n(registers.b);
	}
	case 0x19:		//RR C
	{
		return RR_n(registers.c);
	}
	case 0x1a:		//RR D
	{
		return RR_n(registers.d);
	}
	case 0x1b:		//RR E
	{
		return RR_n(registers.e);
	}
	case 0x1c:		//RR H
	{
		return RR_n(registers.h);
	}
	case 0x1d:		//RR L
	{
		return RR_n(registers.l);
	}
	case 0x1e:		//RR (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		RR_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x1f:		//RR A
	{
		return RR_n(registers.a);
	}
	
	case 0x20:		//SLA B
	{
		return SLA_n(registers.b);
	}
	case 0x21:		//SLA C
	{
		return SLA_n(registers.c);
	}
	case 0x22:		//SLA D
	{
		return SLA_n(registers.d);
	}
	case 0x23:		//SLA E
	{
		return SLA_n(registers.e);
	}
	case 0x24:		//SLA H
	{
		return SLA_n(registers.h);
	}
	case 0x25:		//SLA L
	{
		return SLA_n(registers.l);
	}
	case 0x26:		//SLA (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		SLA_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x27:		//SLA A
	{
		return SLA_n(registers.a);
	}
	case 0x28:		//SRA B
	{
		return SRA_n(registers.b);
	}
	case 0x29:		//SRA C
	{
		return SRA_n(registers.c);
	}
	case 0x2a:		//SRA D
	{
		return SRA_n(registers.d);
	}
	case 0x2b:		//SRA E
	{
		return SRA_n(registers.e);
	}
	case 0x2c:		//SRA H
	{
		return SRA_n(registers.h);
	}
	case 0x2d:		//SRA L
	{
		return SRA_n(registers.l);
	}
	case 0x2e:		//SRA (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		SRA_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x2f:		//SRA A
	{
		return SRA_n(registers.a);
	}
	case 0x30:		//SWAP B
	{
		return SWAP_n(registers.b);
	}
	case 0x31:		//SWAP C
	{
		return SWAP_n(registers.c);
	}
	case 0x32:		//SWAP D
	{
		return SWAP_n(registers.d);
	}
	case 0x33:		//SWAP E
	{
		return SWAP_n(registers.e);
	}
	case 0x34:		//SWAP H
	{
		return SWAP_n(registers.h);
	}
	case 0x35:		//SWAP L
	{
		return SWAP_n(registers.l);
	}
	case 0x36:		//SWAP (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		SWAP_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x37:		//SWAP A
	{
		return SWAP_n(registers.a);
	}
	case 0x38:		//SRL B
	{
		return SRL_n(registers.b);
	}
	case 0x39:		//SRL C
	{
		return SRL_n(registers.c);
	}
	case 0x3a:		//SRL D
	{
		return SRL_n(registers.d);
	}
	case 0x3b:		//SRL E
	{
		return SRL_n(registers.e);
	}
	case 0x3c:		//SRL H
	{
		return SRL_n(registers.h);
	}
	case 0x3d:		//SRL L
	{
		return SRL_n(registers.l);
	}
	case 0x3e:		//SRL (HL)
	{
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		SRL_n(n);
		_memory->write(hl, n);
		return 4;
	}
	case 0x3f:		//SRL A
	{
		return SRL_n(registers.a);
	}

	//BIT b, B	
	case 0x40:case 0x48:case 0x50:case 0x58:case 0x60:case 0x68:case 0x70:case 0x78:
	{
		uint8_t bit = (opcode - 0x40) / 8;
		registers.flag.z = ((registers.b & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//BIT b, C
	case 0x41:case 0x49:case 0x51:case 0x59:case 0x61:case 0x69:case 0x71:case 0x79:
	{
		uint8_t bit = (opcode - 0x41) / 8;
		registers.flag.z = ((registers.c & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//BIT b, D
	case 0x42:case 0x4a:case 0x52:case 0x5a:case 0x62:case 0x6a:case 0x72:case 0x7a:
	{
		uint8_t bit = (opcode - 0x42) / 8;
		registers.flag.z = ((registers.d & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//BIT b, E
	case 0x43:case 0x4b:case 0x53:case 0x5b:case 0x63:case 0x6b:case 0x73:case 0x7b:
	{
		uint8_t bit = (opcode - 0x43) / 8;
		registers.flag.z = ((registers.e & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//BIT b, H
	case 0x44:case 0x4c:case 0x54:case 0x5c:case 0x64:case 0x6c:case 0x74:case 0x7c:
	{
		uint8_t bit = (opcode - 0x44) / 8;
		registers.flag.z = ((registers.h & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//BIT b, L
	case 0x45:case 0x4d:case 0x55:case 0x5d:case 0x65:case 0x6d:case 0x75:case 0x7d:
	{
		uint8_t bit = (opcode - 0x45) / 8;
		registers.flag.z = ((registers.l & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//BIT b, (HL)
	case 0x46:case 0x4e:case 0x56:case 0x5e:case 0x66:case 0x6e:case 0x76:case 0x7e:
	{
		uint8_t bit = (opcode - 0x46) / 8;
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		registers.flag.z = ((n & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 3;
	}
	//BIT b, A
	case 0x47:case 0x4f:case 0x57:case 0x5f:case 0x67:case 0x6f:case 0x77:case 0x7f:
	{
		uint8_t bit = (opcode - 0x47) / 8;
		registers.flag.z = ((registers.a & (0x1 << bit)) == 0);
		registers.flag.n = 0;
		registers.flag.h = 1;
		registers.pc += 1;
		return 2;
	}
	//RES b, B
	case 0x80:case 0x88:case 0x90:case 0x98:case 0xa0:case 0xa8:case 0xb0:case 0xb8:
	{
		uint8_t bit = (opcode - 0x80) / 8;
		registers.b &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//RES b, C
	case 0x81:case 0x89:case 0x91:case 0x99:case 0xa1:case 0xa9:case 0xb1:case 0xb9:
	{
		uint8_t bit = (opcode - 0x81) / 8;
		registers.c &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//RES b, D
	case 0x82:case 0x8a:case 0x92:case 0x9a:case 0xa2:case 0xaa:case 0xb2:case 0xba:
	{
		uint8_t bit = (opcode - 0x82) / 8;
		registers.d &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//RES b, E
	case 0x83:case 0x8b:case 0x93:case 0x9b:case 0xa3:case 0xab:case 0xb3:case 0xbb:
	{
		uint8_t bit = (opcode - 0x83) / 8;
		registers.e &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//RES b, H
	case 0x84:case 0x8c:case 0x94:case 0x9c:case 0xa4:case 0xac:case 0xb4:case 0xbc:
	{
		uint8_t bit = (opcode - 0x84) / 8;
		registers.h &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//RES b, L
	case 0x85:case 0x8d:case 0x95:case 0x9d:case 0xa5:case 0xad:case 0xb5:case 0xbd:
	{
		uint8_t bit = (opcode - 0x85) / 8;
		registers.l &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//RES b, (HL)
	case 0x86:case 0x8e:case 0x96:case 0x9e:case 0xa6:case 0xae:case 0xb6:case 0xbe:
	{
		uint8_t bit = (opcode - 0x86) / 8;
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t n = _memory->read(hl);
		n &= ~(0x1 << bit);
		_memory->write(hl, n);
		registers.pc += 1;
		return 4;
	}
	//RES b, A
	case 0x87:case 0x8f:case 0x97:case 0x9f:case 0xa7:case 0xaf:case 0xb7:case 0xbf:
	{
		uint8_t bit = (opcode - 0x87) / 8;
		registers.a &= ~(0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, B
	case 0xc0:case 0xc8:case 0xd0:case 0xd8:case 0xe0:case 0xe8:case 0xf0:case 0xf8:
	{
		uint8_t bit = (opcode - 0xc0) / 8;
		registers.b |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, C
	case 0xc1:case 0xc9:case 0xd1:case 0xd9:case 0xe1:case 0xe9:case 0xf1:case 0xf9:
	{
		uint8_t bit = (opcode - 0xc1) / 8;
		registers.c |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, D
	case 0xc2:case 0xca:case 0xd2:case 0xda:case 0xe2:case 0xea:case 0xf2:case 0xfa:
	{
		uint8_t bit = (opcode - 0xc2) / 8;
		registers.d |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, E
	case 0xc3:case 0xcb:case 0xd3:case 0xdb:case 0xe3:case 0xeb:case 0xf3:case 0xfb:
	{
		uint8_t bit = (opcode - 0xc3) / 8;
		registers.e |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, H
	case 0xc4:case 0xcc:case 0xd4:case 0xdc:case 0xe4:case 0xec:case 0xf4:case 0xfc:
	{
		uint8_t bit = (opcode - 0xc4) / 8;
		registers.h |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, L
	case 0xc5:case 0xcd:case 0xd5:case 0xdd:case 0xe5:case 0xed:case 0xf5:case 0xfd:
	{
		uint8_t bit = (opcode - 0xc5) / 8;
		registers.l |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	//SET b, (HL)
	case 0xc6:case 0xce:case 0xd6:case 0xde:case 0xe6:case 0xee:case 0xf6:case 0xfe:
	{
		uint8_t bit = (opcode - 0xc6) / 8;
		uint16_t hl = (registers.h << 8) | registers.l;
		uint8_t d = _memory->read(hl);
		d |= (0x1 << bit);
		_memory->write(hl, d);
		registers.pc += 1;
		return 4;
	}
	//SET b, A
	case 0xc7:case 0xcf:case 0xd7:case 0xdf:case 0xe7:case 0xef:case 0xf7:case 0xff:
	{
		uint8_t bit = (opcode - 0xc7) / 8;
		registers.a |= (0x1 << bit);
		registers.pc += 1;
		return 2;
	}
	default:
		std::cout << "\nInstruction not implemented (0xCB xx): 0x" << std::hex << (int)opcode;
		fatal(FATAL_INSTRUCTION_NOT_IMPLEMENTED, __func__);
	}

	return 0;
}

int GameBoy::SWAP_n(uint8_t& reg) {
	reg = ((reg << 4) & 0xf0) | ((reg >> 4) & 0xf);
	registers.flag.z = (reg == 0);
	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.c = 0;
	registers.pc += 1;
	return 2;
}

int GameBoy::SLA_n(uint8_t& reg) {
	registers.flag.c = ((reg & 0x80) != 0);
	reg <<= 1;
	registers.flag.z = (reg == 0);
	registers.flag.n = 0;
	registers.flag.h = 0;

	registers.pc += 1;
	return 2;
}

int GameBoy::SRA_n(uint8_t& reg) {
	registers.flag.c = (reg & 0x1);
	reg = ((reg & 0x80) | (reg >> 1));
	registers.flag.z = (reg == 0);
	registers.flag.n = 0;
	registers.flag.h = 0;

	registers.pc += 1;
	return 2;
}

int GameBoy::RL_n(uint8_t& reg) {
	uint8_t bit0 = registers.flag.c;
	registers.flag.c = ((reg & 0x80) != 0);
	reg <<= 1;
	reg |= bit0;
	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.z = (reg == 0);

	registers.pc += 1;
	return 2;
}

int GameBoy::RR_n(uint8_t& reg) {
	uint8_t bit7 = registers.flag.c;
	registers.flag.c = ((reg & 0x1) != 0);
	reg >>= 1;
	reg |= (bit7 << 7);
	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.z = (reg == 0);

	registers.pc += 1;
	return 2;
}


int GameBoy::RLC_n(uint8_t& reg) {
	registers.flag.c = (reg >> 7) & 0x1;
	reg <<= 1;
	reg |= registers.flag.c;
	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.z = (reg == 0);
	registers.pc += 1;
	return 2;
}

int GameBoy::RRC_n(uint8_t& reg) {
	registers.flag.c = (reg & 0x1);
	reg >>= 1;
	reg |= (registers.flag.c << 7);

	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.z = (reg == 0);
	registers.pc += 1;
	return 2;
}

int GameBoy::LD_r1_r2(uint8_t& reg1, uint8_t& reg2) {
	reg1 = reg2;
	registers.pc += 1;
	return 1;
}

int GameBoy::SBC_A_n(uint8_t& reg) {
	uint16_t n = reg + registers.flag.c;
	
	//reg + carry-flag operation
	registers.flag.h = (((reg & 0xf) + registers.flag.c) > 0xf);
	registers.flag.c = (n > 0xff);
	//subtruction operation
	registers.flag.h |= ((registers.a & 0xf) < (n & 0xf));
	registers.flag.c |= (registers.a < (n & 0xff));

	registers.a -= (n & 0xff);
	registers.flag.z = (registers.a == 0);
	registers.flag.n = 1;

	registers.pc += 1;
	return 1;
}

int GameBoy::ADC_A_n(uint8_t& reg) {
	uint16_t sum = registers.a + reg + registers.flag.c;

	registers.flag.h = (((registers.a & 0xf) + (reg & 0xf) + registers.flag.c) > 0xf);

	registers.a += (reg + registers.flag.c);

	registers.flag.z = (registers.a == 0);
	registers.flag.n = 0;
	registers.flag.c = (sum > 0xff);

	registers.pc += 1;
	return 1;
}

int GameBoy::OR_n(uint8_t& reg) {
	registers.a |= reg;

	registers.flag.z = (registers.a == 0);
	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.c = 0;

	registers.pc += 1;
	return 1;
}

int GameBoy::AND_n(uint8_t& reg) {
	registers.a &= reg;

	registers.flag.z = (registers.a == 0);
	registers.flag.n = 0;
	registers.flag.h = 1;
	registers.flag.c = 0;

	registers.pc += 1;
	return 1;
}

int GameBoy::XOR_n(uint8_t& reg) {
	registers.a ^= reg;

	registers.flag.z = (registers.a == 0);
	registers.flag.n = 0;
	registers.flag.h = 0;
	registers.flag.c = 0;

	registers.pc += 1;
	return 1;
}

int GameBoy::CP_n(uint8_t& reg) {
	registers.flag.z = (registers.a == reg);
	registers.flag.n = 1;
	registers.flag.h = ((registers.a & 0xf) < (reg & 0xf));
	registers.flag.c = (registers.a < reg);

	registers.pc += 1;
	return 1;
}

int GameBoy::SRL_n(uint8_t& reg) {
	registers.flag.c = ((reg & 0x1) != 0);
	reg >>= 1;
	reg &= 0x7f;
	registers.flag.z = (reg == 0);
	registers.flag.n = 0;
	registers.flag.h = 0;

	registers.pc += 1;
	return 2;
}

int GameBoy::SUB_n(uint8_t& reg) {
	registers.flag.h = ((registers.a & 0xf) < (reg & 0xf));
	registers.flag.c = (registers.a < reg);

	registers.a -= reg;
	registers.flag.z = (registers.a == 0);
	registers.flag.n = 1;

	registers.pc += 1;
	return 1;
}

int GameBoy::ADD_n(uint8_t& reg) {
	uint16_t sum = registers.a + reg;

	registers.flag.h = (((registers.a & 0xf) + (reg & 0xf)) > 0xf);
	registers.a += reg;
	registers.flag.z = (registers.a == 0);
	registers.flag.n = 0;
	registers.flag.c = (sum > 0xff);

	registers.pc += 1;
	return 1;
}

