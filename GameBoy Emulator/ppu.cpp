#include "structures.h"
#include "ppu.h"
#include "memory.h"
#include "globals.h"
#include "gameboy.h"

#include <mutex>
#include <malloc.h>

Ppu::Ppu() {
	
}

void Ppu::Init() {

	dmg_palette = gb_default_palette;
	vram = _memory->getVram();

	//used to provide a copy of the buffer to render to the renderer
	tempBuffer = (uint32_t*)malloc(23040 * 4);
}

//simple bubble sort for x position
void Ppu::sort(sprite_attribute** buffer, int len) {
	int i, j;
	for (i = 0; i < len - 1; i++) {
		for (j = 0; j < len - i - 1; j++) {
			if (buffer[j]->x_pos > buffer[j + 1]->x_pos) {
				sprite_attribute* temp = buffer[j];
				buffer[j] = buffer[j + 1];
				buffer[j + 1] = temp;
			}
		}
	}
}

void Ppu::clearScanline(IO_map* io) {
	uint32_t* buffer = screenBuffers[activeBuffer];

	//clear the scanline
	uint32_t* scanlineBuffer = &buffer[io->LY * 160];
	for (int i = 0; i < 160; i++) {
		memcpy(&scanlineBuffer[i], &dmg_palette[0], 4);
	}
}

void Ppu::clearScreen() {
	bufferMutex.lock();
	for (int i = 0; i < 160*144*2; i++) {
		memcpy(&screenBuffers[0][i], &dmg_palette[0], 4);
	}
	bufferMutex.unlock();
}

void Ppu::disable() {
	if (!registers.enabled)
		return;

	registers.enabled = 0;
	clearScreen();
}

void Ppu::enable() {
	registers.enabled = 1;
}

void Ppu::drawScanline(int clk_cycles){
	IO_map* io = _memory->getIOMap();
	uint8_t* oam = _memory->getOam();

	//ldc disabled
	if (!(io->LCDC & 0x80)) {
		registers.sl_cnt = 0;
		io->LY = 0;
		disable();
		return;
	}
	enable();

	registers.sl_cnt += clk_cycles;
	if (registers.sl_cnt > 456) {
		registers.sl_cnt -= 456;
		io->LY++;
		registers.spritesLoaded = 0;
		registers.bufferDrawn = 0;
		if (io->LY == 144) {	//enter VBlank
			io->IF |= 0x1;
		}
		if (io->LY >= 154) {
			io->LY = 0;
			bufferMutex.lock();
			activeBuffer = !activeBuffer;
			bufferMutex.unlock();
		}
	}
	
	STAT_struct* stat = ((STAT_struct*)(&io->STAT));
	uint8_t stat_s = 0;
	if (io->LY >= 144) {		//VBLANK
		stat->lcd_mode = 1;
	}
	else {
		//MODE 0 (HBlank) - HBlank occurs between lines and allows a 
		//short period of time for adjusting PPU parameters, VRAM, and OAM.
		if (registers.sl_cnt <= 204) {
			stat->lcd_mode = 0;		
		}
		//MODE 2 - scan OAM to find which OBJs are active. During this time OAM is locked.
		else if (registers.sl_cnt <= 284) {
			stat->lcd_mode = 2;
			if (!registers.spritesLoaded) {		//search sprites in oam
				sprite_attribute* sprites[40];
				memset(registers.scanlineSprites, 0, sizeof(registers.scanlineSprites));
				for (int i = 0; i < 40; i++) sprites[i] = &((sprite_attribute*)oam)[i];
				sort(sprites, 40);
				int spriteSize = ((io->LCDC & 0x4) ? 16 : 8);
				int j = 0;
				for (int i = 39; i >= 0; i--) {
					int yPos = sprites[i]->y_pos - 16;
					if (io->LY >= yPos &&
						(io->LY < yPos + spriteSize)) {
						registers.scanlineSprites[j++] = sprites[i];
						if (j >= 10)	//max 10 sprites per scanline
							break;
					}
				}
				registers.spritesLoaded = 1;
			}
		}
		else {
			stat->lcd_mode = 3;		//MODE 3 (HDraw) - drawing the pixels. No OAM and vram access outside
			if (registers.bufferDrawn == 0) {
				drawBuffer(io);
				registers.bufferDrawn = 1;
			}
		}
	}
	
	//set LY coincidence flag
	if (io->LY == io->LYC) {
		stat->ly_cf = 1;
	}
	else stat->ly_cf = 0;

	//STAT LY coincidence
	if (stat->ly_c && stat->ly_cf) {
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
	if (registers.stat_signal == 0 && stat_s == 1) {
		io->IF |= 0x2;
	}
	registers.stat_signal = stat_s;

}

void Ppu::drawBuffer(IO_map* io) {
	uint32_t* buffer = screenBuffers[activeBuffer];

	//clear the scanline
	uint32_t* scanlineBuffer = &buffer[io->LY * 160];
	for (int i = 0; i < 160; i++) {	
		memcpy(&scanlineBuffer[i], &dmg_palette[io->BGP&0x3], 4);
	}

	//sprites behind background
	for (int i = 0; i < 10; i++) {
		if (registers.scanlineSprites[i] == nullptr)
			continue;
		if (registers.scanlineSprites[i]->priority) {
			drawSprite(registers.scanlineSprites[i], io, scanlineBuffer);
		}
	}
	drawBackground(io, scanlineBuffer);
	//sprites above background
	for (int i = 0; i < 10; i++) {
		if (registers.scanlineSprites[i] == nullptr)
			continue;
		if (!registers.scanlineSprites[i]->priority) {
			drawSprite(registers.scanlineSprites[i], io, scanlineBuffer);
		}
	}
}

void Ppu::drawBackground(IO_map* io, uint32_t* scanlineBuffer) {
	if (!(io->LCDC & 0x1)) {		//background/window disabled
		return;
	}

	//memory section for background tile map
	uint32_t tileMapAddr = ((io->LCDC & 0x8) ? 0x1c00 : 0x1800);		
	uint8_t pixelRow = (io->LY + io->SCY) % 8;
	uint8_t mapRow = (io->LY + io->SCY) / 8;
	if (mapRow > 31) mapRow %= 32;		//wrap the y around
	for (uint8_t screenX = 0; screenX < 160; screenX++) {
		uint8_t tileMapX = screenX + io->SCX;
		short tileNum;
		if (io->LCDC & 0x10) {		//4th bit in LCDC: tiles counting methods
			tileNum = vram[tileMapAddr + mapRow * 32 + tileMapX / 8];
		}
		else {
			tileNum = (char)vram[tileMapAddr + mapRow * 32 + tileMapX / 8] + 256;
		}
		uint8_t* tileMem = &vram[tileNum * 16];
		int col = tileMapX % 8;
		uint8_t color_nr = ((tileMem[pixelRow * 2] >> (7 - col)) & 0x1) |
			(((tileMem[pixelRow * 2 + 1] >> (7 - col)) << 1) & 0x2);
		if (color_nr == 0)		//transparent
			continue;
		uint8_t color = (io->BGP >> (color_nr * 2)) & 0x3;
		SDL_Color pixel = dmg_palette[color];
		memcpy(&scanlineBuffer[screenX], &pixel, 4);
	}

	if (!(io->LCDC & 0x20))		//window disabled
		return;
	if (io->LY < io->WY || io->WX < 7 || io->WX > 152)		//not shown
		return;

	//memory section for window tile map
	tileMapAddr = ((io->LCDC & 0x40) ? 0x1c00 : 0x1800);
	
	pixelRow = (io->LY - io->WY) % 8;
	mapRow = (io->LY - io->WY) / 8;
	for (uint8_t screenX = io->WX - 7; screenX < 160; screenX++) {
		uint8_t tileMapX = screenX - io->WX + 7;
		short tileNum;
		if (io->LCDC & 0x10) {		//4th bit in LCDC: tiles counting methods
			tileNum = vram[tileMapAddr + mapRow * 32 + tileMapX / 8];
		}
		else {
			tileNum = (char)vram[tileMapAddr + mapRow * 32 + tileMapX / 8] + 256;
		}
		uint8_t* tileMem = &vram[tileNum * 16];
		int col = tileMapX % 8;
		uint8_t color_nr = ((tileMem[pixelRow * 2] >> (7 - col)) & 0x1) |
			(((tileMem[pixelRow * 2 + 1] >> (7 - col)) << 1) & 0x2);
		uint8_t color = (io->BGP >> (color_nr * 2)) & 0x3;
		SDL_Color pixel = dmg_palette[color];
		memcpy(&scanlineBuffer[screenX], &pixel, 4);
	}
}

void Ppu::drawSprite(sprite_attribute* sprite, IO_map* io, uint32_t* scanlineBuffer) {

	if (!(io->LCDC & 0x2))		//sprites are disabled
		return;

	int spriteSize = ((io->LCDC & 0x4) ? 16 : 8);
	//vertical flip
	int row = io->LY - (sprite->y_pos - 16);
	row = sprite->y_flip ? (spriteSize-1-row) : row;

	int col = sprite->x_pos - 8;
	uint8_t* spriteMem = &vram[sprite->tile * 16];
	SDL_Color pixel;
	uint8_t color;
	uint8_t palette = (sprite->palette ? io->OBP1 : io->OBP0);
	
	for (int i = 0; i < 8; i++) {
		int pixelCol = sprite->x_flip ? (7-i) : i;		//horizontal flip
		if (col + i < 0 || col + i > 159)	//not inside the screen
			continue;
		uint8_t color_nr = ((spriteMem[row * 2] >> (7 - pixelCol)) & 0x1) |
			(((spriteMem[row * 2 + 1] >> (7 - pixelCol)) << 1) & 0x2);
		if (color_nr == 0)		//transparent
			continue;
		color = (palette >> (color_nr * 2)) & 0x3;
		pixel = dmg_palette[color];
		memcpy(&scanlineBuffer[col + i], &pixel, 4);
	}
}

//return a copy of the buffer to render. The caller MUST NOT release the memory
const uint32_t * const Ppu::getBufferToRender() {
	bufferMutex.lock();
	uint32_t* buffer = screenBuffers[!activeBuffer];
	memcpy(tempBuffer, buffer, 160 * 144 * 4);		//copy the buffer
	bufferMutex.unlock();
	return tempBuffer;
}