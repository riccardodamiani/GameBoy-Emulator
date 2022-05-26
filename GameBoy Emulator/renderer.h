#ifndef TILE_H
#define TILE_H

#include <SDL.h>
#include <cstdint>
#include <mutex>
#include <vector>
#include <string>

#include "structures.h"
#include "gameboy.h"

struct IO_map;

namespace {
	const char* paletteItems[] = { "Default", "Original", "Greyscale"};
	const char* windowSizeItems[] = { "2x2", "3x3", "4x4", "5x5", "6x6" };
	const char* gameSpeedItems[] = { "0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x" };
	const char* gbButtonStrings[] = {"a", "b", "start", "select", "left", "right", "up", "down"};
}

class Renderer {
public:
	Renderer();
	void Init(int width, int height);

	void RenderFrame(double elapsedTime);
	double limit_fps(double elapsedTime, double maxFPS);
	void showMessage(std::string message, float time);
	void turnOff();
	void turnOn();
	std::pair <int, int> getWindowPosition();
	std::pair <int, int> getWindowSize();
	void ResizeWindow(int width, int height);
private:
	void renderMessage(float elapsed);
	void imguiFrame(float elapsed);

	void _debug_renderBgMap();
	void _debug_renderWindowMap();
	void _debug_renderBgTiles();

	//window stuff
	SDL_Window* _window;
	SDL_Renderer* _renderer;
	SDL_Texture* windowScreen;

	bool stopped;

	//text stuff
	float messageTimer;
	std::string message;

	int windowWidth, windowHeight;
	int windowPosX, windowPosY;
	float renderScaleX, renderScaleY;

	//imgui stuff
	bool settingsMenu;
	int settingTabs;
	char* windowSizeSelectedItem;
	char* gameSpeedSelectedItem;
	char* paletteSelectedItem;
	bool showMessageBox;
};

#endif
