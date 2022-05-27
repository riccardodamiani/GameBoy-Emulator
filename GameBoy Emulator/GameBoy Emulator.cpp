
#include <iostream>
#include <SDL.h>
#include <thread>
#include <algorithm>
#include <Windows.h>

#include "gameboy.h"
#include "renderer.h"
#include "sound.h"
#include "input.h"
#include "memory.h"
#include "globals.h"
#include "ppu.h"


void mainRoutine() {
    double elapsedTime = 0;
    while (1) {
        auto startTime = std::chrono::high_resolution_clock::now();

        _input->beginNewFrame();
        _renderer->RenderFrame(elapsedTime);
        _gameboy->runFor(4194 * 16.67);

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        elapsedTime = elapsed.count();	//elapsed time in seconds

        elapsedTime += _renderer->limit_fps(elapsedTime, 60);
    }
}

#undef main
int main(int argc, char** argv)
{

    std::string filename;
#ifdef _DEBUG
    ShowWindow(GetConsoleWindow(), SW_SHOW);
    filename = "..\\..\\games\\Legend of Zelda, The - Link's Awakening (USA, Europe).gb";
#else
    ShowWindow(GetConsoleWindow(), SW_SHOW);
    std::cout << "Drop the rom file here: ";
    std::getline(std::cin, filename);
    filename.erase(std::remove(filename.begin(), filename.end(), '"'), filename.end());
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif 

    _memory->Init(filename.c_str());
    _input->Init();
    _ppu->Init();
    _gameboy->Init();
    _renderer->Init(160 * 4, 144 * 4);

    mainRoutine();

    return 0;
}
