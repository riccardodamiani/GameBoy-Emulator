#include "input.h"
#include "structures.h"
#include "globals.h"
#include "gameboy.h"
#include <SDL.h>
#include <imgui.h>
#include <imgui_sdl.h>
#include <iostream>
#include <fstream>

Input::Input() {
	
}

void Input::Init() {

	if (!loadKeyboardMap()) {
		keysMap[0] = { SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_SPACE, SDL_SCANCODE_LSHIFT,
			SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN };

		keysMap[1] = { SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN,
		SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S };
	}

	for (int i = 0; i < _pressedKeys.size(); i++) {
		_pressedKeys[i] = 0;
		_releasedKeys[i] = 0;
		_heldKeys[i] = 0;
	}
	updatingKbMap = false;
	keyIndex = 0;
}

SDL_Scancode* Input::getKeyboardMap() {
	return (SDL_Scancode*)&keysMap[0];
}

void Input::changingKeyboardMap(int keyIndex) {
	updatingKbMap = true;
	this->keyIndex = keyIndex;
}

void Input::saveKeyboardMap() {
	std::ofstream file("config.dat", std::ios::binary);
	file.write((char*)&keysMap[0], 16*4);
	file.close();
}

bool Input::loadKeyboardMap() {
	std::ifstream file("config.dat", std::ios::binary);
	if (!file.is_open())
		return false;
	file.read((char*)&keysMap[0], 16 * 4);
	file.close();
	return true;
}

void Input::beginNewFrame() {
	for (int i = 0; i < _pressedKeys.size(); i++) {
		_pressedKeys[i] = 0;
		_releasedKeys[i] = 0;
	}
	getSDLEvent();

	jp_mutex.lock();
	jp.left = (isKeyHeld(keysMap[0].left) || isKeyHeld(keysMap[1].left));
	jp.right = (isKeyHeld(keysMap[0].right) || isKeyHeld(keysMap[1].right));
	jp.up = (isKeyHeld(keysMap[0].up) || isKeyHeld(keysMap[1].up));
	jp.down = (isKeyHeld(keysMap[0].down) || isKeyHeld(keysMap[1].down));
	jp.a = (isKeyHeld(keysMap[0].a) || isKeyHeld(keysMap[1].a));
	jp.b = (isKeyHeld(keysMap[0].b) || isKeyHeld(keysMap[1].b));
	jp.select = (isKeyHeld(keysMap[0].select) || isKeyHeld(keysMap[1].select));
	jp.start = (isKeyHeld(keysMap[0].start) || isKeyHeld(keysMap[1].start));
	jp_mutex.unlock();

	if (wasKeyReleased(SDL_SCANCODE_F3)) {
		_gameboy->saveState();
	}

}

//get a input event and convert it into a sdl event
void Input::getSDLEvent() {

	ImGuiIO& io = ImGui::GetIO();
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_QUIT) {
			exit(EXIT_SUCCESS);
			return;
		}
		else if (event.type == SDL_KEYDOWN) {	//keyboard input
			if (event.key.repeat == 0) {
				this->keyDownEvent(event);
				if (updatingKbMap) {
					updatingKbMap = false;
					((SDL_Scancode*)&keysMap[0])[keyIndex] = event.key.keysym.scancode;
				}
			}
		}
		else if (event.type == SDL_KEYUP) {
			this->keyUpEvent(event);
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN) {
			updatingKbMap = false;
		}
	}

	int mouseX, mouseY;
	const int buttons = SDL_GetMouseState(&mouseX, &mouseY);
	// Setup low-level inputs (e.g. on Win32, GetKeyboardState(), 
	io.DeltaTime = 1.0f / 60.0f;
	io.MousePos = ImVec2(static_cast<float>(mouseX), static_cast<float>(mouseY));
	io.MouseDown[0] = buttons & SDL_BUTTON(SDL_BUTTON_LEFT);
	io.MouseDown[1] = buttons & SDL_BUTTON(SDL_BUTTON_RIGHT);
	//io.MouseWheel = static_cast<float>(wheel);

	//check whether the mouse is inside the window (used to show/hide menu bar)
	std::pair <int, int> size = _renderer->getWindowSize();
	mouseInsideWindow = (mouseX > 0) && (mouseX < size.first-10) &&
		(mouseY > 0) && (mouseY < size.second-10);

	return;
}

bool Input::isMouseInWindow() {
	return mouseInsideWindow;
}


joypad Input::getJoypadState(void) {
	jp_mutex.lock();
	joypad ret = jp;
	jp_mutex.unlock();

	return ret;
}

//gets called when a key is pressed
void Input::keyDownEvent(const SDL_Event& event) {

	this->_pressedKeys[event.key.keysym.scancode] = true;
	this->_heldKeys[event.key.keysym.scancode] = true;
}

//gets called when a key is released
void Input::keyUpEvent(const SDL_Event& event) {

	this->_releasedKeys[event.key.keysym.scancode] = true;
	this->_heldKeys[event.key.keysym.scancode] = false;
}

//checks if a certain key was pressed during the current frame
bool Input::wasKeyPressed(SDL_Scancode key) {
	return this->_pressedKeys[key];
}

//checks if a certain key was released during the current frame
bool Input::wasKeyReleased(SDL_Scancode key) {
	return this->_releasedKeys[key];
}

//checks if a certain key is currently being held
bool Input::isKeyHeld(SDL_Scancode key) {
	return this->_heldKeys[key];
}
