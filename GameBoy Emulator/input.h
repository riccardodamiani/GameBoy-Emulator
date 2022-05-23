#ifndef INPUT_H
#define INPUT_H

#include "structures.h"

#include <mutex>
#include <SDL.h>
#include <array>

class Input {
public:
	Input();
	void Init();
	void beginNewFrame();
	joypad getJoypadState(void);
	//returns the state of a key or a mouse button
	bool wasKeyPressed(SDL_Scancode key);
	bool wasKeyReleased(SDL_Scancode key);
	bool isKeyHeld(SDL_Scancode key);
	bool isMouseInWindow();
	SDL_Scancode *getKeyboardMap();
	void changingKeyboardMap(int keyIndex);
	void saveKeyboardMap();
	bool loadKeyboardMap();
private:
	void keyUpEvent(const SDL_Event& event);
	void keyDownEvent(const SDL_Event& event);
	void getSDLEvent();

	std::array <bool, SDL_Scancode::SDL_NUM_SCANCODES> _heldKeys;
	std::array <bool, SDL_Scancode::SDL_NUM_SCANCODES> _pressedKeys;
	std::array <bool, SDL_Scancode::SDL_NUM_SCANCODES> _releasedKeys;
	std::mutex jp_mutex;
	bool mouseInsideWindow;
	bool updatingKbMap;
	int keyIndex;

	joypad jp;
	joypad_map keysMap[2];
};

#endif
