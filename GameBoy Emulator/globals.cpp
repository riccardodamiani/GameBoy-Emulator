#include "globals.h"
#include "gameboy.h"
#include "renderer.h"
#include "input.h"
#include "ppu.h"
#include "memory.h"
#include "sound.h"

namespace {
	GameBoy* t_gb;
	Renderer* t_r;
	Input* t_i;
	Ppu* t_p;
	Memory* t_m;
	Sound* t_s;

	bool Init_all(void) {
		t_gb = new GameBoy();
		t_r = new Renderer();
		t_i = new Input();
		t_p = new Ppu();
		t_m = new Memory();
		t_s = new Sound();
		return true;
	}
	bool a = Init_all();
}

GameBoy* const _gameboy = t_gb;
Renderer* const _renderer = t_r;
Input* const _input = t_i;
Ppu* const _ppu = t_p;
Memory* const _memory = t_m;
Sound* const _sound = t_s;

