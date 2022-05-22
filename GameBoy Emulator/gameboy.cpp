#include "gameboy.h"
#include "structures.h"
#include "errors.h"
#include "cartridge.h"
#include "renderer.h"
#include "sound.h"
#include "input.h"
#include "globals.h"

#include <iostream>
#include <fstream>
#include <cstdint>
#include <string.h>
#include <thread>
#include <chrono>


GameBoy::GameBoy(){

}

bool GameBoy::Init(const char* rom_filename) {
	
	this->vram = (uint8_t*)(this->gb_mem + 0x8000);
	this->wram = (uint8_t*)(this->gb_mem + 0xc000);
	this->io_map = (IO_map*)(this->gb_mem + 0xff00);
	this->oam = this->gb_mem + 0xfe00;

	videoMode = 0;
	time_clock = 0;
	joypadStatus = {};
	_saveState = false;

	std::ifstream bootrom_file("bootrom.bin", std::ios::in | std::ios::binary | std::ios::ate);

	if (!bootrom_file.is_open())
		fatal(FATAL_BOOT_ROM_NOT_FOUND, __func__);

	std::streampos size = bootrom_file.tellg();
	if (size != 256)
		fatal(FATAL_INVALID_BOOT_ROM_SIZE, __func__);

	bootrom_file.seekg(0, std::ios::beg);
	bootrom_file.read((char*)this->boot_rom, 256);
	bootrom_file.close();

	//init mem
	memset(&this->registers, 0, sizeof(this->registers));
	this->registers.pc = 0;
	memset(this->gb_mem, 0, sizeof(this->gb_mem));
	memset(frameStat, 0, sizeof(frameStat));

	//init joypad stuff
	this->registers.joyp_stat = 1;
	io_map->JOYP = 0xff;

	this->cart = new Cartridge(rom_filename);
	this->sound = new Sound();

	return true;
}

void GameBoy::_internal_saveState() {
	this->saveMutex.lock();
	if (this->_saveState) {
		cart->saveState();
		_saveState = false;
	}
	this->saveMutex.unlock();
}

void GameBoy::saveState() {
	this->saveMutex.lock();
	_saveState = true;
	this->saveMutex.unlock();
}

void GameBoy::screenUpdate(int clocks) {
	uint8_t stat_s = 0;

	if ((io_map->LCDC & 0x80) == 0) { 	//the lcd is off
		this->registers.sl_cnt = 0;
		io_map->LY = 0;
		return;
	}

	if ((io_map->LCDC & 0x20) > 0) {
		io_map->LCDC = io_map->LCDC;
	}

	STAT_struct* stat = ((STAT_struct*)(&io_map->STAT));

	registers.sl_cnt += clocks;
	if (registers.sl_cnt >= 456) {
		registers.sl_cnt -= 456;
		io_map->LY++;
		
		if (io_map->LY == 144) {		//VBlank
			io_map->IF |= 0x1;
		}
	}
	
	if (io_map->LY >= 144) {		//Mode 1: (Vblank)
		
		stat->lcd_mode = 1;
		if (io_map->LY >= 154) {
			io_map->LY = 0;
		}
	}
	else {
		if (registers.sl_cnt <= 204) {
			stat->lcd_mode = 0;		//MODE 0 (HBlank) - HBlank occurs between lines and allows a short period of time for adjusting PPU parameters, VRAM, and OAM.
		}
		else if (registers.sl_cnt <= 284) {
			stat->lcd_mode = 2;		//MODE 2 - scan OAM to find which OBJs are active. During this time OAM is locked.
		}
		else {

			if (registers.sl_cnt > 400) {
				//save the lcd stat for each scanline
				frameStat[io_map->LY].lcdc = io_map->LCDC;
				frameStat[io_map->LY].SCX = io_map->SCX;
				frameStat[io_map->LY].SCY = io_map->SCY;
				frameStat[io_map->LY].WX = io_map->WX;
				frameStat[io_map->LY].WY = io_map->WY;
				
				//update the renderer at the end of the frame
				if (io_map->LY == 143) {
					_renderer->UpdateGraphics(frameStat);
				}
			}
			stat->lcd_mode = 3;		//MODE 3 (HDraw) - drawing the pixels
		}
	}

	//set LY coincidence flag
	if (io_map->LY == io_map->LYC) {
		stat->ly_cf = 1;
	}
	else stat->ly_cf = 0;

	//STAT LY coincidence
	if (stat->ly_c && stat->ly_cf){	
		stat_s = 1;
	}

	//stat interrupt signal for modes
	if (stat->lcd_mode == 0 && stat->mode0_int) {
		stat_s = 1;
	}
	if (stat->lcd_mode == 1 && stat->mode1_int) {
		stat_s = 1;
	}
	if (stat->lcd_mode == 2 && stat->mode2_int) {
		stat_s = 1;
	}

	//stat interrupt
	if (this->registers.stat_signal == 0 && stat_s == 1) {
		this->io_map->IF |= 0x2;
	}
	this->registers.stat_signal = stat_s;

}

uint8_t* GameBoy::getVram(void) {
	return this->vram;
}

IO_map* GameBoy::getIOMap(void){
	return this->io_map;
}

uint8_t* GameBoy::getOam() {
	return this->oam;
}

int GameBoy::nextInstruction() {

	int cycles = 0;
	
	if (!registers.stopped) {
		cycles = handleInterrupt();
	}

	if (!registers.halted && !registers.stopped) {
		cycles += this->execute();

		//update divider register at a rate of 16384Hz 
		registers.div_cnt += cycles * 4;
		if (registers.div_cnt >= 256) {
			registers.div_cnt -= 256;
			io_map->DIV++;
		}
	}
	else cycles += 1;		//lcd and the timer still needs the clock to work in halt mode

	handleJoypad();
	if (!registers.stopped) {
		handleSerial();
		handleTimer(cycles);
		screenUpdate(cycles * 4);
		sound->UpdateSound(this->io_map);
	}

	//limits gameboy speed
	time_clock += cycles * 4;
	if (time_clock >= 4200) {
		time_clock = 0;
		joypadStatus = _input->getJoypadState();		//get joypad state every ~1ms
		_internal_saveState();

		auto endTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> elapsed = endTime - realTimePoint;
		double elapsedTime = elapsed.count();	//elapsed time in seconds
		realTimePoint = endTime;
		std::this_thread::sleep_for(std::chrono::microseconds((long long)((0.001 - elapsedTime) * 100000)));
	}
	
	return cycles;
}


void GameBoy::handleTimer(int cycles) {

	if (!(io_map->TAC & 0x4))
		return;

	this->registers.timer_clk += cycles * 4;
	int div_flag = (io_map->TAC & 0x3);
	int divider = (div_flag == 0) ? 1024 : (4 << (div_flag*2));
	if (this->registers.timer_clk >= divider) {
		this->registers.timer_clk -= divider;
		io_map->TIMA++;
		if (io_map->TIMA == 0) {	//overflow
			io_map->TIMA = io_map->TMA;		//reload timer
			io_map->IF |= 0x4;		//timer IRQ asserted
		}
	}
	
}

void GameBoy::handleJoypad(void) {

	if (registers.stopped) {
		if (joypadStatus.a || joypadStatus.b || joypadStatus.select || joypadStatus.start ||
			joypadStatus.right || joypadStatus.left || joypadStatus.up || joypadStatus.down) {
			registers.stopped = false;
			_renderer->turnOn();
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
	if (this->registers.joyp_stat && !((io_map->JOYP & 0xf) == 0xf)) {
		io_map->IF |= 0x10;
	}
	this->registers.joyp_stat = ((io_map->JOYP&0xf) == 0xf);		//logical and of all bits of joypad buttons
}

int GameBoy::handleInterrupt(void) {

	if (this->registers.IME_CC > 0) {
		if (--this->registers.IME_CC == 0) {
			this->registers.IME = this->registers.IME_U;
		}
	}

	if (this->registers.IME && (this->io_map->IF & this->io_map->IE)) {

		for (int i = 0; i < 5; i++) {
			if ((this->io_map->IF & (0x1 << i)) && (this->io_map->IE & (0x1 << i))) {
				this->registers.halted = 0;

				uint16_t interrupt_vect_addr = 0x40 + 0x8 * i;
				//PUSH PC
				write(this->registers.sp - 1, (this->registers.pc>>8) & 0xff);
				write(this->registers.sp - 2, this->registers.pc & 0xff);
				this->registers.sp -= 2;

				this->registers.IME = 0;		//disable interrupt
				this->io_map->IF &= ~(0x1 << i);	//clear interrupt flag
				this->registers.pc = interrupt_vect_addr;		//jump to the corrisponding interrupt vector
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

	uint16_t pc = this->registers.pc;
	/*if (pc == 0x1d9f) {
		pc = pc;
	}
	if (pc == 0x1e7e) {
		pc = pc;
	}*/
	uint8_t opcode = read(pc);

	switch (opcode) {
	case 0x0:		//NOP
	{
		this->registers.pc += 1;
		return 1;
	}
	case 0x1:		//LD BD, d16
	{
		this->registers.c = read(pc + 1);
		this->registers.b = read(pc + 2);
		this->registers.pc += 3;
		return 3;
	}
	case 0x02:		//LD (BC), A
	{
		uint16_t bc = (this->registers.b << 8) | this->registers.c;
		write(bc, this->registers.a);
		this->registers.pc += 1;
		return 2;
	}
	case 0x3:		//INC BC
	{
		uint16_t bc = (this->registers.b << 8) | this->registers.c;
		bc++;
		this->registers.b = (bc >> 8) & 0xff;
		this->registers.c = bc & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x4:		//INC B
	{
		this->registers.flag.h = ((this->registers.b & 0xf) == 0xf);

		this->registers.b++;
		this->registers.flag.z = (this->registers.b == 0);
		this->registers.flag.n = 0;

		this->registers.pc += 1;
		return 1;
	}
	case 0x5:		//DEC B
	{
		this->registers.flag.h = ((this->registers.b & 0xf) == 0);	//needs a half carry?

		this->registers.b--;
		this->registers.flag.z = (this->registers.b == 0);
		this->registers.flag.n = 1;
		
		this->registers.pc += 1;
		return 1;
	}
	case 0x6:		//LD B, d8
	{
		this->registers.b = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0x7:		//RLCA
	{
		this->registers.flag.c = ((this->registers.a & 0x80) != 0);
		this->registers.a <<= 1;
		this->registers.a |= this->registers.flag.c;
		this->registers.flag.n = 0;
		this->registers.flag.h = 0;
		this->registers.flag.z = (this->registers.a == 0);

		this->registers.pc += 1;
		return 1;
	}
	case 0x08:		//LD (a16), SP
	{
		uint16_t gb_addr = (read(pc + 1) | read(pc + 2) << 8);
		write(gb_addr, this->registers.sp & 0xff);
		write(gb_addr + 1, (this->registers.sp >> 8) & 0xff);
		this->registers.pc += 3;
		return 5;
	}
	case 0x9:		//ADD HL, BC
	{
		uint16_t bc = (this->registers.b << 8) | this->registers.c;
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint32_t sum = hl + bc;

		this->registers.flag.h = (((hl & 0xfff) + (bc & 0xfff)) > 0xfff);

		hl += bc;

		this->registers.flag.n = 0;
		this->registers.flag.c = (sum > 0xffff);

		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0xa:		//LD A, (BC)
	{
		uint16_t bc = (this->registers.b << 8) | this->registers.c;
		this->registers.a = read(bc);
		this->registers.pc += 1;
		return 2;
	}
	case 0xb:		//DEC BC
	{
		uint16_t bc = (this->registers.b << 8) | this->registers.c;
		bc--;
		this->registers.b = (bc >> 8) & 0xff;
		this->registers.c = bc & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0xc:		//INC C
	{
		this->registers.flag.h = ((this->registers.c & 0xf) == 0xf);

		this->registers.c++;
		this->registers.flag.z = (this->registers.c == 0);
		this->registers.flag.n = 0;
		
		this->registers.pc += 1;
		return 1;
	}
	case 0xd:		//DEC C
	{
		this->registers.flag.h = ((this->registers.c & 0xf) == 0);	//needs a half carry?

		this->registers.c--;
		this->registers.flag.z = (this->registers.c == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 1;
		return 1;
	}
	case 0xe:	//LD C, d8
	{
		this->registers.c = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0xf:		//RRCA
	{
		this->registers.flag.c = ((this->registers.a & 0x1) != 0);
		this->registers.a >>= 1;
		this->registers.a |= (this->registers.flag.c << 7);
		this->registers.flag.n = 0;
		this->registers.flag.h = 0;
		this->registers.flag.z = (this->registers.a == 0);
		
		this->registers.pc += 1;
		return 1;
	}
	case 0x10:		//STOP d8
	{
		registers.stopped = true;
		sound->Halt();
		_renderer->turnOff();
		registers.pc += 2;
		return 1;
	}
	case 0x11:		//LD DE, d16
	{
		this->registers.e = read(pc + 1);
		this->registers.d = read(pc + 2);
		this->registers.pc += 3;
		return 3;
	}
	case 0x12:		//LD (DE), A
	{
		uint16_t de = (this->registers.d << 8) | this->registers.e;
		write(de, this->registers.a);
		this->registers.pc += 1;
		return 2;
	}
	case 0x13:		//INC DE
	{
		uint16_t de = (this->registers.d << 8) | this->registers.e;
		de++;
		this->registers.d = (de >> 8) & 0xff;
		this->registers.e = de & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x14:		//INC D
	{
		this->registers.flag.h = ((this->registers.d & 0xf) == 0xf);

		this->registers.d++;
		this->registers.flag.z = (this->registers.d == 0);
		this->registers.flag.n = 0;

		this->registers.pc += 1;
		return 1;
	}
	case 0x15:		//DEC D
	{
		this->registers.flag.h = ((this->registers.d & 0xf) == 0);	//needs a half carry?

		this->registers.d--;
		this->registers.flag.z = (this->registers.d == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 1;
		return 1;
	}
	case 0x16:		//LD D, d8
	{
		this->registers.d = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0x17:		//RLA
	{
		uint8_t bit0 = this->registers.flag.c;
		this->registers.flag.c = ((this->registers.a & 0x80) != 0);
		this->registers.a <<= 1;
		this->registers.a |= bit0;
		this->registers.flag.n = 0;
		this->registers.flag.h = 0;
		this->registers.flag.z = (this->registers.a == 0);

		this->registers.pc += 1;
		return 1;
	}
	case 0x18:		//JR d8
	{
		char jump = read(pc + 1);
		this->registers.pc = (unsigned)((short)this->registers.pc + jump);
		this->registers.pc += 2;
		return 2;
	}
	case 0x19:		//ADD HL, DE
	{
		uint16_t de = (this->registers.d << 8) | this->registers.e;
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint32_t sum = hl + de;

		this->registers.flag.h = (((hl & 0xfff) + (de & 0xfff)) > 0xfff);

		hl += de;

		this->registers.flag.n = 0;
		this->registers.flag.c = (sum > 0xffff);

		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x1a:		//LD A, (DE)
	{
		uint16_t de = (this->registers.d << 8) | this->registers.e;
		this->registers.a = read(de);
		this->registers.pc += 1;
		return 2;
	}
	case 0x1b:		//DEC DE
	{
		uint16_t de = (this->registers.d << 8) | this->registers.e;
		de--;
		this->registers.d = (de >> 8) & 0xff;
		this->registers.e = de & 0xff;

		this->registers.pc += 1;
		return 2;
	}
	case 0x1c:		//INC E
	{
		this->registers.flag.h = ((this->registers.e & 0xf) == 0xf);

		this->registers.e++;
		this->registers.flag.z = (this->registers.e == 0);
		this->registers.flag.n = 0;

		this->registers.pc += 1;
		return 1;
	}
	case 0x1d:		//DEC E
	{
		this->registers.flag.h = ((this->registers.e & 0xf) == 0);	//needs a half carry?

		this->registers.e--;
		this->registers.flag.z = (this->registers.e == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 1;
		return 1;
	}
	case 0x1e:		//LD E, d8
	{
		this->registers.e = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0x1f:		//RRA
	{
		uint8_t bit7 = this->registers.flag.c;
		this->registers.flag.c = ((this->registers.a & 0x1) != 0);
		this->registers.a >>= 1;
		this->registers.a |= (bit7 << 7);
		this->registers.flag.n = 0;
		this->registers.flag.h = 0;
		this->registers.flag.z = (this->registers.a == 0);

		this->registers.pc += 1;
		return 1;
	}
	case 0x20:		//JR NZ, d8
	{
		char jump = read(pc + 1);
		if (!this->registers.flag.z) {
			this->registers.pc = (unsigned)((short)this->registers.pc + jump);
		}
		this->registers.pc += 2;
		return 2;
	}

	case 0x21:		//LD HL, d16
	{
		this->registers.l = read(pc + 1);
		this->registers.h = read(pc + 2);
		this->registers.pc += 3;
		return 3;
	}
	case 0x22:		//LD (HL+), A
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.a);
		hl++;
		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x23:		//INC HL
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		hl++;
		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x24:		//INC H
	{
		this->registers.flag.h = ((this->registers.h & 0xf) == 0xf);

		this->registers.h++;
		this->registers.flag.z = (this->registers.h == 0);
		this->registers.flag.n = 0;

		this->registers.pc += 1;
		return 1;
	}
	case 0x25:		//DEC H
	{
		this->registers.flag.h = ((this->registers.h & 0xf) == 0);	//needs a half carry?

		this->registers.h--;
		this->registers.flag.z = (this->registers.h == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 1;
		return 1;
	}
	case 0x26:		//LD H, d8
	{
		this->registers.h = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0x27:		//DAA
	{
		uint8_t &a = this->registers.a;
		uint8_t cf = this->registers.flag.c;

		if (this->registers.flag.n) {		//subtraction in last math instruction
			//4 lower nibbles greater than 9
			if (((a & 0xf) > 9) || this->registers.flag.h) {
				this->registers.flag.c |= (a < 6);
				a -= 6;
			}
			//4 upper nibbles greater than 9
			if ((a > 0x9f) || cf) {
				a -= 0x60;
				this->registers.flag.c = 1;
			}
			else {
				this->registers.flag.c = 0;
			}
		}
		else {
			//4 lower nibbles greater than 9
			if (((a & 0xf) > 9) || this->registers.flag.h) {
				this->registers.flag.c |= (((uint16_t)a + 6) > 0xff);
				a += 6;
			}

			//4 upper nibbles greater than 9
			if ((a > 0x9f) || cf) {
				this->registers.flag.c = 1;
				a += 0x60;
			}
			else {
				this->registers.flag.c = 0;
			}
		}

		this->registers.flag.z = (this->registers.a == 0);
		this->registers.flag.h = 0;
		this->registers.pc += 1;
		return 1;
	}
	case 0x28:		//JR Z, d8
	{
		char jump = read(pc + 1);
		if (this->registers.flag.z) {
			this->registers.pc = (unsigned)((short)this->registers.pc + jump);
		}
		this->registers.pc += 2;
		return 2;
	}
	case 0x29:		//ADD HL, HL
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint32_t sum = hl + hl;

		this->registers.flag.h = (((hl & 0xfff) + (hl & 0xfff)) > 0xfff);
		hl += hl;
		this->registers.flag.n = 0;
		this->registers.flag.c = (sum > 0xffff);

		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x2a:		//LD A, (HL+)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.a = read(hl);
		hl++;
		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x2b:		//DEC HL
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		hl--;
		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x2c:		//INC L
	{
		this->registers.flag.h = ((this->registers.l & 0xf) == 0xf);

		this->registers.l++;
		this->registers.flag.z = (this->registers.l == 0);
		this->registers.flag.n = 0;

		this->registers.pc += 1;
		return 1;
	}
	case 0x2d:		//DEC L
	{
		this->registers.flag.h = ((this->registers.l & 0xf) == 0);	//needs a half carry?

		this->registers.l--;
		this->registers.flag.z = (this->registers.l == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 1;
		return 1;
	}
	case 0x2e:		//LD L, d8
	{
		this->registers.l = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0x2f:		//CPL
	{
		this->registers.a = ~this->registers.a;
		this->registers.flag.n = 1;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 1;
	}
	case 0x30:		//JR NC d8
	{
		char jump = read(pc + 1);
		if (!this->registers.flag.c) {
			this->registers.pc = (unsigned)((short)this->registers.pc + jump);
			this->registers.pc += 2;
			return 3;
		}
		this->registers.pc += 2;
		return 2;
	}
	case 0x31:		//LD SP, d16
	{
		this->registers.sp = (read(pc + 1) | read(pc + 2) << 8);
		this->registers.pc += 3;
		return 3;
	}
	case 0x32:		//LD (HL-), A
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.a);
		hl--;
		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x33:		//INC SP
	{
		this->registers.sp++;
		this->registers.pc += 1;
		return 2;
	}
	case 0x34:		//INC (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);

		this->registers.flag.h = ((n & 0xf) == 0xf);
		n++;
		this->registers.flag.z = (n == 0);
		this->registers.flag.n = 0;
		write(hl, n);

		this->registers.pc += 1;
		return 3;
	}
	case 0x35:		//DEC (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);

		this->registers.flag.h = ((n & 0xf) == 0);	//needs a half carry?
		n--;
		this->registers.flag.z = (n == 0);
		this->registers.flag.n = 1;
		write(hl, n);

		this->registers.pc += 1;
		return 3;
	}
	case 0x36:		//LD (HL), d8
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(pc + 1);
		write(hl, n);

		this->registers.pc += 2;
		return 3;
	}
	case 0x37:		//SCF
	{
		this->registers.flag.c = 1;
		this->registers.flag.n = 0;
		this->registers.flag.h = 0;
		this->registers.pc += 1;
		return 1;
	}
	case 0x38:		//JR C d8
	{
		char jump = read(pc + 1);
		if (this->registers.flag.c) {
			this->registers.pc = (unsigned)((short)this->registers.pc + jump);
			this->registers.pc += 2;
			return 3;
		}
		this->registers.pc += 2;
		return 2;
	}
	case 0x39:		//ADD HL, SP
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint32_t sum = hl + this->registers.sp;

		this->registers.flag.h = (((hl & 0xfff) + (this->registers.sp & 0xfff)) > 0xfff);
		hl += this->registers.sp;
		this->registers.flag.n = 0;
		this->registers.flag.c = (sum > 0xffff);

		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x3a:		//LD A, (HL-)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.a = read(hl);
		hl--;
		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 1;
		return 2;
	}
	case 0x3b:		//DEC SP
	{
		this->registers.sp--;
		this->registers.pc += 1;
		return 2;
	}
	case 0x3c:		//INC A
	{
		this->registers.flag.h = ((this->registers.a & 0xf) == 0xf);

		this->registers.a++;
		this->registers.flag.z = (this->registers.a == 0);
		this->registers.flag.n = 0;

		this->registers.pc += 1;
		return 1;
	}
	case 0x3d:		//DEC A
	{
		this->registers.flag.h = ((this->registers.a & 0xf) == 0);	//needs a half carry?

		this->registers.a--;
		this->registers.flag.z = (this->registers.a == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 1;
		return 1;
	}
	case 0x3e:		//LD A, d8
	{
		this->registers.a = read(pc + 1);
		this->registers.pc += 2;
		return 2;
	}
	case 0x3f:		//CCF
	{
		this->registers.flag.c = ~this->registers.flag.c;
		this->registers.flag.n = 0;
		this->registers.flag.h = 0;
		this->registers.pc += 1;
		return 1;
	}
	case 0x40:		//LD B, B
	{
		return LD_r1_r2(this->registers.b, this->registers.b);
	}
	case 0x41:		//LD B, C
	{
		return LD_r1_r2(this->registers.b, this->registers.c);
	}
	case 0x42:		//LD B, D
	{
		return LD_r1_r2(this->registers.b, this->registers.d);
	}
	case 0x43:		//LD B, E
	{
		return LD_r1_r2(this->registers.b, this->registers.e);
	}
	case 0x44:		//LD B, H
	{
		return LD_r1_r2(this->registers.b, this->registers.h);
	}
	case 0x45:		//LD B, L
	{
		return LD_r1_r2(this->registers.b, this->registers.l);
	}
	case 0x46:		//LD B, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.b = read(hl);
		this->registers.pc += 1;
		return 2;
	}
	case 0x47:		//LD B, A
	{
		return LD_r1_r2(this->registers.b, this->registers.a);
	}
	case 0x48:		//LD C, B
	{
		return LD_r1_r2(this->registers.c, this->registers.b);
	}
	case 0x49:		//LD C, C
	{
		return LD_r1_r2(this->registers.c, this->registers.c);
	}
	case 0x4a:		//LD C, D
	{
		return LD_r1_r2(this->registers.c, this->registers.d);
	}
	case 0x4b:		//LD C, E
	{
		return LD_r1_r2(this->registers.c, this->registers.e);
	}
	case 0x4c:		//LD C, H
	{
		return LD_r1_r2(this->registers.c, this->registers.h);
	}
	case 0x4d:		//LD C, L
	{
		return LD_r1_r2(this->registers.c, this->registers.l);
	}
	case 0x4e:		//LD C, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.c = read(hl);
		this->registers.pc += 1;
		return 2;
	}
	case 0x4f:		//LD C, A
	{
		return LD_r1_r2(this->registers.c, this->registers.a);
	}

	case 0x50:		//LD D, B
	{
		return LD_r1_r2(this->registers.d, this->registers.b);
	}
	case 0x51:		//LD D, C
	{
		return LD_r1_r2(this->registers.d, this->registers.c);
	}
	case 0x52:		//LD D, D
	{
		return LD_r1_r2(this->registers.d, this->registers.d);
	}
	case 0x53:		//LD D, E
	{
		return LD_r1_r2(this->registers.d, this->registers.e);
	}
	case 0x54:		//LD D, H
	{
		return LD_r1_r2(this->registers.d, this->registers.h);
	}
	case 0x55:		//LD D, L
	{
		return LD_r1_r2(this->registers.d, this->registers.l);
	}
	case 0x56:		//LD D, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.d = read(hl);
		this->registers.pc += 1;
		return 2;
	}
	case 0x57:		//LD D, A
	{
		return LD_r1_r2(this->registers.d, this->registers.a);
	}

	case 0x58:		//LD E, B
	{
		return LD_r1_r2(this->registers.e, this->registers.b);
	}
	case 0x59:		//LD E, C
	{
		return LD_r1_r2(this->registers.e, this->registers.c);
	}
	case 0x5a:		//LD E, D
	{
		return LD_r1_r2(this->registers.e, this->registers.d);
	}
	case 0x5b:		//LD E, E
	{
		return LD_r1_r2(this->registers.e, this->registers.e);
	}
	case 0x5c:		//LD E, H
	{
		return LD_r1_r2(this->registers.e, this->registers.h);
	}
	case 0x5d:		//LD E, L
	{
		return LD_r1_r2(this->registers.e, this->registers.l);
	}
	case 0x5e:		//LD E, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.e = read(hl);
		this->registers.pc += 1;
		return 2;
	}
	case 0x5f:		//LD E, A
	{
		return LD_r1_r2(this->registers.e, this->registers.a);
	}
	case 0x60:		//LD H, B
	{
		return LD_r1_r2(this->registers.h, this->registers.b);
	}
	case 0x61:		//LD H, C
	{
		return LD_r1_r2(this->registers.h, this->registers.c);
	}
	case 0x62:		//LD H, D
	{
		return LD_r1_r2(this->registers.h, this->registers.d);
	}
	case 0x63:		//LD H, E
	{
		return LD_r1_r2(this->registers.h, this->registers.e);
	}
	case 0x64:		//LD H, H
	{
		return LD_r1_r2(this->registers.h, this->registers.h);
	}
	case 0x65:		//LD H, L
	{
		return LD_r1_r2(this->registers.h, this->registers.l);
	}
	case 0x66:		//LD H, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.h = read(hl);
		this->registers.pc += 1;
		return 2;
	}
	case 0x67:		//LD H, A
	{
		return LD_r1_r2(this->registers.h, this->registers.a);
	}
	case 0x68:		//LD L, B
	{
		return LD_r1_r2(this->registers.l, this->registers.b);
	}
	case 0x69:		//LD L, C
	{
		return LD_r1_r2(this->registers.l, this->registers.c);
	}
	case 0x6a:		//LD L, D
	{
		return LD_r1_r2(this->registers.l, this->registers.d);
	}
	case 0x6b:		//LD L, E
	{
		return LD_r1_r2(this->registers.l, this->registers.e);
	}
	case 0x6c:		//LD L, H
	{
		return LD_r1_r2(this->registers.l, this->registers.h);
	}
	case 0x6d:		//LD L, L
	{
		return LD_r1_r2(this->registers.l, this->registers.l);
	}
	case 0x6e:		//LD L, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.l = read(hl);
		this->registers.pc += 1;
		return 2;
	}
	case 0x6f:		//LD L, A
	{
		return LD_r1_r2(this->registers.l, this->registers.a);
	}
	case 0x70:		//LD (HL), B
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.b);

		this->registers.pc += 1;
		return 2;
	}
	case 0x71:		//LD (HL), C
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.c);

		this->registers.pc += 1;
		return 2;
	}
	case 0x72:		//LD (HL), D
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.d);

		this->registers.pc += 1;
		return 2;
	}
	case 0x73:		//LD (HL), E
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.e);

		this->registers.pc += 1;
		return 2;
	}
	case 0x74:		//LD (HL), H
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.h);

		this->registers.pc += 1;
		return 2;
	}
	case 0x75:		//LD (HL), L
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.l);

		this->registers.pc += 1;
		return 2;
	}
	case 0x76:		//HALT
	{
		if (this->registers.IME) {		//interrupt are enabled
			this->registers.halted = 1;
		}
		this->registers.pc += 1;
		return 1;
	}
	case 0x77:		//LD (HL), A
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		write(hl, this->registers.a);
		
		this->registers.pc += 1;
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
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.a = read(hl);
		this->registers.pc += 1;
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
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		ADD_n(n);
		return 2;
	}
	case 0x87:		//ADD A
	{
		return ADD_n(registers.a);
	}
	case 0x88:		//ADC A, B
	{
		return ADC_A_n(this->registers.b);
	}
	case 0x89:		//ADC A, C
	{
		return ADC_A_n(this->registers.c);
	}
	case 0x8a:		//ADC A, D
	{
		return ADC_A_n(this->registers.d);
	}
	case 0x8b:		//ADC A, E
	{
		return ADC_A_n(this->registers.e);
	}
	case 0x8c:		//ADC A, H
	{
		return ADC_A_n(this->registers.h);
	}
	case 0x8d:		//ADC A, L
	{
		return ADC_A_n(this->registers.l);
	}
	case 0x8e:		//ADC A, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		ADC_A_n(n);
		return 2;
	}
	case 0x8f:		//ADC A, A
	{
		return ADC_A_n(this->registers.a);
	}
	case 0x90:		//SUB B
	{
		return SUB_n(this->registers.b);
	}
	case 0x91:		//SUB C
	{
		return SUB_n(this->registers.c);
	}
	case 0x92:		//SUB D
	{
		return SUB_n(this->registers.d);
	}
	case 0x93:		//SUB E
	{
		return SUB_n(this->registers.e);
	}
	case 0x94:		//SUB H
	{
		return SUB_n(this->registers.h);
	}
	case 0x95:		//SUB L
	{
		return SUB_n(this->registers.l);
	}
	case 0x96:		//SUB (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		SUB_n(n);
		return 2;
	}
	case 0x97:		//SUB A
	{
		return SUB_n(this->registers.a);
	}
	case 0x98:		//SBC A, B
	{
		return SBC_A_n(this->registers.b);
	}
	case 0x99:		//SBC A, C
	{
		return SBC_A_n(this->registers.c);
	}
	case 0x9a:		//SBC A, D
	{
		return SBC_A_n(this->registers.d);
	}
	case 0x9b:		//SBC A, E
	{
		return SBC_A_n(this->registers.e);
	}
	case 0x9c:		//SBC A, H
	{
		return SBC_A_n(this->registers.h);
	}
	case 0x9d:		//SBC A, L
	{
		return SBC_A_n(this->registers.l);
	}
	case 0x9e:		//SBC A, (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		SBC_A_n(n);
		return 2;
	}
	case 0x9f:		//SBC A, A
	{
		return SBC_A_n(this->registers.a);
	}
	case 0xa0:		//AND B
	{
		return AND_n(this->registers.b);
	}
	case 0xa1:		//AND C
	{
		return AND_n(this->registers.c);
	}
	case 0xa2:		//AND D
	{
		return AND_n(this->registers.d);
	}
	case 0xa3:		//AND E
	{
		return AND_n(this->registers.e);
	}
	case 0xa4:		//AND H
	{
		return AND_n(this->registers.h);
	}
	case 0xa5:		//AND L
	{
		return AND_n(this->registers.l);
	}
	case 0xa6:		//AND (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		AND_n(n);
		return 2;
	}
	case 0xa7:		//AND A
	{
		return AND_n(this->registers.a);
	}
	case 0xa8:		//XOR B
	{
		return XOR_n(this->registers.b);
	}
	case 0xa9:		//XOR C
	{
		return XOR_n(this->registers.c);
	}
	case 0xaa:		//XOR D
	{
		return XOR_n(this->registers.d);
	}
	case 0xab:		//XOR E
	{
		return XOR_n(this->registers.e);
	}
	case 0xac:		//XOR H
	{
		return XOR_n(this->registers.h);
	}
	case 0xad:		//XOR L
	{
		return XOR_n(this->registers.l);
	}
	case 0xae:		//XOR (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		XOR_n(n);
		return 2;
	}
	case 0xaf:		//XOR A
	{
		return XOR_n(this->registers.a);
	}
	case 0xb0:		//OR B
	{
		return OR_n(this->registers.b);
	}
	case 0xb1:		//OR C
	{
		return OR_n(this->registers.c);
	}
	case 0xb2:		//OR D
	{
		return OR_n(this->registers.d);
	}
	case 0xb3:		//OR E
	{
		return OR_n(this->registers.e);
	}
	case 0xb4:		//OR H
	{
		return OR_n(this->registers.h);
	}
	case 0xb5:		//OR L
	{
		return OR_n(this->registers.l);
	}
	case 0xb6:		//OR (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		OR_n(n);
		return 2;
	}
	case 0xb7:		//OR A
	{
		return OR_n(this->registers.a);
	}
	case 0xb8:		//CP B
	{
		return CP_n(this->registers.b);
	}
	case 0xb9:		//CP C
	{
		return CP_n(this->registers.c);
	}
	case 0xba:		//CP D
	{
		return CP_n(this->registers.d);
	}
	case 0xbb:		//CP E
	{
		return CP_n(this->registers.e);
	}
	case 0xbc:		//CP H
	{
		return CP_n(this->registers.h);
	}
	case 0xbd:		//CP L
	{
		return CP_n(this->registers.l);
	}
	case 0xbe:		//CP (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);

		CP_n(n);
		return 2;
	}
	case 0xbf:		//CP A
	{
		return CP_n(this->registers.a);
	}
	case 0xc0:		//RET NZ
	{
		if (!this->registers.flag.z) {
			this->registers.pc = (read(this->registers.sp) | (read(this->registers.sp + 1) << 8));
			this->registers.sp += 2;
			return 5;
		}

		this->registers.pc += 1;
		return 2;
	}
	case 0xc1:		//POP BC
	{
		this->registers.c = read(this->registers.sp);
		this->registers.b = read(this->registers.sp + 1);
		this->registers.sp += 2;
		this->registers.pc += 1;
		return 3;
	}
	case 0xc2:		//JP NZ d16
	{
		if (!this->registers.flag.z) {
			uint16_t addr = (read(pc + 1) | (read(pc + 2) << 8));
			this->registers.pc = addr;
			return 4;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xc3:		//JP d16
	{
		uint16_t addr = (read(pc + 1) | (read(pc + 2) << 8));
		this->registers.pc = addr;
		//this->registers.pc += 3;
		return 3;
	}
	case 0xc4:		//CALL NZ
	{
		if (!this->registers.flag.z) {
			uint16_t jump_addr = (read(pc + 1) | (read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
			write(this->registers.sp - 2, ret_addr & 0Xff);

			this->registers.sp -= 2;
			this->registers.pc = jump_addr;
			return 6;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xc5:		//PUSH BC
	{
		write(this->registers.sp - 1, this->registers.b);
		write(this->registers.sp - 2, this->registers.c);
		this->registers.sp -= 2;
		this->registers.pc += 1;
		return 4;
	}
	
	case 0xc6:		//ADD A, d8
	{
		uint8_t n = read(pc + 1);
		ADD_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xc7:		//RST 0x0
	{
		uint16_t jump_addr = 0x0;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xc8:		//RET Z
	{
		if (this->registers.flag.z) {
			this->registers.pc = (read(this->registers.sp) | (read(this->registers.sp + 1) << 8));
			this->registers.sp += 2;
		}
		else {
			this->registers.pc += 1;
		}

		return 2;
	}
	case 0xc9:		//RET
	{
		this->registers.pc = (read(this->registers.sp) | (read(this->registers.sp + 1) << 8));
		this->registers.sp += 2;
		return 2;
	}
	case 0xca:		//JP Z d16
	{
		if (this->registers.flag.z) {
			uint16_t addr = (read(pc + 1) | (read(pc + 2) << 8));
			this->registers.pc = addr;
			return 4;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xcb:		//PREFIX
	{
		this->registers.pc += 1;
		return prefixed_execute() + 1;
	}
	case 0xcc:		//CALL Z a16
	{
		if (this->registers.flag.z) {
			uint16_t jump_addr = (read(pc + 1) | (read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
			write(this->registers.sp - 2, ret_addr & 0Xff);

			this->registers.sp -= 2;
			this->registers.pc = jump_addr;
			return 6;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xcd:		//CALL 
	{
		uint16_t jump_addr = (read(pc + 1) | (read(pc + 2) << 8));

		uint16_t ret_addr = pc + 3;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 3;
	}
	case 0xce:		//ADC A, d8
	{
		uint8_t n = read(pc + 1);
		ADC_A_n(n);
		this->registers.pc += 1;
		return 2;
	}
	case 0xcf:		//RST 0x8
	{
		uint16_t jump_addr = 0x8;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xd0:		//RET NC
	{
		if (!this->registers.flag.c) {
			this->registers.pc = (read(this->registers.sp) | (read(this->registers.sp + 1) << 8));
			this->registers.sp += 2;
			return 5;
		}
		
		this->registers.pc += 1;
		return 2;
	}
	case 0xd1:		//POP DE
	{
		this->registers.e = read(this->registers.sp);
		this->registers.d = read(this->registers.sp + 1);
		this->registers.sp += 2;
		this->registers.pc += 1;
		return 3;
	}
	case 0xd2:		//JP NC d16
	{
		if (!this->registers.flag.c) {
			uint16_t addr = (read(pc + 1) | (read(pc + 2) << 8));
			this->registers.pc = addr;
			return 4;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xd3:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xd3\nPC = " + std::to_string(registers.pc));
	}
	case 0xd4:		//CALL NC a16
	{
		if (!this->registers.flag.c) {
			uint16_t jump_addr = (read(pc + 1) | (read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
			write(this->registers.sp - 2, ret_addr & 0Xff);

			this->registers.sp -= 2;
			this->registers.pc = jump_addr;
			return 6;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xd5:		//PUSH DE
	{
		write(this->registers.sp - 1, this->registers.d);
		write(this->registers.sp - 2, this->registers.e);
		this->registers.sp -= 2;
		this->registers.pc += 1;
		return 4;
	}
	case 0xd6:		//SUB d8
	{
		uint8_t n = read(pc + 1);
		this->registers.flag.h = ((this->registers.a & 0xf) < (n & 0xf));
		this->registers.flag.c = (this->registers.a < n);

		this->registers.a -= n;
		this->registers.flag.z = (this->registers.a == 0);
		this->registers.flag.n = 1;

		this->registers.pc += 2;
		return 2;
	}
	case 0xd7:		//RST 0x10
	{
		uint16_t jump_addr = 0x10;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xd8:		//RET C
	{
		if (this->registers.flag.c) {
			this->registers.pc = (read(this->registers.sp) | (read(this->registers.sp + 1) << 8));
			this->registers.sp += 2;
			return 5;
		}

		this->registers.pc += 1;
		return 2;
	}
	case 0xd9:		//RETI
	{
		this->registers.pc = (read(this->registers.sp) | (read(this->registers.sp + 1) << 8));
		this->registers.sp += 2;
		this->registers.IME = 1;
		return 2;
	}
	case 0xda:		//JP C a16
	{
		if (this->registers.flag.c) {
			uint16_t addr = (read(pc + 1) | (read(pc + 2) << 8));
			this->registers.pc = addr;
			return 4;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xdb:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xdb\nPC = " + std::to_string(registers.pc));
	}
	case 0xdc:		//CALL C a16
	{
		if (this->registers.flag.c) {
			uint16_t jump_addr = (read(pc + 1) | (read(pc + 2) << 8));

			uint16_t ret_addr = pc + 3;
			write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
			write(this->registers.sp - 2, ret_addr & 0Xff);

			this->registers.sp -= 2;
			this->registers.pc = jump_addr;
			return 6;
		}
		this->registers.pc += 3;
		return 3;
	}
	case 0xdd:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xdd\nPC = " + std::to_string(registers.pc));
	}
	case 0xde:		//SBC A, d8
	{
		uint8_t n = read(pc + 1);
		SBC_A_n(n);
		this->registers.pc += 1;
		return 2;
	}
	case 0xdf:		//RST 0x18
	{
		uint16_t jump_addr = 0x18;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xe0:		//LDH (n), A
	{
		uint16_t gb_addr = 0xff00 + read(pc + 1);
		write(gb_addr, this->registers.a);
		this->registers.pc += 2;
		return 4;
	}
	case 0xe1:			//POP HL
	{
		this->registers.l = read(this->registers.sp);
		this->registers.h = read(this->registers.sp + 1);
		this->registers.sp += 2;
		this->registers.pc += 1;
		return 3;
	}
	case 0xe2:		//LD (C), A
	{
		uint16_t gb_addr = 0xff00 + this->registers.c;
		write(gb_addr, this->registers.a);
		this->registers.pc += 1;
		return 2;
	}
	case 0xe3:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xe3\nPC = " + std::to_string(registers.pc));
	}
	case 0xe4:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xe4\nPC = " + std::to_string(registers.pc));
	}
	case 0xe5:		//PUSH HL
	{
		write(this->registers.sp - 1, this->registers.h);
		write(this->registers.sp - 2, this->registers.l);
		this->registers.sp -= 2;
		this->registers.pc += 1;
		return 4;
	}
	case 0xe6:		//AND d8
	{
		uint8_t n = read(pc + 1);
		AND_n(n);
		this->registers.pc += 1;
		return 2;
	}
	case 0xe7:		//RST 0x20
	{
		uint16_t jump_addr = 0x20;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xe8:		//ADD SP, r8
	{
		char n = read(pc + 1);
		registers.flag.h = ((registers.sp & 0xf) + (((uint8_t)n) & 0xf) > 0xf);
		registers.flag.c = ((registers.sp & 0xff) + ((uint8_t)n) > 0xff);
		registers.sp = (unsigned)((short)this->registers.sp + n);

		registers.flag.z = 0;
		registers.flag.n = 0;
		registers.pc += 2;
		return 4;
	}
	case 0xe9:		//JP HL
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.pc = hl;
		return 1;
	}
	case 0xea:		//LD (d16), A
	{
		uint16_t gb_addr = (read(pc + 1) | (read(pc + 2) << 8));
		write(gb_addr, this->registers.a);
		this->registers.pc += 3;
		return 4;
	}
	case 0xeb:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xeb\nPC = " + std::to_string(registers.pc));
	}
	case 0xec:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xec\nPC = " + std::to_string(registers.pc));
	}
	case 0xed:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xed\nPC = " + std::to_string(registers.pc));
	}
	case 0xee:		//XOR d8
	{
		uint8_t n = read(pc + 1);
		XOR_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xef:		//RST 28H
	{
		uint16_t jump_addr = 0x28;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xf0:		//LDH A, d8
	{
		uint16_t gb_addr = 0xff00 + read(pc + 1);
		this->registers.a = read(gb_addr);
		this->registers.pc += 2;
		return 3;
	}
	case 0xf1:		//POP AF
	{
		*((uint8_t*)&this->registers.flag) = read(this->registers.sp );
		this->registers.a = read(this->registers.sp + 1);

		this->registers.sp += 2;
		this->registers.pc += 1;
		return 3;
	}
	case 0xf2:		//LD A,(C)
	{
		uint16_t gb_addr = 0xff00 + this->registers.c;
		this->registers.a = read(gb_addr);
		this->registers.pc += 1;
		return 2;
	}
	case 0xf3:		//DI
	{
		this->registers.IME_U = 0;
		this->registers.IME_CC = 2;
		this->registers.pc += 1;
		return 1;
	}
	case 0xf4:		//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xf4\nPC = " + std::to_string(registers.pc));
	}
	case 0xf5:		//PUSH AF
	{
		uint8_t flag = *((uint8_t*)&this->registers.flag);
		write(this->registers.sp - 1, this->registers.a);
		write(this->registers.sp - 2, flag);
		this->registers.sp -= 2;
		this->registers.pc += 1;
		return 4;
	}
	case 0xf6:		//OR d8
	{
		uint8_t n = read(pc + 1);
		OR_n(n);
		registers.pc += 1;
		return 2;
	}
	case 0xf7:		//RST 0x30
	{
		uint16_t jump_addr = 0x30;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	case 0xf8:		//LD HL, SP + r8
	{
		uint8_t n = read(pc + 1);
		uint16_t hl = this->registers.sp + n;

		this->registers.flag.z = 0;
		this->registers.flag.n = 0;
		this->registers.flag.h = (((this->registers.sp & 0xf) + (n & 0xf)) > 0xf);
		this->registers.flag.c = (((this->registers.sp & 0xff) + n) > 0xff);

		this->registers.h = (hl >> 8) & 0xff;
		this->registers.l = hl & 0xff;
		this->registers.pc += 2;
		return 3;
	}
	case 0xf9:		//LD SP, HL
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		this->registers.sp = hl;
		this->registers.pc += 1;
		return 2;
	}
	case 0xfa:		//LD A, (d16)
	{
		uint16_t gb_addr = (read(pc + 1) | (read(pc + 2) << 8));
		this->registers.a = read(gb_addr);
		this->registers.pc += 3;
		return 4;
	}
	case 0xfb:		//EI
	{
		this->registers.IME_U = 1;
		this->registers.IME_CC = 2;
		this->registers.pc += 1;
		return 1;
	}
	case 0xfc:				//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xfc\nPC = " + std::to_string(registers.pc));
	}
	case 0xfd:				//INVALID OPCODE
	{
		fatal(FATAL_INVALID_OPCODE, __func__, "\nOpcode: 0xfd\nPC = " + std::to_string(registers.pc));
	}
	case 0xfe:		//CP, d8
	{
		uint8_t d = read(pc + 1);
		this->registers.flag.z = (this->registers.a == d);
		this->registers.flag.n = 1;
		this->registers.flag.h = ((this->registers.a & 0xf) < (d & 0xf));
		this->registers.flag.c = (this->registers.a < d);
		this->registers.pc += 2;
		return 1;
	}
	case 0xff:		//RST 0x38
	{
		uint16_t jump_addr = 0x38;

		uint16_t ret_addr = pc + 1;
		write(this->registers.sp - 1, (ret_addr >> 8) & 0xff);
		write(this->registers.sp - 2, ret_addr & 0Xff);

		this->registers.sp -= 2;
		this->registers.pc = jump_addr;
		return 8;
	}
	default:
 		std::cout << "\nInstruction not implemented: 0x" << std::hex << (int)opcode;
		fatal(FATAL_INSTRUCTION_NOT_IMPLEMENTED, __func__);
	}

	return 0;
}


int GameBoy::prefixed_execute() {
	uint16_t pc = this->registers.pc;
	uint8_t opcode = read(pc);

	switch (opcode) {
	case 0x00:		//RLC B
	{
		return RLC_n(this->registers.b);
	}
	case 0x01:		//RLC C
	{
		return RLC_n(this->registers.c);
	}
	case 0x02:		//RLC D
	{
		return RLC_n(this->registers.d);
	}
	case 0x03:		//RLC E
	{
		return RLC_n(this->registers.e);
	}
	case 0x04:		//RLC H
	{
		return RLC_n(this->registers.h);
	}
	case 0x05:		//RLC L
	{
		return RLC_n(this->registers.l);
	}
	case 0x06:		//RLC (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		RLC_n(n);
		write(hl, n);
		return 4;
	}
	case 0x07:		//RLC A
	{
		return RLC_n(this->registers.a);
	}
	case 0x08:		//RRC B
	{
		return RRC_n(this->registers.b);
	}
	case 0x09:		//RRC C
	{
		return RRC_n(this->registers.c);
	}
	case 0x0a:		//RRC D
	{
		return RRC_n(this->registers.d);
	}
	case 0x0b:		//RRC E
	{
		return RRC_n(this->registers.e);
	}
	case 0x0c:		//RRC H
	{
		return RRC_n(this->registers.h);
	}
	case 0x0d:		//RRC L
	{
		return RRC_n(this->registers.l);
	}
	case 0x0e:		//RRC (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		RRC_n(n);
		write(hl, n);
		return 4;
	}
	case 0xf:		//RRC A
	{
		return RRC_n(this->registers.a);
	}
	case 0x10:		//RL B
	{
		return RL_n(this->registers.b);
	}
	case 0x11:		//RL C
	{
		return RL_n(this->registers.c);
	}
	case 0x12:		//RL D
	{
		return RL_n(this->registers.d);
	}
	case 0x13:		//RL E
	{
		return RL_n(this->registers.e);
	}
	case 0x14:		//RL H
	{
		return RL_n(this->registers.h);
	}
	case 0x15:		//RL L
	{
		return RL_n(this->registers.l);
	}
	case 0x16:		//RL (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		RL_n(n);
		write(hl, n);
		return 4;
	}
	case 0x17:		//RL A
	{
		return RL_n(this->registers.a);
	}
	case 0x18:		//RR B
	{
		return RR_n(this->registers.b);
	}
	case 0x19:		//RR C
	{
		return RR_n(this->registers.c);
	}
	case 0x1a:		//RR D
	{
		return RR_n(this->registers.d);
	}
	case 0x1b:		//RR E
	{
		return RR_n(this->registers.e);
	}
	case 0x1c:		//RR H
	{
		return RR_n(this->registers.h);
	}
	case 0x1d:		//RR L
	{
		return RR_n(this->registers.l);
	}
	case 0x1e:		//RR (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		RR_n(n);
		write(hl, n);
		return 4;
	}
	case 0x1f:		//RR A
	{
		return RR_n(this->registers.a);
	}
	
	case 0x20:		//SLA B
	{
		return SLA_n(this->registers.b);
	}
	case 0x21:		//SLA C
	{
		return SLA_n(this->registers.c);
	}
	case 0x22:		//SLA D
	{
		return SLA_n(this->registers.d);
	}
	case 0x23:		//SLA E
	{
		return SLA_n(this->registers.e);
	}
	case 0x24:		//SLA H
	{
		return SLA_n(this->registers.h);
	}
	case 0x25:		//SLA L
	{
		return SLA_n(this->registers.l);
	}
	case 0x26:		//SLA (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		SLA_n(n);
		write(hl, n);
		return 4;
	}
	case 0x27:		//SLA A
	{
		return SLA_n(this->registers.a);
	}
	case 0x28:		//SRA B
	{
		return SRA_n(this->registers.b);
	}
	case 0x29:		//SRA C
	{
		return SRA_n(this->registers.c);
	}
	case 0x2a:		//SRA D
	{
		return SRA_n(this->registers.d);
	}
	case 0x2b:		//SRA E
	{
		return SRA_n(this->registers.e);
	}
	case 0x2c:		//SRA H
	{
		return SRA_n(this->registers.h);
	}
	case 0x2d:		//SRA L
	{
		return SRA_n(this->registers.l);
	}
	case 0x2e:		//SRA (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		SRA_n(n);
		write(hl, n);
		return 4;
	}
	case 0x2f:		//SRA A
	{
		return SRA_n(this->registers.a);
	}
	case 0x30:		//SWAP B
	{
		return SWAP_n(this->registers.b);
	}
	case 0x31:		//SWAP C
	{
		return SWAP_n(this->registers.c);
	}
	case 0x32:		//SWAP D
	{
		return SWAP_n(this->registers.d);
	}
	case 0x33:		//SWAP E
	{
		return SWAP_n(this->registers.e);
	}
	case 0x34:		//SWAP H
	{
		return SWAP_n(this->registers.h);
	}
	case 0x35:		//SWAP L
	{
		return SWAP_n(this->registers.l);
	}
	case 0x36:		//SWAP (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		SWAP_n(n);
		write(hl, n);
		return 4;
	}
	case 0x37:		//SWAP A
	{
		return SWAP_n(this->registers.a);
	}
	case 0x38:		//SRL B
	{
		return SRL_n(this->registers.b);
	}
	case 0x39:		//SRL C
	{
		return SRL_n(this->registers.c);
	}
	case 0x3a:		//SRL D
	{
		return SRL_n(this->registers.d);
	}
	case 0x3b:		//SRL E
	{
		return SRL_n(this->registers.e);
	}
	case 0x3c:		//SRL H
	{
		return SRL_n(this->registers.h);
	}
	case 0x3d:		//SRL L
	{
		return SRL_n(this->registers.l);
	}
	case 0x3e:		//SRL (HL)
	{
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		SRL_n(n);
		write(hl, n);
		return 4;
	}
	case 0x3f:		//SRL A
	{
		return SRL_n(this->registers.a);
	}

	//BIT b, B	
	case 0x40:case 0x48:case 0x50:case 0x58:case 0x60:case 0x68:case 0x70:case 0x78:
	{
		uint8_t bit = (opcode - 0x40) / 8;
		this->registers.flag.z = ((this->registers.b & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//BIT b, C
	case 0x41:case 0x49:case 0x51:case 0x59:case 0x61:case 0x69:case 0x71:case 0x79:
	{
		uint8_t bit = (opcode - 0x41) / 8;
		this->registers.flag.z = ((this->registers.c & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//BIT b, D
	case 0x42:case 0x4a:case 0x52:case 0x5a:case 0x62:case 0x6a:case 0x72:case 0x7a:
	{
		uint8_t bit = (opcode - 0x42) / 8;
		this->registers.flag.z = ((this->registers.d & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//BIT b, E
	case 0x43:case 0x4b:case 0x53:case 0x5b:case 0x63:case 0x6b:case 0x73:case 0x7b:
	{
		uint8_t bit = (opcode - 0x43) / 8;
		this->registers.flag.z = ((this->registers.e & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//BIT b, H
	case 0x44:case 0x4c:case 0x54:case 0x5c:case 0x64:case 0x6c:case 0x74:case 0x7c:
	{
		uint8_t bit = (opcode - 0x44) / 8;
		this->registers.flag.z = ((this->registers.h & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//BIT b, L
	case 0x45:case 0x4d:case 0x55:case 0x5d:case 0x65:case 0x6d:case 0x75:case 0x7d:
	{
		uint8_t bit = (opcode - 0x45) / 8;
		this->registers.flag.z = ((this->registers.l & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//BIT b, (HL)
	case 0x46:case 0x4e:case 0x56:case 0x5e:case 0x66:case 0x6e:case 0x76:case 0x7e:
	{
		uint8_t bit = (opcode - 0x46) / 8;
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		this->registers.flag.z = ((n & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 3;
	}
	//BIT b, A
	case 0x47:case 0x4f:case 0x57:case 0x5f:case 0x67:case 0x6f:case 0x77:case 0x7f:
	{
		uint8_t bit = (opcode - 0x47) / 8;
		this->registers.flag.z = ((this->registers.a & (0x1 << bit)) == 0);
		this->registers.flag.n = 0;
		this->registers.flag.h = 1;
		this->registers.pc += 1;
		return 2;
	}
	//RES b, B
	case 0x80:case 0x88:case 0x90:case 0x98:case 0xa0:case 0xa8:case 0xb0:case 0xb8:
	{
		uint8_t bit = (opcode - 0x80) / 8;
		this->registers.b &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//RES b, C
	case 0x81:case 0x89:case 0x91:case 0x99:case 0xa1:case 0xa9:case 0xb1:case 0xb9:
	{
		uint8_t bit = (opcode - 0x81) / 8;
		this->registers.c &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//RES b, D
	case 0x82:case 0x8a:case 0x92:case 0x9a:case 0xa2:case 0xaa:case 0xb2:case 0xba:
	{
		uint8_t bit = (opcode - 0x82) / 8;
		this->registers.d &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//RES b, E
	case 0x83:case 0x8b:case 0x93:case 0x9b:case 0xa3:case 0xab:case 0xb3:case 0xbb:
	{
		uint8_t bit = (opcode - 0x83) / 8;
		this->registers.e &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//RES b, H
	case 0x84:case 0x8c:case 0x94:case 0x9c:case 0xa4:case 0xac:case 0xb4:case 0xbc:
	{
		uint8_t bit = (opcode - 0x84) / 8;
		this->registers.h &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//RES b, L
	case 0x85:case 0x8d:case 0x95:case 0x9d:case 0xa5:case 0xad:case 0xb5:case 0xbd:
	{
		uint8_t bit = (opcode - 0x85) / 8;
		this->registers.l &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//RES b, (HL)
	case 0x86:case 0x8e:case 0x96:case 0x9e:case 0xa6:case 0xae:case 0xb6:case 0xbe:
	{
		uint8_t bit = (opcode - 0x86) / 8;
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t n = read(hl);
		n &= ~(0x1 << bit);
		write(hl, n);
		this->registers.pc += 1;
		return 4;
	}
	//RES b, A
	case 0x87:case 0x8f:case 0x97:case 0x9f:case 0xa7:case 0xaf:case 0xb7:case 0xbf:
	{
		uint8_t bit = (opcode - 0x87) / 8;
		this->registers.a &= ~(0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, B
	case 0xc0:case 0xc8:case 0xd0:case 0xd8:case 0xe0:case 0xe8:case 0xf0:case 0xf8:
	{
		uint8_t bit = (opcode - 0xc0) / 8;
		this->registers.b |= (0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, C
	case 0xc1:case 0xc9:case 0xd1:case 0xd9:case 0xe1:case 0xe9:case 0xf1:case 0xf9:
	{
		uint8_t bit = (opcode - 0xc1) / 8;
		this->registers.c |= (0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, D
	case 0xc2:case 0xca:case 0xd2:case 0xda:case 0xe2:case 0xea:case 0xf2:case 0xfa:
	{
		uint8_t bit = (opcode - 0xc2) / 8;
		this->registers.d |= (0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, E
	case 0xc3:case 0xcb:case 0xd3:case 0xdb:case 0xe3:case 0xeb:case 0xf3:case 0xfb:
	{
		uint8_t bit = (opcode - 0xc3) / 8;
		this->registers.e |= (0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, H
	case 0xc4:case 0xcc:case 0xd4:case 0xdc:case 0xe4:case 0xec:case 0xf4:case 0xfc:
	{
		uint8_t bit = (opcode - 0xc4) / 8;
		this->registers.h |= (0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, L
	case 0xc5:case 0xcd:case 0xd5:case 0xdd:case 0xe5:case 0xed:case 0xf5:case 0xfd:
	{
		uint8_t bit = (opcode - 0xc5) / 8;
		this->registers.l |= (0x1 << bit);
		this->registers.pc += 1;
		return 2;
	}
	//SET b, (HL)
	case 0xc6:case 0xce:case 0xd6:case 0xde:case 0xe6:case 0xee:case 0xf6:case 0xfe:
	{
		uint8_t bit = (opcode - 0xc6) / 8;
		uint16_t hl = (this->registers.h << 8) | this->registers.l;
		uint8_t d = read(hl);
		d |= (0x1 << bit);
		write(hl, d);
		this->registers.pc += 1;
		return 4;
	}
	//SET b, A
	case 0xc7:case 0xcf:case 0xd7:case 0xdf:case 0xe7:case 0xef:case 0xf7:case 0xff:
	{
		uint8_t bit = (opcode - 0xc7) / 8;
		this->registers.a |= (0x1 << bit);
		this->registers.pc += 1;
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
	this->registers.flag.z = (reg == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.c = 0;
	this->registers.pc += 1;
	return 2;
}

int GameBoy::SLA_n(uint8_t& reg) {
	this->registers.flag.c = ((reg & 0x80) != 0);
	reg <<= 1;
	this->registers.flag.z = (reg == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;

	this->registers.pc += 1;
	return 2;
}

int GameBoy::SRA_n(uint8_t& reg) {
	this->registers.flag.c = (reg & 0x1);
	reg = ((reg & 0x80) | (reg >> 1));
	this->registers.flag.z = (reg == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;

	this->registers.pc += 1;
	return 2;
}

int GameBoy::RL_n(uint8_t& reg) {
	uint8_t bit0 = this->registers.flag.c;
	this->registers.flag.c = ((reg & 0x80) != 0);
	reg <<= 1;
	reg |= bit0;
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.z = (reg == 0);

	this->registers.pc += 1;
	return 2;
}

int GameBoy::RR_n(uint8_t& reg) {
	uint8_t bit7 = this->registers.flag.c;
	this->registers.flag.c = ((reg & 0x1) != 0);
	reg >>= 1;
	reg |= (bit7 << 7);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.z = (reg == 0);

	this->registers.pc += 1;
	return 2;
}


int GameBoy::RLC_n(uint8_t& reg) {
	this->registers.flag.c = (reg >> 7) & 0x1;
	reg <<= 1;
	reg |= this->registers.flag.c;
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.z = (reg == 0);
	this->registers.pc += 1;
	return 2;
}

int GameBoy::RRC_n(uint8_t& reg) {
	this->registers.flag.c = (reg & 0x1);
	reg >>= 1;
	reg |= (this->registers.flag.c << 7);

	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.z = (reg == 0);
	this->registers.pc += 1;
	return 2;
}

int GameBoy::LD_r1_r2(uint8_t& reg1, uint8_t& reg2) {
	reg1 = reg2;
	this->registers.pc += 1;
	return 1;
}

int GameBoy::SBC_A_n(uint8_t& reg) {
	uint8_t n = reg + this->registers.flag.c;
	
	this->registers.flag.h = ((this->registers.a & 0xf) < (n & 0xf));
	this->registers.flag.c = (this->registers.a < n);

	this->registers.a -= n;
	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 1;

	this->registers.pc += 1;
	return 1;
}

int GameBoy::ADC_A_n(uint8_t& reg) {
	uint16_t sum = this->registers.a + reg + this->registers.flag.c;

	this->registers.flag.h = (((this->registers.a & 0xf) + (reg & 0xf) + this->registers.flag.c) > 0xf);

	this->registers.a += (reg + this->registers.flag.c);

	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 0;
	this->registers.flag.c = (sum > 0xff);

	this->registers.pc += 1;
	return 1;
}

int GameBoy::OR_n(uint8_t& reg) {
	this->registers.a |= reg;

	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.c = 0;

	this->registers.pc += 1;
	return 1;
}

int GameBoy::AND_n(uint8_t& reg) {
	this->registers.a &= reg;

	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 1;
	this->registers.flag.c = 0;

	this->registers.pc += 1;
	return 1;
}

int GameBoy::XOR_n(uint8_t& reg) {
	this->registers.a ^= reg;

	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;
	this->registers.flag.c = 0;

	this->registers.pc += 1;
	return 1;
}

int GameBoy::CP_n(uint8_t& reg) {
	this->registers.flag.z = (this->registers.a == reg);
	this->registers.flag.n = 1;
	this->registers.flag.h = ((this->registers.a & 0xf) < (reg & 0xf));
	this->registers.flag.c = (this->registers.a < reg);

	this->registers.pc += 1;
	return 1;
}

int GameBoy::SRL_n(uint8_t& reg) {
	this->registers.flag.c = ((reg & 0x1) != 0);
	reg >>= 1;
	reg &= 0x7f;
	this->registers.flag.z = (reg == 0);
	this->registers.flag.n = 0;
	this->registers.flag.h = 0;

	this->registers.pc += 1;
	return 2;
}

int GameBoy::SUB_n(uint8_t& reg) {
	this->registers.flag.h = ((this->registers.a & 0xf) < (reg & 0xf));
	this->registers.flag.c = (this->registers.a < reg);

	this->registers.a -= reg;
	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 1;

	this->registers.pc += 1;
	return 1;
}

int GameBoy::ADD_n(uint8_t& reg) {
	uint16_t sum = this->registers.a + reg;

	this->registers.flag.h = (((this->registers.a & 0xf) + (reg & 0xf)) > 0xf);
	this->registers.a += reg;
	this->registers.flag.z = (this->registers.a == 0);
	this->registers.flag.n = 0;
	this->registers.flag.c = (sum > 0xff);

	this->registers.pc += 1;
	return 1;
}

uint8_t* GameBoy::translateAddr(uint16_t addr) {
	if (this->io_map->BRC == 0 && addr < 0x100) {		//intercept accesses to 0x0000 - 0x00ff (boot rom)
		return (uint8_t*)(addr + this->boot_rom);
	}

	return (uint8_t*)(addr + this->gb_mem);
}


//translate the gameboy address into a real memory address and read a byte
uint8_t GameBoy::read(uint16_t gb_address) {

	if (this->io_map->BRC == 0 && gb_address < 0x100) {	//bootstrap rom
		return this->boot_rom[gb_address];
	}

	if ((gb_address >= 0 && gb_address <= 0x7fff) || (gb_address >= 0xa000 && gb_address <= 0xbfff)) {		//cartridge address
		return this->cart->read(gb_address);
	}

	return this->gb_mem[gb_address];
}

//translate the gameboy address into a real memory address and write a byte
void GameBoy::write(uint16_t gb_address, uint8_t value) {

	if (this->io_map->BRC == 0 && gb_address < 0x100) {	//bootstrap rom
		return;
	}

	if ((gb_address >= 0 && gb_address <= 0x7fff) || (gb_address >= 0xa000 && gb_address <= 0xbfff)) {		//cartridge address
		this->cart->write(gb_address, value);
		return;
	}

	//writing any value to the divider register resets it to 0
	if (gb_address == 0xff04) value = 0;
	if (gb_address == 0xff1e) 
		value = value;

	this->gb_mem[gb_address] = value;

	if (gb_address == 0xff46)
		oam_dma_copy();

}

//copy the memory to oam region instantly
void GameBoy::oam_dma_copy(void) {

	uint16_t src_addr = gb_mem[0xff46] << 8;
	uint16_t dst_addr = 0xfe00;		//always OAM
	for (int i = 0; i < 160; i++) {
		write(dst_addr++, read(src_addr++));
	}
}