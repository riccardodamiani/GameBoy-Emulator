#ifndef TILE_H
#define TILE_H

#include <SDL.h>
#include <SDL_ttf.h>
#include <cstdint>
#include <mutex>
#include <vector>
#include <string>

#include "structures.h"
#include "gameboy.h"

struct IO_map;
struct scanlineStat;

namespace {
	SDL_Color gb_screen_palette[4] = {
		{224, 248, 208, 0},
		{136, 192, 112, 0},
		{52, 104, 86, 0},
		{8, 24, 32, 0}
	};
}

class Renderer {
public:
	Renderer();
	void Init(int width, int height);

	void UpdateGraphics(scanlineStat* frameStat);
	void RenderFrame(double elapsedTime);
	double limit_fps(double elapsedTime, double maxFPS);
	void showMessage(std::string message, float time);
	void turnOff();
	void turnOn();
	std::pair <int, int> getWindowPosition();
	std::pair <int, int> getWindowSize();
private:
	void updateTiles();
	void checkAndReload();
	void render_8000_method(int tile_data_select);
	void render_9000_method(int tile_data_select);
	void renderSpritesWithPriority(std::vector<scanlineStat> &frameRect, int priority);
	void renderBg(std::vector<scanlineStat>& frameRect);
	void renderWindow(std::vector<scanlineStat>& frameRect);
	void renderMessage();

	void _debug_renderBgMap();
	void _debug_renderWindowMap();
	void _debug_renderBgTiles();

	//window stuff
	SDL_Window* _window;
	SDL_Renderer* _renderer;
	SDL_Texture* bg_tiles;
	SDL_Texture* obj_tiles_1;
	SDL_Texture* obj_tiles_2;
	SDL_Texture* bg_map;
	SDL_Texture* window_map;

	uint64_t tilesHash;

	uint8_t vramCopy[0x2000];
	uint8_t oamCopy[256];
	IO_map* ioCopy;
	scanlineStat frameStatCopy[144];
	uint8_t vramTemp[0x2000];
	uint8_t oamTemp[256];
	IO_map* ioTemp;
	scanlineStat frameStatTemp[144];

	uint8_t* vram;
	IO_map* io;
	uint8_t* oam;
	std::mutex memMutex;
	bool stopped;

	//text stuff
	float messageTimer;
	SDL_Texture* messageTexture;
	SDL_Rect messageRect;
	TTF_Font* font;

	int windowWidth, windowHeight;
	int windowPosX, windowPosY;
	float renderScaleX, renderScaleY;

	//imgui stuff
	bool settingsMenu;
	int settingTabs;
};

#endif
