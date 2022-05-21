#include "renderer.h"
#include "structures.h"
#include "errors.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <thread>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "structures.h"
#include "gameboy.h"
#include "globals.h"

Renderer::Renderer() {
	
}

void Renderer::Init(int width, int height) {

	this->windowWidth = width;
	this->windowHeight = height;
	renderScaleX = (float)width / 160.0;
	renderScaleY = (float)height / 144.0;
	messageTimer = 0;
	messageTexture = nullptr;

	this->vram = _gameboy->getVram();
	this->io = _gameboy->getIOMap();
	this->oam = _gameboy->getOam();

	ioCopy = (IO_map*)calloc(256, 1);
	ioTemp = (IO_map*)calloc(256, 1);
	memset(ioCopy, 0, 256);
	memset(vramCopy, 0, 0x2000);
	memset(oamCopy, 0, 256);
	memset(ioTemp, 0, 256);
	memset(vramTemp, 0, 0x2000);
	memset(oamTemp, 0, 256);

	_window = SDL_CreateWindow("", 80, 80, this->windowWidth, this->windowHeight, 0);
	_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
	SDL_SetWindowTitle(this->_window, "Gameboy Emulator");

	//texture tiles for 3 palettes
	this->bg_tiles = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 192);
	obj_tiles_1 = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 192);
	obj_tiles_2 = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 192);
	SDL_SetTextureBlendMode(bg_tiles, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(obj_tiles_1, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(obj_tiles_2, SDL_BLENDMODE_BLEND);

	//bg map
	bg_map = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 256, 256);
	SDL_SetTextureBlendMode(bg_map, SDL_BLENDMODE_BLEND);

	window_map = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 256, 256);

	TTF_Init();
	this->font = TTF_OpenFont("Fonts\\Roboto-Regular.ttf", 72);
	if (!font)
		std::cout << "Warning: Couldn't init ttf font." << std::endl;
}

//copy the vram and oam memory from the gameboy
//to a internal memory
void Renderer::UpdateGraphics(scanlineStat* frameStat) {

	memMutex.lock();
	memcpy(vramTemp, vram, 0x2000);
	memcpy(oamTemp, oam, 256);
	memcpy(ioTemp, io, 256);
	memcpy(frameStatTemp, frameStat, sizeof(frameStatTemp));
	memMutex.unlock();
}

//calculates an hash of the tiles memory region (0x8000 - 0x9800) and
//reload the memory if has changed
void Renderer::checkAndReload() {
	memMutex.lock();

	//update internal buffers
	memcpy(vramCopy, vramTemp, 0x2000);
	memcpy(oamCopy, oamTemp, 256);
	memcpy(ioCopy, ioTemp, 256);
	memcpy(frameStatCopy, frameStatTemp, sizeof(frameStatCopy));

	//reloads tiles memory if necessary
	uint64_t hash = 14695981039346656037;
	uint64_t* array = (uint64_t*)vramCopy;

	for (int i = 0; i < 768; i++) {
		hash *= 1099511628211;
		hash ^= array[i];
	}
	hash *= 1099511628211;
	hash ^= ioCopy->BGP | (ioCopy->OBP0 << 8) | (ioCopy->OBP1 << 16);
	if (hash != tilesHash) {
		updateTiles();
		tilesHash = hash;
	}
	memMutex.unlock();
}


void Renderer::turnOff() {
	stopped = true;
}

void Renderer::turnOn() {
	stopped = false;
}

void Renderer::showMessage(std::string message, float time) {
	this->messageTimer = time;
	
	SDL_Surface* surface = TTF_RenderText_Shaded(font, message.c_str(), {20, 20, 20, 200}, {230, 230, 230, 230});
	//SDL_Surface* surface = TTF_RenderText_Solid(font, message.c_str(), { 50, 50, 50, 200 });
	messageTexture = SDL_CreateTextureFromSurface(_renderer, surface);
	double scale = 1;
	messageRect.w = surface->w / 2;
	messageRect.h = surface->h / 2;
	
	if (messageRect.w > windowWidth-20) {
		scale = (double)messageRect.w / (windowWidth - 20);
		messageRect.w /= scale;
		messageRect.h /= scale;
	}
	messageRect.x = (windowWidth / 2) - (surface->w / scale) * 0.25;
	messageRect.y = (windowHeight / 10);

	SDL_FreeSurface(surface);

}

void Renderer::renderMessage() {
	if (messageTexture != nullptr && messageTimer > 0) {
		SDL_SetRenderTarget(_renderer, nullptr);
		SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_BLEND);
		SDL_SetTextureBlendMode(messageTexture, SDL_BLENDMODE_BLEND);
		SDL_RenderCopy(_renderer, messageTexture, nullptr, &messageRect);
	}
}

void Renderer::RenderFrame(double elapsedTime) {

	this->messageTimer -= elapsedTime;

	SDL_SetRenderTarget(_renderer, nullptr);
	SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);

	checkAndReload();
	if (!(this->io->LCDC & 0x80) || stopped) {		//ldc disabled or gameboy is stopped
		SDL_SetRenderDrawColor(this->_renderer, 224, 248, 208, 255);
		SDL_RenderClear(this->_renderer);
		SDL_RenderPresent(this->_renderer);
		renderMessage();
		return;
	}

	//clear background
	uint8_t color_index = ioCopy->BGP & 0x3;
	SDL_Color color = gb_screen_palette[color_index];
	SDL_SetRenderDrawColor(this->_renderer, color.r, color.g, color.b, 255);
	SDL_RenderClear(this->_renderer);

	//split the screen into rectangles with same LCD registers
	std::vector<scanlineStat> frameRect;
	int lastRectStart = 0;
	for (int i = 1; i < 144; i++) {
		if (!(frameStatCopy[i].lcdc == frameStatCopy[i - 1].lcdc &&		//some values changed mid frame
			frameStatCopy[i].WX == frameStatCopy[i - 1].WX &&
			frameStatCopy[i].WY == frameStatCopy[i - 1].WY &&
			frameStatCopy[i].SCX == frameStatCopy[i - 1].SCX &&
			frameStatCopy[i].SCY == frameStatCopy[i - 1].SCY)) {

			frameRect.push_back(frameStatCopy[i - 1]);		//create a rectangle with previous values
			frameRect.back().firstScanline = lastRectStart;
			frameRect.back().height = i - lastRectStart;
			lastRectStart = i;
		}
	}
	frameRect.push_back(frameStatCopy[143]);
	frameRect.back().firstScanline = lastRectStart;
	frameRect.back().height = 144 - lastRectStart;

	//render stuff
	renderSpritesWithPriority(frameRect, 1);	//behind background
	renderBg(frameRect);
	renderWindow(frameRect);
	renderSpritesWithPriority(frameRect, 0);		//above background
	renderMessage();
	//_debug_renderBgMap();
	//_debug_renderWindowMap();
	//_debug_renderBgTiles();

	SDL_RenderPresent(this->_renderer);
}

void Renderer::renderBg(std::vector<scanlineStat> &frameRect) {

	//render all bg rectangles
	for (int i = 0; i < frameRect.size(); i++) {

		if (!(frameRect[i].lcdc & 0x1)) {		//background/window disabled
			continue;
		}

		SDL_SetTextureBlendMode(bg_map, SDL_BLENDMODE_NONE);
		SDL_SetRenderTarget(_renderer, bg_map);
		SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);

		if (frameRect[i].lcdc & 0x10) {		//4th bit in LCDC: tiles counting methods
			render_8000_method(frameRect[i].lcdc & 0x8);
		}
		else {
			render_9000_method(frameRect[i].lcdc & 0x8);
		}
		//render the background to screen
		SDL_SetRenderTarget(_renderer, nullptr);
		SDL_SetTextureBlendMode(bg_map, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_BLEND);

		//the screen is inside the background
		if ((frameRect[i].SCX + 160 < 256) && (frameRect[i].SCY +
			frameRect[i].firstScanline + frameRect[i].height < 256)) {
			SDL_Rect bgRect = { frameRect[i].SCX,
				frameRect[i].SCY + frameRect[i].firstScanline,
				160, frameRect[i].height };
			SDL_Rect screenRect = { 0, frameRect[i].firstScanline * renderScaleY,
				windowWidth, frameRect[i].height * renderScaleY };
			SDL_RenderCopy(_renderer, bg_map, &bgRect, &screenRect);
		}
		else {
			//only x wrapping
			if (!(frameRect[i].SCX + 160 < 256) && (frameRect[i].SCY +
				frameRect[i].firstScanline + frameRect[i].height < 256)) {

				int leftRectWidth = 256 - frameRect[i].SCX;
				SDL_Rect leftRectBg = { frameRect[i].SCX,
					frameRect[i].SCY + frameRect[i].firstScanline,
					leftRectWidth, frameRect[i].height };
				SDL_Rect rightRectBg = { 0,
					frameRect[i].SCY + frameRect[i].firstScanline,
					160 - leftRectWidth, frameRect[i].height };
				SDL_Rect leftRectScreen = { 0,
					frameRect[i].firstScanline * renderScaleY,
					leftRectWidth * renderScaleX,
					frameRect[i].height * renderScaleY };
				SDL_Rect rightRectScreen = { leftRectWidth * renderScaleX,
					frameRect[i].firstScanline * renderScaleY,
					(160 - leftRectWidth) * renderScaleX,
					frameRect[i].height * renderScaleY };
				SDL_RenderCopy(_renderer, bg_map, &leftRectBg, &leftRectScreen);
				SDL_RenderCopy(_renderer, bg_map, &rightRectBg, &rightRectScreen);
			}
			else	//only y wrapping
			if ((frameRect[i].SCX + 160 < 256) && !(frameRect[i].SCY +
					frameRect[i].firstScanline + frameRect[i].height < 256)) {
				int x = 160;
				int y1 = 256 - frameRect[i].SCY - frameRect[i].firstScanline;
				int y2 = frameRect[i].height - y1;
				SDL_Rect Rect1_bg = { frameRect[i].SCX, frameRect[i].SCY + frameRect[i].firstScanline, x, y1};
				SDL_Rect Rect2_bg = { frameRect[i].SCX , 0, x, y2};
				SDL_Rect Rect1_screen = {0, frameRect[i].firstScanline* renderScaleY, 
					x* renderScaleX, y1* renderScaleY };
				SDL_Rect Rect2_screen = { 0, (y1 + frameRect[i].firstScanline)* renderScaleY, 
					x* renderScaleX, y2* renderScaleY };
				SDL_RenderCopy(_renderer, bg_map, &Rect1_bg, &Rect1_screen);
				SDL_RenderCopy(_renderer, bg_map, &Rect2_bg, &Rect2_screen);
			}
			else {	//both
				int x1 = 256 - frameRect[i].SCX;
				int x2 = frameRect[i].SCX - 96;
				int y1 = 256 - frameRect[i].firstScanline - frameRect[i].SCY;
				int y2 = frameRect[i].height - y1;

				SDL_Rect Rect1Bg = { frameRect[i].SCX, frameRect[i].SCY + frameRect[i].firstScanline, x1, y1};
				SDL_Rect Rect2Bg = { 0, frameRect[i].SCY + frameRect[i].firstScanline, x2, y1 };
				SDL_Rect Rect3Bg = { 0, 0, x2, y2 };
				SDL_Rect Rect4Bg = { frameRect[i].SCX, 0, x1, y2 };
				SDL_Rect Rect1_screen = {0, frameRect[i].firstScanline* renderScaleY,
					x1* renderScaleX, y1* renderScaleY };
				SDL_Rect Rect2_screen = { x1*renderScaleX, frameRect[i].firstScanline* renderScaleY,
					x2* renderScaleX, y1* renderScaleY };
				SDL_Rect Rect3_screen = { x1*renderScaleX, (y1 + frameRect[i].firstScanline)* renderScaleY,
					x2* renderScaleX, y2 * renderScaleY };
				SDL_Rect Rect4_screen = { 0, (y1 + frameRect[i].firstScanline)* renderScaleY,
					x1* renderScaleX, y2* renderScaleY };
				SDL_RenderCopy(_renderer, bg_map, &Rect1Bg, &Rect1_screen);
				SDL_RenderCopy(_renderer, bg_map, &Rect2Bg, &Rect2_screen);
				SDL_RenderCopy(_renderer, bg_map, &Rect3Bg, &Rect3_screen);
				SDL_RenderCopy(_renderer, bg_map, &Rect4Bg, &Rect4_screen);
			}
		}
	}
}

void Renderer::_debug_renderBgTiles() {
	SDL_SetTextureBlendMode(bg_tiles, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);
	SDL_Rect r = { 0, 0, (192.0 / windowHeight) * windowWidth, windowHeight };
	SDL_RenderCopy(_renderer, bg_tiles, nullptr, &r);
}

void Renderer::_debug_renderWindowMap() {

	SDL_SetTextureBlendMode(window_map, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(_renderer, window_map);
	SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);

	if (ioCopy->LCDC & 0x10) {		//5th bit in LCDC: tiles counting methods
		render_8000_method(ioCopy->LCDC & 0x40);
	}
	else {
		render_9000_method(ioCopy->LCDC & 0x40);
	}

	SDL_SetRenderTarget(_renderer, nullptr);
	SDL_RenderCopy(_renderer, window_map, nullptr, nullptr);
}

void Renderer::_debug_renderBgMap() {

	SDL_SetTextureBlendMode(bg_map, SDL_BLENDMODE_NONE);
	SDL_SetRenderTarget(_renderer, bg_map);
	SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);

	if (ioCopy->LCDC & 0x10) {		//4th bit in LCDC: tiles counting methods
		render_8000_method(ioCopy->LCDC & 0x8);
	}
	else {
		render_9000_method(ioCopy->LCDC & 0x8);
	}

	SDL_SetRenderTarget(_renderer, nullptr);
	SDL_RenderCopy(_renderer, bg_map, nullptr, nullptr);
}

void Renderer::renderWindow(std::vector<scanlineStat>& frameRect) {

	// render all bg rectangles
	for (int i = 0; i < frameRect.size(); i++) {

		if (!(frameRect[i].lcdc & 0x1))		//background/window disabled
			continue;
	
		if (!(frameRect[i].lcdc & 0x20))		//window disabled
			continue;

		//window is out of the screen
		if ((frameRect[i].firstScanline + frameRect[i].height < frameRect[i].WY) ||
			frameRect[i].WX >= 0xa7)
			continue;

		SDL_SetTextureBlendMode(window_map, SDL_BLENDMODE_NONE);
		SDL_SetRenderTarget(_renderer, window_map);
		SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);

		if (frameRect[i].lcdc & 0x10) {		//5th bit in LCDC: tiles counting methods
			render_8000_method(frameRect[i].lcdc & 0x40);
		}
		else {
			render_9000_method(frameRect[i].lcdc & 0x40);
		}

		//render the window to screen
		SDL_SetRenderTarget(_renderer, nullptr);

		int screenX = frameRect[i].WX - 7;
		int screenY = (frameRect[i].WY > frameRect[i].firstScanline ? frameRect[i].WY : frameRect[i].firstScanline);
		int screenHeight = ((frameRect[i].WY > frameRect[i].firstScanline) ? 
			(frameRect[i].height - (frameRect[i].WY - frameRect[i].firstScanline)) : 
			frameRect[i].height);
		int windowY = (frameRect[i].firstScanline > frameRect[i].WY ?
			(frameRect[i].firstScanline - frameRect[i].WY) : 
			0);
		SDL_Rect screenRect = { screenX * renderScaleX, screenY * renderScaleY,
			255 * renderScaleX , screenHeight * renderScaleY };
		SDL_Rect windowRect = {0, windowY, 255, screenHeight };
		SDL_RenderCopy(_renderer, window_map, &windowRect, &screenRect);
	}
}

//simple bubble sort for x position
void sort(sprite_attribute** buffer, int len) {
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

void Renderer::renderSpritesWithPriority(std::vector<scanlineStat>& frameRect, int priority) {
	SDL_SetRenderTarget(_renderer, nullptr);
	SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_BLEND);	//use transparency
	SDL_SetTextureBlendMode(obj_tiles_1, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(obj_tiles_2, SDL_BLENDMODE_BLEND);

	sprite_attribute* sprites[40];
	for (int i = 0; i < 40; i++) sprites[i] = &((sprite_attribute*)oamCopy)[i];
	sort(sprites, 40);

	if (!(ioCopy->LCDC & 0x4)) {		//8x8 tiles
		for (int i = 39; i >= 0; --i) {
			sprite_attribute& sprite = *sprites[i];
			if (sprite.y_pos <= 8 || sprite.y_pos >= 160 || //invalid sprite position
				sprite.x_pos == 0 || sprite.x_pos >= 168 ||
				sprite.priority != priority)
				continue;

			//check whether the sprites are visible
			bool show = false;
			int yPos = sprite.y_pos - 16;
			int xPos = sprite.x_pos - 8;
			for (int i = 0; i < frameRect.size(); i++) {
				if ((yPos >= frameRect[i].firstScanline &&
					yPos <= frameRect[i].firstScanline + frameRect[i].height) ||
					(yPos < 0 && yPos > -8)) {
					if (frameRect[i].lcdc & 0x2)
						show = true;
					break;
				}
			}
			if (!show) continue;

			//draw
			uint8_t tileNum = sprite.tile;

			SDL_Rect sourceRect = { (tileNum % 16) * 8, (tileNum / 16) * 8, 8, 8 };
			SDL_Rect destRect = { xPos * renderScaleX, yPos * renderScaleY,
				8 * renderScaleX, 8 * renderScaleY };
			SDL_RendererFlip flip = (SDL_RendererFlip)(((int)SDL_FLIP_HORIZONTAL * sprite.x_flip) |
				((int)SDL_FLIP_VERTICAL * sprite.y_flip));

			//use the correct tile map
			SDL_Texture* obj_tiles = sprite.palette == 0 ? obj_tiles_1 : obj_tiles_2;
			SDL_RenderCopyEx(_renderer, obj_tiles, &sourceRect, &destRect, 0, nullptr, flip);
		}
		return;
	}

	//8x16 tiles
	for (int i = 39; i >= 0; --i) {
		sprite_attribute& sprite = *sprites[i];
		if (sprite.y_pos == 0 || sprite.y_pos >= 160 || //invalid sprite position
			sprite.x_pos == 0 || sprite.x_pos >= 168 ||
			sprite.priority != priority)
			continue;
		uint8_t tileNum = sprite.tile;

		//check whether the sprites are visible
		bool show = false;
		int yPos = sprite.y_pos - 16;
		int xPos = sprite.x_pos - 8;
		for (int i = 0; i < frameRect.size(); i++) {
			if ((yPos >= frameRect[i].firstScanline &&
				yPos <= frameRect[i].firstScanline + frameRect[i].height) || 
				(yPos < 0 && yPos > -16)) {		//needed to draw sprites that starts before the first rectangle
				if (frameRect[i].lcdc & 0x2)
					show = true;
				break;
			}
		}
		if (!show) continue;

		//draw
		SDL_Rect sourceRect1 = { (tileNum % 16) * 8, (tileNum / 16) * 8, 8, 8 };
		SDL_Rect destRect1 = { xPos * renderScaleX, yPos * renderScaleY,
			8 * renderScaleX, 8 * renderScaleY };
		tileNum++;	//next sprite
		//uint8_t yPos = sprite->y_pos + 8;
		SDL_Rect sourceRect2 = { (tileNum % 16) * 8, (tileNum / 16) * 8, 8, 8 };
		SDL_Rect destRect2 = { xPos  * renderScaleX, (yPos + 8) * renderScaleY,
			8 * renderScaleX, 8 * renderScaleY };

		SDL_RendererFlip flip = (SDL_RendererFlip)(((int)SDL_FLIP_HORIZONTAL * sprite.x_flip) |
			((int)SDL_FLIP_VERTICAL * sprite.y_flip));

		//use the correct tile map
		SDL_Texture* obj_tiles = sprite.palette == 0 ? obj_tiles_1 : obj_tiles_2;
		if (!sprite.y_flip) {
			SDL_RenderCopyEx(_renderer, obj_tiles, &sourceRect1, &destRect1, 0, nullptr, flip);
			SDL_RenderCopyEx(_renderer, obj_tiles, &sourceRect2, &destRect2, 0, nullptr, flip);
		}
		else {	//swap the screen rectangles
			SDL_RenderCopyEx(_renderer, obj_tiles, &sourceRect1, &destRect2, 0, nullptr, flip);
			SDL_RenderCopyEx(_renderer, obj_tiles, &sourceRect2, &destRect1, 0, nullptr, flip);
		}
	}
}

//in this mode the tile number is a unsigned value that count the tiles starting 
//from address 0x8000 of gb vram (from pixel 0 in tiles texture)
void Renderer::render_8000_method(int tile_data_select) {

	uint32_t tileMapAddr = (tile_data_select ? 0x1c00 : 0x1800);		//memory section for tile map
	SDL_SetTextureBlendMode(bg_tiles, SDL_BLENDMODE_NONE);

	for (int row = 0; row < 32; row++) {
		for (int col = 0; col < 32; col++) {
			uint8_t tileNum = vramCopy[tileMapAddr + row * 32 + col];

			int tilePos = row * 32 + col;
			SDL_Rect sourceRect = { (tileNum % 16) * 8, (tileNum / 16) * 8, 8, 8 };
			SDL_Rect destRect = { (tilePos % 32) * 8, (tilePos / 32) * 8, 8, 8 };
			SDL_RenderCopy(_renderer, bg_tiles, &sourceRect, &destRect);
		}
	}

	SDL_SetTextureBlendMode(bg_tiles, SDL_BLENDMODE_BLEND);
}

//in this mode the tile number is a signed value that count the tiles starting 
//from address 0x9000 of gb vram (from line 128 in tiles texture)
void Renderer::render_9000_method(int tile_data_select) {

	uint32_t tileMapAddr = (tile_data_select ? 0x1c00 : 0x1800);		//memory section for tile map

	for (int row = 0; row < 32; row++) {
		for (int col = 0; col < 32; col++) {
			short tileNum = ((char)vramCopy[tileMapAddr + row * 32 + col]) + 256;

			int tilePos = row * 32 + col;
			SDL_Rect sourceRect = { (tileNum % 16) * 8, (tileNum / 16) * 8, 8, 8 };
			SDL_Rect destRect = { (tilePos % 32) * 8, (tilePos / 32) * 8, 8, 8 };
			SDL_RenderCopy(_renderer, bg_tiles, &sourceRect, &destRect);
		}
	}

}

//sleeps for the time needed to have a FPS. Returns the time it have sleeped
double Renderer::limit_fps(double elapsedTime, double maxFPS) {
	if (elapsedTime >= (1.0 / maxFPS)) {
		return 0;
	}
	else {
		std::this_thread::sleep_for(std::chrono::microseconds((long long)(((1.0 / maxFPS) - elapsedTime) * 1000000.0)));
		return ((1.0 / maxFPS) - elapsedTime);
	}
}

//transform vram data into textures drawable by sdl
void Renderer::updateTiles(){
	double x = 255.0 / 3;
	uint32_t* bg_pixels, *obj1_pixels,* obj2_pixels;
	bg_pixels = (uint32_t*)malloc(128 * 192 * 4);
	obj1_pixels = (uint32_t*)malloc(128 * 192 * 4);
	obj2_pixels = (uint32_t*)malloc(128 * 192 * 4);

	for (int tileRowCounter = 0; tileRowCounter < 24; tileRowCounter++) {
		
		SDL_Color pixel;
		
		for (int h = 0; h < 16; h++) {
			uint16_t gb_addr = (tileRowCounter * 16 + h) * 16;

			for (int i = 0; i < 8; i++) {
				for (int j = 0; j < 8; j++) {
					int index = i * 128 + j + h * 8 + tileRowCounter * 1024;

					uint8_t color_nr = ((vramCopy[gb_addr + i * 2] >> (7 - j)) & 0x1) |
						(((vramCopy[gb_addr + i * 2 + 1] >> (7 - j)) << 1) & 0x2);

					//background tiles
					uint8_t color = (ioCopy->BGP >> (color_nr * 2)) & 0x3;	

					//white pixels are considered transparent in the background for render priority
					pixel = gb_screen_palette[color];
					pixel.a = (color_nr == 0x0 ? 0 : 0xff);
					((uint32_t*)bg_pixels)[index] = ((pixel.a << 24) | (pixel.r << 16) | (pixel.g << 8) | pixel.b);

					//obj palette 1 tiles
					color = (ioCopy->OBP0 >> (color_nr * 2)) & 0x3;
					pixel = gb_screen_palette[color];
					pixel.a = (color_nr == 0x0 ? 0 : 0xff);
					((uint32_t*)obj1_pixels)[index] = ((pixel.a << 24) | (pixel.r << 16) | (pixel.g << 8) | pixel.b);

					//obj palette 2 tiles
					color = (ioCopy->OBP1 >> (color_nr * 2)) & 0x3;
					pixel = gb_screen_palette[color];
					pixel.a = (color_nr == 0x0 ? 0 : 0xff);
					((uint32_t*)obj2_pixels)[index] = ((pixel.a << 24) | (pixel.r << 16) | (pixel.g << 8) | pixel.b);
				}
			}
		}
	}

	void* pixelBuffer;
	int pitch;

	SDL_SetTextureBlendMode(bg_tiles, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(obj_tiles_1, SDL_BLENDMODE_NONE);
	SDL_SetTextureBlendMode(obj_tiles_2, SDL_BLENDMODE_NONE);

	if(SDL_LockTexture(bg_tiles, nullptr, &pixelBuffer, &pitch) < 0)
		fatal(FATAL_TEXTURE_LOCKING_FAILED, __func__);
	memcpy(pixelBuffer, bg_pixels, 128 * 192 * 4);
	SDL_UnlockTexture(bg_tiles);

	if(SDL_LockTexture(obj_tiles_1, nullptr, &pixelBuffer, &pitch) < 0)
		fatal(FATAL_TEXTURE_LOCKING_FAILED, __func__);
	memcpy(pixelBuffer, obj1_pixels, 128 * 192 * 4);
	SDL_UnlockTexture(obj_tiles_1);

	if(SDL_LockTexture(obj_tiles_2, nullptr, &pixelBuffer, &pitch) < 0)
		fatal(FATAL_TEXTURE_LOCKING_FAILED, __func__);
	memcpy(pixelBuffer, obj2_pixels, 128 * 192 * 4);
	SDL_UnlockTexture(obj_tiles_2);
	
	free(bg_pixels);
	free(obj1_pixels);
	free(obj2_pixels);
}