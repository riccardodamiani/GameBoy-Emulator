#include "sound.h"
#include "structures.h"
#include "errors.h"
#include "memory.h"
#include "globals.h"

#include <math.h>
#include <iostream>
#include <SDL.h>
#include <SDL_audio.h>
#include <SDL_mixer.h>
#include <malloc.h>

#define SAMPLE_RATE 44100

#define DEFAULT_VOLUME 200.0
#define CHANNEL1_VOLUME 150.0
#define CHANNEL2_VOLUME 150.0
#define CHANNEL3_VOLUME 250.0
#define CHANNEL4_VOLUME 120.0


void ch1_callback(int channel, void* stream, int len, void* udata) {
    sound_pulse_data& data( *((sound_pulse_data *)udata));
    uint16_t* audio = (uint16_t*)stream;

    memset(stream, 0, len);

    if (!data.trigger) {
        return;
    }

    for (int i = 0; i < len / 512; i++) {
        float freq = 4194304.0 / ((float)((2048 - (data.frequency_reg)) << 6));
        float period = SAMPLE_RATE / freq;
        float on = period * data.duty_cycle;
        for (int j = 0; j < 256; j++) {
       
            float t = fmod(data.sound_chunk_counter, period);

            //ugly square wave with duty cycle
            if (t < on)
                audio[i * 256 + j] = data.volume * CHANNEL1_VOLUME;
            else
                audio[i * 256 + j] = 0;
              

            data.sound_chunk_counter++;
        }
        //frequency sweep
        data.freq_sweep_update_timer += 256.0 / SAMPLE_RATE;
        if (data.freq_sweep_timer != 0 && data.freq_sweep_update_timer > data.freq_sweep_timer) {
            data.freq_sweep_update_timer -= data.freq_sweep_timer;
            data.frequency_reg += data.freq_sweep_amount;
            if (data.frequency_reg + data.freq_sweep_amount > 0x7ff || 
                data.frequency_reg + data.freq_sweep_amount <= 0) {
                data.trigger = 0;
                return;
            }
        }
        //volume sweep
        data.vol_sweep_update_timer += 256.0 / SAMPLE_RATE;
        if (data.vol_sweep_step_len != 0 && data.vol_sweep_update_timer >= data.vol_sweep_step_len) {
            data.vol_sweep_update_timer -= data.vol_sweep_step_len;
            
            data.volume += data.vol_sweep_dir;
            if (data.volume <= 0) {
                data.trigger = 0;
                return;
            }
            if (data.volume > 0xf)
                data.volume = 0xf;
        }
        //sound len counter
        data.sound_timer += 256.0 / SAMPLE_RATE;
        if (data.len_counter_enable && data.sound_timer >= data.sound_len) {
            data.trigger = 0;
            return;
        }
    }
}

void ch2_callback(int channel, void* stream, int len, void* udata) {
    sound_pulse_data& data(*((sound_pulse_data*)udata));
    uint16_t* audio = (uint16_t*)stream;

    memset(stream, 0, len);

    if (!data.trigger) {
        return;
    }

    for (int i = 0; i < len / 512; i++) {
        float freq = 4194304.0 / ((float)((2048 - (data.frequency_reg)) << 6));
        float period = SAMPLE_RATE / freq;
        float on = period * data.duty_cycle;
        for (int j = 0; j < 256; j++) {
            //audio[i * 256 + j] = data.volume * 1000 * (sin(2 * M_PI * data.sound_chunk_counter / period));
            float t = fmod(data.sound_chunk_counter, period);
            if (t < on)
                audio[i * 256 + j] = data.volume * CHANNEL2_VOLUME;
            else
                audio[i * 256 + j] = 0;
            data.sound_chunk_counter++;
        }
        //volume sweep
        data.vol_sweep_update_timer += 256.0 / SAMPLE_RATE;
        if (data.vol_sweep_step_len != 0 && data.vol_sweep_update_timer >= data.vol_sweep_step_len) {
            data.vol_sweep_update_timer -= data.vol_sweep_step_len;

            data.volume += data.vol_sweep_dir;
            if (data.volume <= 0) {
                data.trigger = 0;
                return;
            }
            if (data.volume > 0xf)
                data.volume = 0xf;
        }
        //sound len counter
        data.sound_timer += 256.0 / SAMPLE_RATE;
        if (data.len_counter_enable && data.sound_timer >= data.sound_len) {
            data.trigger = 0;
            return;
        }
    }
}

void ch3_callback(int channel, void* stream, int len, void* udata) {
    sound_wave_data& data(*((sound_wave_data*)udata));
    uint16_t* audio = (uint16_t*)stream;

    memset(stream, 0, len);

    if (!data.trigger) {
        return;
    }
    
    for (int i = 0; i < len / 512; i++) {
        for (int j = 0; j < 256; j++) {
            float period = SAMPLE_RATE / data.frequency;
            float t = fmod(data.sound_chunk_counter, period);
            int index = (int)((t / period) * 32.0);
            uint16_t sample = (data.wave_pattern[index / 2] >> (4 * (1 - (index & 0x1)))) & 0xf;
            sample = (data.volume == 0) ? 0 : (sample >> (data.volume - 1));
            audio[i * 256 + j] = sample * CHANNEL3_VOLUME;     //adjustments for channel mixing
            data.sound_chunk_counter++;
        }
        //sound len counter
        data.sound_timer += 256.0 / SAMPLE_RATE;
        if (data.len_counter_enable && data.sound_timer >= data.sound_len) {
            data.trigger = 0;
            return;
        }
    }
}

void ch4_callback(int channel, void* stream, int len, void* udata) {
    sound_noise_data& data(*((sound_noise_data*)udata));
    uint16_t* audio = (uint16_t*)stream;

    memset(stream, 0, len);

    if (!data.trigger) {
        return;
    }

    uint8_t lfsr_output = 1;
    for (int i = 0; i < len / 512; i++) {
        for (int j = 0; j < 256; j++) {
            audio[i*256 + j] = lfsr_output ? CHANNEL4_VOLUME * data.volume: 0;     //adjustments for channel mixing
            data.sound_chunk_counter++;

            //update linear feedback shift register
            data.lfsr_output_timer += 1.0 / SAMPLE_RATE;
            if (data.lfsr_output_timer > data.lfsr_period) {
                data.lfsr_output_timer -= data.lfsr_period;

                data.lfsr >>= 1;
                lfsr_output = data.lfsr & 0x1;      //takes the output
                uint8_t t_bit = (lfsr_output ^ (data.lfsr >> 1)) & 0x1;       //bit that become the msb
                uint8_t r_shift = (data.lfsr_width * 8);
                data.lfsr &= (0x7fff >> r_shift);       //clear msb
                data.lfsr |= (t_bit << (15 - r_shift));      //ORs new msb
            }

        }
        //volume sweep
        data.vol_sweep_update_timer += 256.0 / SAMPLE_RATE;
        if (data.vol_sweep_step_len != 0 && data.vol_sweep_update_timer >= data.vol_sweep_step_len) {
            data.vol_sweep_update_timer -= data.vol_sweep_step_len;

            data.volume += data.vol_sweep_dir;
            if (data.volume <= 0) {
                data.trigger = 0;
                return;
            }
            if (data.volume > 0xf)
                data.volume = 0xf;
        }

        //sound len counter
        data.sound_timer += 256.0 / SAMPLE_RATE;
        if (data.len_counter_enable && data.sound_timer >= data.sound_len) {
            data.trigger = 0;
            return;
        }
    }
}


void Sound::UpdateSound(IO_map* io) {
    io_sound_pulse_channel* ch1 = (io_sound_pulse_channel*)&io->NR10;
    io_sound_pulse_channel* ch2 = (io_sound_pulse_channel*)&io->NOT_MAPPED_2;       //corresponding to NR20
    io_sound_wave_channel* ch3 = (io_sound_wave_channel*)&io->NR30;
    io_sound_noise_channel* ch4 = (io_sound_noise_channel*)&io->NOT_MAPPED_3;     //corrisponding to NR40

    //audio chip is powered off or emulator is muted
    if (!(io->NR52 & 0x80) || !enableSound) {
        ch1->trigger = 0;
        ch2->trigger = 0;
        ch3->trigger = 0;
        ch4->trigger = 0;
        channel1.trigger = 0;
        channel2.trigger = 0;
        channel3.trigger = 0;
        channel4.trigger = 0;
        return;
    }
    
    //channel DACs are off or volume is killed?
    if (ch1->initial_volume == 0 || (ch1->initial_volume | ch1->vol_sweep_dir) == 0) {
        channel1.trigger = 0;
        ch1->initial_volume = ch1->vol_sweep_dir = 0;
    }
    if (ch2->initial_volume == 0 || (ch2->initial_volume | ch2->vol_sweep_dir) == 0) {
        channel2.trigger = 0;
        ch2->initial_volume = ch2->vol_sweep_dir = 0;
    }
    if (/*ch3->volume == 0 || */!ch3->master_switch) {
        channel3.trigger = 0;
    }
    if (ch4->initial_volume == 0 || (ch4->initial_volume | ch4->vol_sweep_dir) == 0) {
        channel4.trigger = 0;
        ch4->initial_volume = ch4->vol_sweep_dir = 0;
    }
    
    if (ch1->trigger ) {     //triggered ch1
        ch1->trigger = 0;
        trigger_channel1(ch1);
        
    }
    update_channel1_registers(ch1);

    if (ch2->trigger) {   //triggered ch2
        ch2->trigger = 0;
        trigger_channel2(ch2);
        
    }
    update_channel2_registers(ch2);

    if (ch3->trigger) {   //triggered ch3
        ch3->trigger = 0;
        trigger_channel3(ch3);
        
    }
    update_channel3_registers(ch3);

    if (ch4->trigger) {   //triggered ch4
        ch4->trigger = 0;
        trigger_channel4(ch4);
        
    }
    update_channel4_registers(ch4);
}


void Sound::update_channel1_registers(io_sound_pulse_channel* ch1) {
    
    int temp_freq = (ch1->freq_msb << 8) | ch1->freq_lsb;
    if (channel1.init_frequency != temp_freq)
        channel1.frequency_reg = channel1.init_frequency = temp_freq;
}

void Sound::update_channel2_registers(io_sound_pulse_channel *ch2) {

    int temp_freq = (ch2->freq_msb << 8) | ch2->freq_lsb;
    if (channel2.init_frequency != temp_freq)
        channel2.frequency_reg = channel2.init_frequency = temp_freq;
}

void Sound::update_channel3_registers(io_sound_wave_channel *ch3) {

    uint16_t freq_reg = (ch3->freq_msb << 8) | ch3->freq_lsb;
    float temp_freq = 4194304.0 / ((2048 - freq_reg) << 7);
    //temp_freq /= 4;

    if (channel3.init_frequency != temp_freq)
        channel3.frequency = channel3.init_frequency = temp_freq;

    if (ch3->volume != channel3.init_volume) {
        channel3.volume = channel3.init_volume = ch3->volume;
    }
    
}

void Sound::update_channel4_registers(io_sound_noise_channel *ch4) {
    
}

void Sound::trigger_channel1(io_sound_pulse_channel* ch1) {
    
    if (!channel1.trigger) {
        channel1.sound_chunk_counter = 0;
    }
    channel1.duty_cycle = ch1->duty_cycle == 0 ? 0.125 : (0.25 * ch1->duty_cycle);
    channel1.len_counter_enable = ch1->len_count_enable;
    channel1.freq_sweep_update_timer = 0;
    channel1.volume = ch1->initial_volume;
    channel1.vol_sweep_dir = (ch1->vol_sweep_dir == 0 ? -1 : 1);
    //souces say vol_sweep_step_len/64.0 but the sound seems to decay twice as fast as the orginal gameboy
    channel1.vol_sweep_step_len = (double)ch1->vol_sweep_step_len / 32.0;
    channel1.freq_sweep_amount = (ch1->freq_sweep_dir == 1 ? -1 : 1) * ch1->freq_sweep_rtshift;
    channel1.freq_sweep_timer = ch1->freq_sweep_timer == 0 ? 0 : ((double)(ch1->freq_sweep_timer + 1)) / 128.0;
    channel1.sound_len = (64 - ch1->len_counter) / 256.0;
    
    channel1.sound_timer = 0;
    channel1.vol_sweep_update_timer = 0;

    channel1.trigger = 1;
}

void Sound::trigger_channel2(io_sound_pulse_channel *ch2) {
    if (!channel1.trigger) {
        channel2.sound_chunk_counter = 0;
    }

    //these are not used in channel 2
    channel2.freq_sweep_amount = 0;     //channel 2 doesn't have frequency sweep
    channel2.freq_sweep_timer = 0;
    channel2.freq_sweep_update_timer = 0;

    channel2.len_counter_enable = ch2->len_count_enable;
    channel2.duty_cycle = ch2->duty_cycle == 0 ? 0.125 : (0.25 * ch2->duty_cycle);
    channel2.volume = ch2->initial_volume;
    channel2.vol_sweep_dir = (ch2->vol_sweep_dir == 0 ? -1 : 1);
    //souces say vol_sweep_step_len/64.0 but the sound seems to decay twice as fast as the orginal gameboy
    channel2.vol_sweep_step_len = (double)ch2->vol_sweep_step_len / 32.0;
    channel2.sound_len = (64 - ch2->len_counter) / 256.0;
    channel2.sound_timer = 0;
    channel2.vol_sweep_update_timer = 0;

    channel2.trigger = 1;
}

void Sound::trigger_channel3(io_sound_wave_channel *ch3) {
    
    if (!channel3.trigger)
        channel3.sound_chunk_counter = 0;

    channel3.len_counter_enable = ch3->len_count_enable;
    channel3.sound_len = (64 - ch3->len_counter) / 256.0;
    channel3.sound_timer = 0;

    channel3.trigger = 1;
}

void Sound::trigger_channel4(io_sound_noise_channel *ch4) {
    if(!channel4.trigger)
        channel4.sound_chunk_counter = 0;

    float div_ratio = ch4->div_freq_ratio == 0 ? 0.5 : ch4->div_freq_ratio;
    channel4.lfsr_period = 1.0 / ((524288.0 / div_ratio) / (2 << (ch4->shift_clk_freq + 1)));
    channel4.sound_len = (64 - ch4->len_counter) / 256.0;
    channel4.sound_timer = 0;
    channel4.len_counter_enable = ch4->len_count_enable;
   
    channel4.duty_cycle = ch4->duty_cycle;
    channel4.lfsr_width = ch4->shift_reg_width;
    channel4.volume = ch4->initial_volume;
    channel4.vol_sweep_dir = (ch4->vol_sweep_dir == 0 ? -1 : 1);
    //souces say vol_sweep_step_len/64.0 but the sound seems to decay twice as fast as the original gameboy
    channel4.vol_sweep_step_len = (double)ch4->vol_sweep_step_len / 32.0;
    channel4.vol_sweep_update_timer = 0;
    channel4.lfsr_output_timer = 0;
    channel4.lfsr = 0xffff;

    channel4.trigger = 1;
}

void Sound::Halt() {
    channel1.trigger = 0;
    channel2.trigger = 0;
    channel3.trigger = 0;
    channel4.trigger = 0;
}

bool* Sound::getSoundEnable() {
    return &enableSound;
}

Sound::Sound()
{
    enableSound = true;
    int bufferLen = 5;

    //allocate 2 seconds of sound for each channel
    channel1.chunk.alen = SAMPLE_RATE * 2 * bufferLen;
    channel1.chunk.abuf = (uint8_t*)calloc(SAMPLE_RATE * bufferLen, 2);
    channel1.chunk.allocated = 1;
    channel1.chunk.volume = 255;

    channel2.chunk.alen = SAMPLE_RATE * 2 * bufferLen;
    channel2.chunk.abuf = (uint8_t*)calloc(SAMPLE_RATE * bufferLen, 2);
    channel2.chunk.allocated = 1;
    channel2.chunk.volume = 255;

    channel3.chunk.alen = SAMPLE_RATE * 2 * bufferLen;
    channel3.chunk.abuf = (uint8_t*)calloc(SAMPLE_RATE * bufferLen, 2);
    channel3.chunk.allocated = 1;
    channel3.chunk.volume = 255;
    channel3.wave_pattern = _memory->getIOMap()->WP;

    channel4.chunk.alen = SAMPLE_RATE * 2 * bufferLen;
    channel4.chunk.abuf = (uint8_t*)calloc(SAMPLE_RATE * bufferLen, 2);
    channel4.chunk.allocated = 1;
    channel4.chunk.volume = 255;

    Mix_Init(0);

    if (Mix_OpenAudio(SAMPLE_RATE, MIX_DEFAULT_FORMAT, 4, 1024) == -1) {		//22050 / 44100
        std::cout << "\n Mix_OpenAudio Failed: %s" << SDL_GetError() << std::endl;
        fatal(FATAL_SDL_AUDIO_INIT_FAILED, __func__);
    }
    Mix_PlayChannel(0, &channel1.chunk, -1);
    Mix_RegisterEffect(0, ch1_callback, nullptr, &channel1);
    Mix_PlayChannel(1, &channel2.chunk, -1);
    Mix_RegisterEffect(1, ch2_callback, nullptr, &channel2);
    Mix_PlayChannel(2, &channel3.chunk, -1);
    Mix_RegisterEffect(2, ch3_callback, nullptr, &channel3);
    Mix_PlayChannel(3, &channel4.chunk, -1);
    Mix_RegisterEffect(3, ch4_callback, nullptr, &channel4);

    Mix_Resume(0);
    Mix_Resume(1);
    Mix_Resume(2);
    Mix_Resume(3);
}