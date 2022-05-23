# Gameboy Emulator
Open-source Gameboy emulator written in C++. It uses SDL2 for graphics and SDL Mixer for audio. In the current state a lot of the most common games run without major issues, however note that the project is still very much a work in progress and, for now, supports only DMG games.  
The code is written for Windows, however Windows API are only used for console stuff so it should be easily portable to other platforms as well.


## Building requirements
SDL2, SDL2_Mixer and imgui.


## Not yet implemented
- MBC3+TIMER, MBC2, MBC4, MBC5 memory bank controllers
- Serial communication
- Settings menu


## Keyboard map
Keyboard map can't be customized yet.
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
| Donkey kong Land 						| Unplayable	| Major graphical issues |
| Legend of Zelda (Link's awakening)	| Great			|		|
| Pokemon Red							| Great			|		|
| Super Mario Land						| Great			|		|
| Super Mario Land 2					| Fine			| Audio channel 2 is distorted |
| Wario Land - Super Mario Land 3 		| Playable		|		|
| Super Mario Land 4 					| Playable		|		|
| Kirby's Dream Land					| Fine			| Audio wave channel missing |
| Kirby's Dream Land 2 					| Fine			| Audio is somewhat wrong |
| Tetris (World)						| Great			|		|
| Space Invaders						| Great			|		|
| Metroid II 							| Playable		|		|
| FF Legend III							| Fine			| Game freezes when entering battle simulator |

**Great:** Runs without any noticeable issues  
**Fine:** Runs with some bugs or glitches  
**Playable:** Game not tested in depth but as far as I know is playable  
**Unplayable:** Has major bugs or glitches that don't allow to play the game  

