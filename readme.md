# Gameboy Emulator
Open-source Gameboy emulator written in C++. It uses SDL2 for graphics and SDL Mixer for audio. In the current state most games run without major issues, however note that the project is still very much a work in progress and, for now, supports only DMG roms.  
The code is written for Windows, however Windows API are only used for console stuff so it should be easily portable to other platforms as well.


## Building requirements
[SDL2](https://libsdl.org/download-2.0.php), [SDL2 Mixer](https://www.libsdl.org/projects/SDL_mixer/), [ImGui](https://github.com/ocornut/imgui) and [imgui_sdl](https://github.com/Tyyppi77/imgui_sdl).


## Not yet implemented
- MBC3+TIMER, MBC4, memory bank controllers
- Serial communication


## Keyboard map
Keyboard map can be customized under menu/settings.
| GB button 	| Key 1 		| Key 2 		|
|---------------|---------------|---------------|
| a 			| q 			| o 			|
| b 			| e 			| p 			|
| start 		| space 		| space			|
| select		| shift 		| enter 		|
| left 			| a 			| left arrow 	|
| right 		| d 			| right arrow 	|
| up			| w 			| up arrow		|
| down 			| s 			| down arrow	|
| save state 	| f3 			| f3 			|



## Games tested
| Game 									| State 		| Bugs |
|---------------------------------------|---------------|-------|
| Donkey kong 							| Great 		|		|
| Donkey kong Land 						| Playable 		|  		|
| Donkey kong Land 2 					| Playable 		|     	|
| Donkey kong Land 3 					| Playable 		|    	|
| F-1 Race 								| Playable 		| 		|
| Faceball 2000 						| Playable 		| Wave audio channel doesn't work |
| Ferrari Grand Prix Challenge			| Playable 		| 		|
| Legend of Zelda (Link's awakening)	| Great			|		|
| Pokemon Red							| Great			|		|
| Super Mario Land						| Great			|		|
| Super Mario Land 2					| Fine			|  		|
| Wario Land - Super Mario Land 3 		| Playable		|		|
| Super Mario Land 4 					| Playable		|		|
| Kirby's Dream Land					| Fine			|		|
| Kirby's Dream Land 2 					| Fine			|		|
| Tetris (World)						| Great			|		|
| Space Invaders						| Great			|		|
| Metroid II 							| Playable		|		|
| FF Legend III							| Fine			| Game freezes when entering battle simulator |

**Great:** Runs without any noticeable issues  
**Fine:** Runs with some bugs or glitches  
**Playable:** Game not tested in depth but as far as I know is playable  
**Unplayable:** Has major bugs or glitches that don't allow to play the game  

