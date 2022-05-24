#ifndef GLOBALS_H
#define GLOBALS_H

class GameBoy;
class Renderer;
class Input;
class Ppu;
class Memory;

extern GameBoy* const _gameboy;
extern Renderer* const _renderer;
extern Input* const _input;
extern Ppu* const _ppu;
extern Memory* const _memory;

#endif