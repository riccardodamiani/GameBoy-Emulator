#ifndef PPU_H
#define PPU_H

#include <cstdint>
#include <mutex>
#include "structures.h"

namespace {
	SDL_Color gb_palettes[][4] = {
		{
			{224, 248, 208, 255},	//default palette
			{136, 192, 112, 255},
			{52, 104, 86, 255},
			{8, 24, 32, 255}
		},
		{
			{135, 130, 10, 255},		//original palette
			{100, 125, 60, 255},
			{55, 90, 75, 255},
			{35, 70, 60, 255}
		},
		{
			{255, 255, 255, 255},		//greyscale palette
			{170, 170, 170, 255},
			{85, 85, 85, 255},
			{0, 0, 0, 255}
		}
	};
};

class Ppu {
public:
	Ppu();
	void Init();
	void drawScanline(int cycles);
	const uint32_t* const getBufferToRender();
	void setPalette(int nr);
private:
	void sort(sprite_attribute** buffer, int len);
	void drawBuffer(IO_map* io);
	void drawSprite(sprite_attribute *sprite, IO_map* io, uint32_t* scanlineBuffer);
	void drawBackground(IO_map* io, uint32_t* scanlineBuffer);
	void clearScanline(IO_map* io);
	void clearScreen();
	void disable();
	void enable();
	ppu_registers registers;
	uint32_t screenBuffers[2][23040];		//screen buffers with pixel format rgba
	uint32_t* tempBuffer;	//used to provide a copy of the buffer to render to the renderer
	int activeBuffer;		//index of the buffer being modified
	uint8_t* vram;
	std::mutex bufferMutex;
	SDL_Color* dmg_palette;

	int paletteNr;
	bool updatePalette;
};

#endif
