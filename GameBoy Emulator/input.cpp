#include "input.h"
#include "structures.h"
#include "globals.h"
#include "gameboy.h"
#include <SDL.h>
#include <imgui.h>
#include <imgui_sdl.h>

Input::Input() {
	
}

void Input::Init() {
	keysMap1 = { SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
		SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_SPACE };

	keysMap2 = { SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_S,
		SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE };

	for (int i = 0; i < _pressedKeys.size(); i++) {
		_pressedKeys[i] = 0;
		_releasedKeys[i] = 0;
		_heldKeys[i] = 0;
	}
}

void Input::beginNewFrame() {
	for (int i = 0; i < _pressedKeys.size(); i++) {
		_pressedKeys[i] = 0;
		_releasedKeys[i] = 0;
	}
	getSDLEvent();

	jp_mutex.lock();
	jp.left = (isKeyHeld(keysMap1.left) || isKeyHeld(keysMap2.left));
	jp.right = (isKeyHeld(keysMap1.right) || isKeyHeld(keysMap2.right));
	jp.up = (isKeyHeld(keysMap1.up) || isKeyHeld(keysMap2.up));
	jp.down = (isKeyHeld(keysMap1.down) || isKeyHeld(keysMap2.down));
	jp.a = (isKeyHeld(keysMap1.a) || isKeyHeld(keysMap2.a));
	jp.b = (isKeyHeld(keysMap1.b) || isKeyHeld(keysMap2.b));
	jp.select = (isKeyHeld(keysMap1.select) || isKeyHeld(keysMap2.select));
	jp.start = (isKeyHeld(keysMap1.start) || isKeyHeld(keysMap2.start));
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
			}
		}
		else if (event.type == SDL_KEYUP) {
			this->keyUpEvent(event);
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
