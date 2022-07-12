#include "structures.h"
#include "ppu.h"
#include "memory.h"
#include "globals.h"
#include "gameboy.h"

#include <mutex>
#include <malloc.h>

Ppu::Ppu() {
	updatePalette = false;
}

void Ppu::Init() {

	dmg_palette = gb_palettes[0];
	vram[0] = _memory->getVramBank0();
	vram[1] = _memory->getVramBank1();

	//used to provide a copy of the buffer to render to the renderer
	tempBuffer = (uint32_t*)malloc(23040 * 4);
}

void Ppu::setPalette(int nr) {
	updatePalette = true;
	paletteNr = nr;
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
	if (_GBC_Mode) {
		for (int i = 0; i < 160 * 144; i++) {
			screenBuffers[0][i] = 0xffffffff;
			screenBuffers[1][i] = 0xffffffff;
		}
		bufferMutex.unlock();
		return;
	}

	for (int i = 0; i < 160 * 144; i++) {
		memcpy(&screenBuffers[0][i], &dmg_palette[0], 4);
		memcpy(&screenBuffers[1][i], &dmg_palette[0], 4);
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
	if (registers.sl_cnt > 456) {	//enter H-Blank
		registers.sl_cnt -= 456;
		io->LY++;
		registers.spritesLoaded = 0;
		registers.bufferDrawn = 0;

		if(io->LY < 144)	//H-Blank in V-Draw
			_memory->transfer_hdma();

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
				findScanlineSprites((sprite_attribute*)oam, io);
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

void Ppu::createWindowScanline(priority_pixel* windowScanline, IO_map* io) {

	for (int i = 0; i < 160; i++) {
		windowScanline[i].trasparent = 1;
	}

	if (!(io->LCDC & 0x20) || (_GBC_Mode && !(io->LCDC & 0x1))) {		//window disabled
		return;
	}
	if (io->LY < io->WY || io->WX > 166) {		//not shown
		return;
	}

	background_attribute bg_att = {};

	//memory section for window tile map
	uint32_t tileMapAddr = ((io->LCDC & 0x40) ? 0x1c00 : 0x1800);

	uint8_t pixelRow = (io->LY - io->WY) % 8;
	uint8_t mapRow = (io->LY - io->WY) / 8;
	for (uint8_t screenX = std::max(io->WX - 7, 0); screenX < 160; screenX++) {
		uint8_t tileMapX = screenX - io->WX + 7;
		short tileNum;
		if (io->LCDC & 0x10) {		//4th bit in LCDC: tiles counting methods
			tileNum = vram[0][tileMapAddr + mapRow * 32 + tileMapX / 8];
		}
		else {
			tileNum = (char)vram[0][tileMapAddr + mapRow * 32 + tileMapX / 8] + 256;
		}
		//get the background tile attribute (only in gbc mode)
		if (_GBC_Mode) memcpy((void*)&bg_att, &vram[1][tileMapAddr + mapRow * 32 + tileMapX / 8], 1);

		//get the pointer to the tile memory
		uint8_t* tileMem = &vram[bg_att.vram_bank][tileNum * 16];

		//find out the color
		int col = tileMapX % 8;
		uint8_t color_nr = ((tileMem[pixelRow * 2] >> (7 - col)) & 0x1) |
			(((tileMem[pixelRow * 2 + 1] >> (7 - col)) << 1) & 0x2);

		if (_GBC_Mode) {
			SDL_Color pixel = _memory->getBackgroundColor(bg_att.bg_palette, color_nr);

			//draw the pixel
			memcpy(&windowScanline[screenX].color, &pixel, 4);
			windowScanline[screenX].trasparent = 0;
			continue;
		}

		uint8_t color = (io->BGP >> (color_nr * 2)) & 0x3;
		SDL_Color pixel = dmg_palette[color];

		//draw the pixel
		memcpy(&windowScanline[screenX].color, &pixel, 4);
		windowScanline[screenX].trasparent = 0;
	}

}

void Ppu::createSpriteScanline(priority_pixel* scanline, IO_map* io) {

	//initialize the scanline as transparent
	for (int i = 0; i < 160; i++) {
		scanline[i].trasparent = 1;
	}
	if (!(io->LCDC & 0x2))		//sprites are disabled
		return;

	//go throught all sprites from lower priority
	for (int i = 9; i >= 0; i--) {
		if (registers.scanlineSprites[i] == nullptr)
			continue;
		drawSprite(registers.scanlineSprites[i], io, scanline);
	}
}

void Ppu::findScanlineBgTiles(IO_map* io) {
	
	short y = (io->SCY + io->LY) % 256;

	uint8_t firstTileGridX = io->SCX / 8;
	uint8_t firstTileGridY = y / 8;
	uint8_t firstTilePixelX = io->SCX % 8;
	uint8_t firstTilePixelY = y % 8;

	short tileNum;
	uint8_t tileGridX;
	uint32_t tileMapAddr = ((io->LCDC & 0x8) ? 0x1c00 : 0x1800);
	
	for (int i = 0; i < 21; i++) {
		//get the x position of the next tile in the 32x32 tiles grid
		tileGridX = (firstTileGridX + i) % 32;

		//get the tile number
		if (io->LCDC & 0x10) {		//4th bit in LCDC: tiles counting methods
			tileNum = vram[0][tileMapAddr + firstTileGridY * 32 + tileGridX];
		}
		else {
			tileNum = (char)vram[0][tileMapAddr + firstTileGridY * 32 + tileGridX] + 256;
		}

		//get tile attributes
		background_attribute tile_attr = {};
		if (_GBC_Mode) memcpy((void*)&tile_attr, &vram[1][tileMapAddr + firstTileGridY * 32 + tileGridX], 1);

		//get the pointer to the tile memory
		uint8_t* tileMem = &vram[tile_attr.vram_bank][tileNum * 16];

		//copy the tile memory and the attributes to the registers
		registers.backgroundTiles[i].tile_attr = tile_attr;
		memcpy(registers.backgroundTiles[i].tile_mem, tileMem, 16);
		flipTile(registers.backgroundTiles[i]);
	}

	//set the position of the first tile on screen
	registers.firstBgTilePixelX = firstTilePixelX;
	registers.firstBgTilePixelY = firstTilePixelY;
}

void Ppu::findScanlineSprites(sprite_attribute* oam, IO_map* io) {

	sprite_attribute* sprites[40];
	memset(registers.scanlineSprites, 0, sizeof(registers.scanlineSprites));
	for (int i = 0; i < 40; i++) sprites[i] = &oam[i];
	if (!_GBC_Mode) sort(sprites, 40);
	int spriteSize = ((io->LCDC & 0x4) ? 16 : 8);
	int j = 0;
	for (int i = 0; i < 40; i++) {
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

void Ppu::drawBuffer(IO_map* io) {
	uint32_t* buffer = screenBuffers[activeBuffer];

	//clear the scanline
	uint32_t* scanlineBuffer = &buffer[io->LY * 160];
	if (!_GBC_Mode) {
		for (int i = 0; i < 160; i++) {
			memcpy(&scanlineBuffer[i], &dmg_palette[io->BGP & 0x3], 4);
		}
	}
	else {
		for (int i = 0; i < 160; i++) {
			scanlineBuffer[i] = 0xffffffff;
		}
	}
	priority_pixel spriteScanline[160], bgScanline[160], windowScanline[160];

	//create scanline buffers
	createBackgroundScanline(bgScanline, io);
	createWindowScanline(windowScanline, io);
	createSpriteScanline(spriteScanline, io);

	for (int i = 0; i < 160; i++) {

		if (spriteScanline[i].trasparent) {
			scanlineBuffer[i] = bgScanline[i].color;
			//window pixels
			if (!windowScanline[i].trasparent) {
				scanlineBuffer[i] = windowScanline[i].color;
			}
			continue;
		}

		//if background has priority, the transparency is considered
		if ((bgScanline[i].priority | spriteScanline[i].priority) && !bgScanline[i].trasparent) {
			scanlineBuffer[i] = bgScanline[i].color;
			//window pixels
			if (!windowScanline[i].trasparent) {
				scanlineBuffer[i] = windowScanline[i].color;
			}
			continue;
		}

		//window pixels
		if (!windowScanline[i].trasparent) {
			scanlineBuffer[i] = windowScanline[i].color;
		}

		//draw sprite
		scanlineBuffer[i] = spriteScanline[i].color;

	}
	
}

void Ppu::createBackgroundScanline(priority_pixel* scanline, IO_map*io) {
	if (!(io->LCDC & 0x1)) {		//background/window disabled
		//set background layer transparent
		for (int i = 0; i < 160; i++) {
			scanline[i].trasparent = 1;
		}
		return;
	}

	findScanlineBgTiles(io);

	for (int i = 0; i < 160; i++) {
		uint8_t bgIndex = (registers.firstBgTilePixelX + i) / 8;
		background_tile& bgTile = registers.backgroundTiles[bgIndex];

		int row = registers.firstBgTilePixelY;
		int col = (registers.firstBgTilePixelX + i) % 8;
		uint8_t color_nr = ((bgTile.tile_mem[row * 2] >> (7 - col)) & 0x1) |
			(((bgTile.tile_mem[row * 2 + 1] >> (7 - col)) << 1) & 0x2);
		scanline[i].trasparent = (color_nr == 0);		//transparent
		scanline[i].priority = bgTile.tile_attr.bg_oam_priority;

		if (_GBC_Mode) {
			SDL_Color pixel = _memory->getBackgroundColor(bgTile.tile_attr.bg_palette, color_nr);

			//draw the pixel
			memcpy(&scanline[i], &pixel, 4);
			continue;
		}

		uint8_t color = (io->BGP >> (color_nr * 2)) & 0x3;
		SDL_Color pixel = dmg_palette[color];

		//draw the pixel
		memcpy(&scanline[i], &pixel, 4);
	}

}


uint8_t Ppu::reverse(uint8_t n) {
	// Reverse the top and bottom nibble then swap them.
	return (reverse_lookup[n & 0b1111] << 4) | reverse_lookup[n >> 4];
}

void Ppu::flipTile(background_tile& tile) {
	if (tile.tile_attr.h_flip) {
		for (int i = 0; i < 16; i++) {
			tile.tile_mem[i] = reverse(tile.tile_mem[i]);
		}
	}
	if (tile.tile_attr.v_flip) {
		for (int i = 0; i < 4; i++) {
			uint16_t t = ((uint16_t*)tile.tile_mem)[i];
			((uint16_t*)tile.tile_mem)[i] = ((uint16_t*)tile.tile_mem)[7 - i];
			((uint16_t*)tile.tile_mem)[7 - i] = t;
		}
	}
}


void Ppu::drawSprite(sprite_attribute* sprite, IO_map* io, priority_pixel* scanlineBuffer) {

	int vram_bank = _GBC_Mode && sprite->vram_bank;
	int spriteSize = 8;
	uint8_t tileMask = 0xff;
	if ((io->LCDC & 0x4)) {
		spriteSize = 16;
		//for 8x16 sprite tiles the lower bit of the tile number is ignored
		tileMask = 0xfe;
	}
	//vertical flip
	int row = io->LY - (sprite->y_pos - 16);
	row = sprite->y_flip ? (spriteSize-1-row) : row;

	int col = sprite->x_pos - 8;
	uint8_t* spriteMem = &vram[vram_bank][(sprite->tile & tileMask) * 16];
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

		if (_GBC_Mode) {
			SDL_Color pixel = _memory->getSpriteColor(sprite->gbc_palette, color_nr);
			//draw the pixel
			memcpy(&scanlineBuffer[col + i].color, &pixel, 4);
			scanlineBuffer[col + i].priority = sprite->priority;
			scanlineBuffer[col + i].trasparent = 0;
			continue;
		}
		color = (palette >> (color_nr * 2)) & 0x3;
		pixel = dmg_palette[color];
		memcpy(&scanlineBuffer[col + i].color, &pixel, 4);
		scanlineBuffer[col + i].priority = sprite->priority;
		scanlineBuffer[col + i].trasparent = 0;
	}
}

//return a copy of the buffer to render. The caller MUST NOT release the memory
const uint32_t * const Ppu::getBufferToRender() {
	bufferMutex.lock();
	uint32_t* buffer = screenBuffers[!activeBuffer];
	memcpy(tempBuffer, buffer, 160 * 144 * 4);		//copy the buffer
	bufferMutex.unlock();

	if (updatePalette) {
		updatePalette = 0;
		if (paletteNr >= 0 && paletteNr < 3) {
			dmg_palette = gb_palettes[paletteNr];
		}
	}
	return tempBuffer;
}