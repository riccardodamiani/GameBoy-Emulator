# Gameboy Emulator
Open-source Gameboy/Gameboy Color emulator written in C++. It uses SDL2 for graphics and SDL Mixer for audio. In the current state a lot of games runs without major issues, however note that this is just a project made for fun and perfect emulation is not my main goal.

## Building requirements
[SDL2](https://libsdl.org/download-2.0.php), [SDL2 Mixer](https://www.libsdl.org/projects/SDL_mixer/), [ImGui](https://github.com/ocornut/imgui) and [imgui_sdl](https://github.com/Tyyppi77/imgui_sdl).


## Not yet implemented
- MBC4 memory bank controller
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
| Tetris 								| Great 		|		|
| Tetris DX (GBC) 						| Great 		| 		|
| Donkey kong Land 1/2/3				| Playable 		|  		|
| Donkey kong Country (GBC)				| Playable 		| 		|
| F-1 Race 								| Playable 		| 		|
| Faceball 2000 						| Playable 		| Wave audio channel missing |
| Ferrari Grand Prix Challenge			| Playable 		| 		|
| Legend of Zelda (Link's awakening)	| Great			|		|
| Legend of Zelda (Oracle of Ages) (GBC)| Playable 		| 		|
| Pokemon Red							| Great			|		|
| Pokemon Crystal (GBC)					| Playable 		|		|
| Super Mario Land 1-4					| Playable		|		|
| Super Mario Bros. Delux (GBC)			| Playable 		|		|
| Kirby's Dream Land					| Playable		|		|
| Kirby's Dream Land 2 					| Playable		|		|
| Metroid II 							| Playable		|		|
| FF Legend III							| Fine			| Freezes when entering battle simulator |

**Great:** Runs without any noticeable issues  
**Fine:** Runs with some bugs or glitches  
**Playable:** Game not tested in depth but as far as I know is playable  
**Unplayable:** Has major bugs or glitches that don't allow to play the game  

