#include "renderer.h"
#include "structures.h"
#include "errors.h"
#include "gameboy.h"
#include "globals.h"
#include "sound.h"
#include "input.h"
#include "memory.h"
#include "ppu.h"

#include <SDL.h>
#include <thread>
#include <cstdint>
#include <vector>
#include <algorithm>

// Dear ImGui
#include <imgui.h>
#include <imgui_sdl.h>

Renderer::Renderer() {
	
}

std::pair <int, int> Renderer::getWindowPosition() {
	return std::pair <int, int>(windowPosX, windowPosY);
}

std::pair <int, int> Renderer::getWindowSize() {
	return std::pair <int, int>(windowWidth, windowHeight);
}

void Renderer::Init(int width, int height) {

	this->windowWidth = width;
	this->windowHeight = height;
	renderScaleX = (float)width / 160.0;
	renderScaleY = (float)height / 144.0;
	messageTimer = 0;

	//setting manu stuff
	settingsMenu = false;

	settingTabs = 0;
	windowSizeSelectedItem = (char*)windowSizeItems[2];
	gameSpeedSelectedItem = (char*)gameSpeedItems[2];
	paletteSelectedItem = (char*)paletteItems[0];

	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");		//needed otherwise imgui breaks when resizing the window
	_window = SDL_CreateWindow("", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, this->windowWidth, this->windowHeight, 0);
	_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	SDL_SetWindowTitle(this->_window, "Gameboy Emulator");
	SDL_GetWindowPosition(_window, &windowPosX, &windowPosY);
	SDL_SetWindowInputFocus(_window);

	ImGui::CreateContext();
	ImGuiSDL::Initialize(_renderer, windowWidth, windowHeight);

	windowScreen = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 160, 144);
	SDL_SetTextureBlendMode(windowScreen, SDL_BLENDMODE_NONE);

}

//sleeps for the time needed to have a FPS. Returns the time it have slept
double Renderer::limit_fps(double elapsedTime, double maxFPS) {
	if (elapsedTime >= (1.0 / maxFPS)) {
		return 0;
	}
	else {
		std::this_thread::sleep_for(std::chrono::microseconds((long long)(((1.0 / maxFPS) - elapsedTime) * 1000000.0)));
		return ((1.0 / maxFPS) - elapsedTime);
	}
}

void Renderer::ResizeWindow(int width, int height) {
	SDL_SetWindowSize(_window, width, height);
	windowWidth = width;
	windowHeight = height;
	renderScaleX = (float)windowWidth / 160.0;
	renderScaleY = (float)windowHeight / 144.0;
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize.x = static_cast<float>(width);
	io.DisplaySize.y = static_cast<float>(height);
}

void Renderer::showMessage(std::string message, float time) {
	this->messageTimer = time;
	this->message = message;
}

void Renderer::renderMessage(float elapsed) {

	this->messageTimer -= elapsed;
	showMessageBox = (message != "" && messageTimer > 0);
	if (!showMessageBox)
		return;
	int size = std::min((int)message.size() * 9, windowWidth-10);
	ImGui::SetNextWindowPos(ImVec2(windowWidth / 2 - size/2, 20));
	ImGui::SetNextWindowSize(ImVec2(size, 40));
	ImGui::Begin("message", nullptr, ImGuiWindowFlags_NoTitleBar);
	ImGui::TextWrapped(message.c_str());
	ImGui::End();
}

void Renderer::RenderFrame(double elapsedTime) {

	//SDL_GetWindowPosition(_window, &windowPosX, &windowPosY);
	imguiFrame(elapsedTime);

	SDL_SetRenderTarget(_renderer, nullptr);
	SDL_SetRenderDrawBlendMode(this->_renderer, SDL_BLENDMODE_NONE);

	//draw the buffer
	const uint32_t* const screen = _ppu->getBufferToRender();
	int pitch;
	void* pixelBuffer;
	if (SDL_LockTexture(windowScreen, nullptr, &pixelBuffer, &pitch) < 0)
		fatal(FATAL_TEXTURE_LOCKING_FAILED, __func__);
	memcpy(pixelBuffer, screen, 160 * 144 * 4);
	SDL_UnlockTexture(windowScreen);
	SDL_SetRenderTarget(_renderer, nullptr);
	SDL_RenderCopy(_renderer, windowScreen, nullptr, nullptr);

	ImGui::Render();
	ImGuiSDL::Render(ImGui::GetDrawData());

	SDL_RenderPresent(this->_renderer);
	
}

void Renderer::imguiFrame(float elapsed) {
	ImGui::NewFrame();
	if (_input->isMouseInWindow()) {		//hide main menu bar when mouse is outside the window
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("Menu"))
			{
				if (ImGui::MenuItem("Settings"))
				{
					settingsMenu = true;
				}
				if (ImGui::MenuItem("Save state", "F3"))
				{
					_memory->saveCartridgeState();
				}
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}

	if (settingsMenu) {
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
		ImGui::Begin("Settings", &settingsMenu, ImGuiWindowFlags_NoCollapse);
		ImGui::SameLine();
		if (ImGui::Button("Sound", ImVec2(windowWidth / 5, 25)))
		{
			settingTabs = 0;
		}
		ImGui::SameLine();

		if (ImGui::Button("Gameboy", ImVec2(windowWidth / 5, 25)))
		{
			settingTabs = 1;
		}
		ImGui::SameLine();
		if (ImGui::Button("Joypad", ImVec2(windowWidth / 5, 25)))
		{
			settingTabs = 2;
		}
		ImGui::SameLine();
		if (ImGui::Button("Window", ImVec2(windowWidth / 5, 25)))
		{
			settingTabs = 3;
		}

		if (settingTabs == 0) {		//sound settings
			ImGui::Checkbox("Enable sound", _sound->getSoundEnable());
		}
		else if (settingTabs == 1) {		//gameboy settings
			if (ImGui::BeginCombo("Game speed", gameSpeedSelectedItem))
			{
				for (int n = 0; n < IM_ARRAYSIZE(gameSpeedItems); n++)
				{
					bool is_selected = (gameSpeedSelectedItem == gameSpeedItems[n]);
					if (ImGui::Selectable(gameSpeedItems[n], is_selected)) {		//set new selected item
						gameSpeedSelectedItem = (char*)gameSpeedItems[n];
						_gameboy->setClockSpeed(0.5 + n * 0.25);
					}
					if (is_selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			if (ImGui::BeginCombo("Palette", paletteSelectedItem))
			{
				for (int n = 0; n < IM_ARRAYSIZE(paletteItems); n++)
				{
					bool is_selected = (paletteSelectedItem == paletteItems[n]);
					if (ImGui::Selectable(paletteItems[n], is_selected)) {		//set new selected item
						paletteSelectedItem = (char*)paletteItems[n];
						_ppu->setPalette(n);
					}
					if (is_selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		else if (settingTabs == 2) {		//keyboard settings
			ImGui::BeginTable("Keyboard map", 3);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::Text("Button");
			ImGui::TableSetColumnIndex(1); ImGui::Text("Key 1");
			ImGui::TableSetColumnIndex(2); ImGui::Text("Key 2");
			SDL_Scancode* map = _input->getKeyboardMap();

			for (int row = 0; row < 8; row++) {		//create keyboard map table
				ImGui::TableNextRow();
				for (int column = 0; column < 3; column++) {
					ImGui::TableSetColumnIndex(column);
					if (column == 0) {
						ImGui::Text(gbButtonStrings[row]);
					}
					else {
						int index = row + (column - 1) * 8;
						ImGui::PushID(index);	//avoid problems when two buttons have the same keyName
						SDL_Keycode key = SDL_GetKeyFromScancode(map[index]);
						const char* keyName = SDL_GetKeyName(key);
						if (ImGui::Button(keyName)) {		//wants to change a key
							_input->changingKeyboardMap(index);
						}
						ImGui::PopID();
					}
				}
			}
			ImGui::EndTable();
		}
		else if (settingTabs == 3) {		//window settings

			if (ImGui::BeginCombo("Window Size", windowSizeSelectedItem))
			{
				for (int n = 0; n < IM_ARRAYSIZE(windowSizeItems); n++)
				{
					bool is_selected = (windowSizeSelectedItem == windowSizeItems[n]);
					if (ImGui::Selectable(windowSizeItems[n], is_selected)) {
						windowSizeSelectedItem = (char*)windowSizeItems[n];	//set new selected item
						this->ResizeWindow(160 * (n + 2), 144 * (n + 2));
					}
					if (is_selected) {
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
		}
		ImGui::SetCursorPos(ImVec2(0, windowHeight - 20)); // Move cursor on needed positions
		if (ImGui::Button("Save and Close")) {
			_input->saveKeyboardMap();
			settingsMenu = false;
		}
		ImGui::End();
	}
	renderMessage(elapsed);
	ImGui::EndFrame();
}
