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
	void updateReg(uint16_t address, uint8_t val);
	void Init();
private:
	void update_channel1_counter(io_sound_pulse_channel* ch1);
	void update_channel2_counter(io_sound_pulse_channel *ch2);
	void update_channel3_counter(io_sound_wave_channel *ch3);
	void update_channel4_counter(io_sound_noise_channel *ch4);
	void trigger_channel1(io_sound_pulse_channel* ch1);
	void trigger_channel2(io_sound_pulse_channel *ch2);
	void trigger_channel3(io_sound_wave_channel* ch3);
	void trigger_channel4(io_sound_noise_channel *ch4);
	void channel1_register_write(uint16_t addr, uint8_t val, io_sound_pulse_channel* ch1);
	void channel2_register_write(uint16_t addr, uint8_t val, io_sound_pulse_channel* ch2);
	void channel3_register_write(uint16_t addr, uint8_t val, io_sound_wave_channel* ch3);
	void channel4_register_write(uint16_t addr, uint8_t val, io_sound_noise_channel* ch4);
	sound_pulse_data channel1;
	sound_pulse_data channel2;
	sound_wave_data channel3;
	sound_noise_data channel4;
	bool enableSound;
};

#endif
