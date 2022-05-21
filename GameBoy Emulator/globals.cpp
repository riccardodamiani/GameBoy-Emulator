#include "globals.h"
#include "gameboy.h"
#include "renderer.h"
#include "input.h"

namespace {
	GameBoy* t_gb;
	Renderer* t_r;
	Input* t_i;

	bool Init_all(void) {
		t_gb = new GameBoy();
		t_r = new Renderer();
		t_i = new Input();
		return true;
	}
	bool a = Init_all();
}

GameBoy* const _gameboy = t_gb;
Renderer* const _renderer = t_r;
Input* const _input = t_i;


