# Gameboy Emulator
Open-source Gameboy emulator written in c++. 

## Build requirements
SDL2, SDL audio and SDL font libraries.

## To do
- Implement support for MBC3+TIMER, MBC2, MBC4, MBC5 memory bank controllers
- Serial communication
- Settings menu

## Known bugs
- Donkey kong Land graphics is all screwed up.
- Audio issues in some game (i.e. notes that last longer than expected, distortion, ecc..).
- FF Legend III: game freezes when entering battle simulator.


## Keyboard map
For now the keys can't be changed.
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
| Game 									| State 		|
|---------------------------------------|---------------|
| Donkey kong 							| Playable 		|
| Donkey kong Land 						| Unplayable	|
| Legend of Zelda (Link's awakening)	| Playable		|
| Pokemon Red							| Great			|
| Super Mario Land						| Playable		|
| Super Mario Land 2					| Playable		|
| Wario Land - Super Mario Land 3 		| Playable		|
| Super Mario Land 4 					| Playable		|
| Kirby's Dream Land					| Playable		|
| Kirby's Dream Land 2 					| Playable		|
| Tetris (World)						| Playable		|
| Space Invaders						| Playable		|
| Metroid II 							| Playable		|
| FF Legend III							| Not sure		|

**Perfect:** self explanatory  
**Great:** Has minor bugs or glitches  
**Fine:** Has some major bugs or glitches, but the game is still completable  
**Unplayable:** Has major bugs or glitches that don't allow to complete the game  
**Playable:** Game not fully tested but as far as I know is playable  
