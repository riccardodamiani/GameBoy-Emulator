#ifndef SOUND_H
#define SOUND_H

#include <SDL_mixer.h>
#include "structures.h"

class Sound {
public:
	Sound();
	void UpdateSound(IO_map* io);
	void Halt();
	bool* getSoundEnable();
private:
	sound_pulse_data channel1;
	sound_pulse_data channel2;
	sound_wave_data channel3;
	sound_noise_data channel4;
	bool enableSound;
};

#endif
