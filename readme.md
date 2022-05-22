# Gameboy Emulator
Open-source Gameboy emulator written in C++ for Windows. It uses SDL2 for graphics and SDL Mixer for audio. The project is still very much a work in progress and, for now, supports only DMG games. Windows api are only used for console stuff so it should be easily portable to other platforms.


## Build requirements
SDL2, SDL audio and SDL font libraries.


## Not yet implemented
- MBC3+TIMER, MBC2, MBC4, MBC5 memory bank controllers
- Serial communication
- Settings menu


## Keyboard map
Keyboard map can't be customed yet.
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
| Donkey kong 							| Playable 		|		|
| Donkey kong Land 						| Unplayable	| Major graphical issues |
| Legend of Zelda (Link's awakening)	| Playable		|		|
| Pokemon Red							| Great			|		|
| Super Mario Land						| Playable		|		|
| Super Mario Land 2					| Playable		| Audio channel 2 is distorted |
| Wario Land - Super Mario Land 3 		| Playable		|		|
| Super Mario Land 4 					| Playable		|		|
| Kirby's Dream Land					| Playable		| Audio wave channel missing |
| Kirby's Dream Land 2 					| Playable		| Audio is somewhat wrong |
| Tetris (World)						| Playable		|		|
| Space Invaders						| Playable		|		|
| Metroid II 							| Playable		|		|
| FF Legend III							| Playable		| Game freezes when entering battle simulator |

**Great:** Completable without any noticeable issue  
**Fine:** Completable with some bugs or glitches  
**Unplayable:** Has major bugs or glitches that don't allow to complete the game  
**Playable:** Game not fully tested but as far as I know is playable  
