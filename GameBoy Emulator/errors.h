#ifndef ERRORS_H
#define ERRORS_H

#include <string>
#include <iostream>

#define FATAL_ROM_NOT_FOUND 0
#define FATAL_BOOT_ROM_NOT_FOUND 1
#define FATAL_INVALID_BOOT_ROM_SIZE 2
#define FATAL_INSTRUCTION_NOT_IMPLEMENTED 3
#define FATAL_UNSUPPORTED_MBC_CHIP 4
#define FATAL_SDL_AUDIO_INIT_FAILED 5
#define FATAL_TILE_SIZE_NOT_SUPPORTED 6
#define FATAL_UNMATCHING_ROM_SIZE 7
#define FATAL_UNSUPPORTED_ROM_SIZE 8
#define FATAL_ROM_NOT_DMG_COMPATIBLE 9
#define FATAL_HEADER_CHECKSUM_DO_NOT_MATCH 10
#define FATAL_TEXTURE_LOCKING_FAILED 11
#define FATAL_INVALID_RAM_SIZE 12
#define FATAL_INVALID_OPCODE 13
namespace {
	const char* fatal_errors[] = {
		"Rom file not found",
		"Boot rom file not found",
		"Invalid boot rom size",
		"Instruction not implemented",
		"Unsupported cardridge type",
		"SDL_OpenAudio failed",
		"Tile map size 8x16 is not supported",
		"File size and rom size do not match",
		"Unsupported rom size",
		"The rom is not DMG compatible",
		"Unmatching header checksum",
		"Texture locking failed",
		"Invalid ram size",
		"Invalid opcode"
	};
}

void fatal(int error_code, std::string func_name, std::string info = "");

#endif

