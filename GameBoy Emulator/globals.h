#ifndef GLOBALS_H
#define GLOBALS_H

class GameBoy;
class Renderer;
class Input;
class Ppu;
class Memory;
class Sound;

extern GameBoy* const _gameboy;
extern Renderer* const _renderer;
extern Input* const _input;
extern Ppu* const _ppu;
extern Memory* const _memory;
extern Sound* const _sound;

extern bool _GBC_Mode;

#endif