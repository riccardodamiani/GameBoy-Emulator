#ifndef PPU_H
#define PPU_H

#include <cstdint>
#include <mutex>
#include "structures.h"

namespace {
	SDL_Color gb_screen_palette[4] = {
		{224, 248, 208, 255},
		{136, 192, 112, 255},
		{52, 104, 86, 255},
		{8, 24, 32, 255}
	};
};

class Ppu {
public:
	Ppu();
	void Init();
	void drawScanline(int cycles);
	const uint32_t* const getBufferToRender();
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
};

#endif
