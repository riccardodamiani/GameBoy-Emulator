#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <cstdint>
#include <SDL_mixer.h>
#include <SDL.h>

enum MBC_type {
	NO_MBC,
	MBC1,
	MBC2,
	MBC3,
	MBC4,
	MBC5,
	MBC6,
	MBC7
};

struct cartridge_header {
	uint8_t entryPoint[4];
	uint8_t nintendoLogo[0x30];
	uint8_t title[15];
	//uint8_t manufCode[4];
	uint8_t cgbFlag;
	uint8_t newLicenseeCode[2];
	uint8_t sgbFlag;
	uint8_t cartridgeType;
	uint8_t romSize;
	uint8_t ramSize;
	uint8_t destCode;
	uint8_t oldLicenseeCode;
	uint8_t maskRomVersionNumber;
	uint8_t headerChecksum;
	uint8_t globalChecksum[2];
};

struct MBC {
	enum MBC_type type;
	uint8_t reg1;
	uint8_t reg2;
	uint32_t (*translateAddr)(uint16_t gb_addr);
	uint32_t mbc0_translate_func(uint16_t gb_addr) {
		return gb_addr;
	}
};

struct sprite_attribute {
	uint8_t y_pos;
	uint8_t x_pos;
	uint8_t tile;
	uint8_t gbc_palette : 3,		//GBC only. Palette number
		vram_bank : 1,		//GBC only. Vram bank number
		palette : 1,	//sprite palette number
		x_flip : 1,
		y_flip : 1,
		priority : 1;	//render priority
};

struct background_attribute {		//attribute sprite for background (GBC mode only)
	uint8_t bg_palette : 3,		//background palette number
		vram_bank : 1,	//vram bank number
		not_used : 1,
		h_flip : 1,		//(0=Normal, 1=Mirror horizontally)
		v_flip : 1,		//(0=Normal, 1=Mirror vertically)
		bg_oam_priority : 1;	//(0=Use OAM priority bit, 1=BG Priority)
};

struct background_tile {
	uint8_t tile_mem[16];
	background_attribute tile_attr;
};


struct sound_pulse_data {
	Mix_Chunk chunk;		//sound data
	int sound_chunk_counter;
	float duty_cycle;
	float init_sound_len;
	float sound_len;	//sound duration in seconds
	//float sound_timer;	//time elapsed
	int len_counter_enable;		//1 if len counter is enable
	int trigger;	//1 when the channel is active
	int volume;		//starting volume
	float vol_sweep_step_len;		//time between each volume update in seconds
	float vol_sweep_update_timer;		//time to next volume update in seconds
	int vol_sweep_dir;		//volume sweep direction: -1 decrease, 1 increase
	int frequency_reg;
	int new_frequency;
	bool frequency_write;
	float freq_sweep_timer;		//time between each frequency update in seconds
	float freq_sweep_update_timer;		//time to next frequency update in seconds
	int freq_sweep_amount;		//amount that is added to the frequency_reg register with sign
};

struct io_sound_pulse_channel {
	uint8_t freq_sweep_rtshift : 3,	//amount that is added to the frequency register with sign controlled by freq_sweep_dir
		freq_sweep_dir : 1,		//frequency sweep direction: 0 decrease, 1 increase
		freq_sweep_timer : 3,		//freq = 128 / (freq_sweep_timer + 1)		frequency of update
		unused1 : 1;

	uint8_t len_counter : 6,		//sound length
		duty_cycle : 2;	//duty cycle of the sound: 0 = 12.5%, 1 = 25%, 2 = 50%, 3 = 75%
		
	uint8_t vol_sweep_step_len : 3,		//each step takes vol_sweep_step_len/64 seconds
		vol_sweep_dir : 1,		//volume sweep direction: 0 decrease, 1 increase
		initial_volume : 4;		//starting volume

	uint8_t freq_lsb;			//8 least significant bits of frequency
	uint8_t freq_msb : 3,		//3 most significant bits of frequency	(write)
		unused2 : 3,
		len_count_enable : 1,	//len counter enable	(read/write)
		trigger : 1;		//trigger for the channel		(write only)
};

struct sound_wave_data {
	uint8_t* wave_pattern;
	Mix_Chunk chunk;		//sound data
	int sound_chunk_counter;
	float sound_len;	//sound duration in seconds
	//float sound_timer;	//time elapsed
	int len_counter_enable;		//1 if len counter is enable
	int trigger;	//1 when the channel is active
	int volume;		//volume register
	//int init_volume;	//volume on init
	uint16_t frequency;		//frequency register
	uint16_t new_frequency;
	bool frequency_write;
};

struct io_sound_wave_channel {
	uint8_t unused1 : 7,
		master_switch : 1;		//master on-off for channel 3
	uint8_t len_counter;		//sound length
	uint8_t unused2 : 5,
		volume : 2,			//volume level: (0: Mute; 1: 100%; 2: 50% [rtshift 1]; 3: 25% [rtshift 2])
		unused3 : 1;
	uint8_t freq_lsb;		//8 least significant bits of frequency
	uint8_t freq_msb : 3,		//3 most significant bits of frequency	(write)
		unused4 : 3,
		len_count_enable : 1,	//len counter enable	(read/write)
		trigger : 1;		//trigger for the channel		(write only)
};

struct sound_noise_data {
	Mix_Chunk chunk;		//sound data
	int trigger;
	int sound_chunk_counter;
	float sound_len;	//sound duration in seconds
	//float sound_timer;	//time elapsed
	int len_counter_enable;		//1 if len counter is enable
	int duty_cycle;
	int volume;		//init volume
	float vol_sweep_step_len;		//time between each volume update in seconds
	float vol_sweep_update_timer;		//time to next volume update in seconds
	int vol_sweep_dir;		//volume sweep direction: -1 decrease, 1 increase
	float lfsr_output_timer;		//time to next lfsr output in seconds
	float lfsr_period;		//time between each linear feedback shift registers output bit in seconds
	int lfsr_width;		//lfsr width: (0: 15bits, 1: 7 bits)
	uint16_t lfsr;
};

struct io_sound_noise_channel {
	uint8_t unused;
	uint8_t len_counter : 6,		//sound length
		duty_cycle : 2;	//duty cycle of the sound: 0 = 12.5%, 1 = 25%, 2 = 50%, 3 = 75%
	uint8_t vol_sweep_step_len : 3,		//each step takes vol_sweep_step_len/64 seconds
		vol_sweep_dir : 1,		//volume sweep direction: 0 decrease, 1 increase
		initial_volume : 4;		//starting volume
	uint8_t div_freq_ratio : 3,		//dividing ratio for frequency
		shift_reg_width : 1,		//shift register width (0: 15bits, 1: 7 bits)
		shift_clk_freq : 4;			//shift clock frequency
	//the output frequency from the noise generator is: 
	//524288 Hz / div_freq_ratio / 2^(shift_clk_freq+1)     
	//For div_freq_ratio=0 use div_freq_ratio=0.5 instead
	uint8_t unused2 : 6,
		len_count_enable : 1,	//len counter enable	(read/write)
		trigger : 1;		//trigger for the channel		(write only)

};

struct registers {
	uint8_t a, b, c, d, e, h, l;	//general purpose registers
	struct flag {
		uint8_t grd : 4,	//always 0
			c : 1,		//carry
			h : 1,		//half carry
			n : 1,		//negative
			z : 1;	//zero
	}flag;
	uint16_t	pc,		//program counter
				sp;		//stack pointer

	uint8_t IME : 1,		//interrupt master enable
		IME_CC : 2,
		IME_U : 1,
		EMPTY : 4;
	//uint16_t sl_cnt;		//scanline cycles counter
	uint16_t timer_clk;		//counter for timer handling
	uint16_t div_cnt;		//counter for divider register
	uint8_t joyp_stat;		//previous joypad stat. Used to trigger a joypad IRQ
	uint8_t halted;
	uint8_t stopped;
	uint64_t clock_cnt;
};

struct priority_pixel {
	uint32_t color;
	uint8_t priority : 1,
		trasparent : 1,
		not_used : 6;
};

struct ppu_registers {
	uint8_t stat_signal;
	uint16_t sl_cnt;
	uint8_t spritesLoaded;
	uint8_t bufferDrawn;
	sprite_attribute* scanlineSprites[10];
	background_tile backgroundTiles[21];
	uint8_t firstBgTilePixelX, firstBgTilePixelY;
	priority_pixel windowScanline[160];
	uint8_t windowScanlineActive;
	uint8_t enabled;
};

struct STAT_struct {
	uint8_t lcd_mode : 2,		//lcd draw mode
		ly_cf : 1,			//LY coincidence flag
		//STAT interrupt selection:
		mode0_int : 1,	//MODE 0 (HBlank)
		mode1_int : 1,	//Mode 1 (VBlank)
		mode2_int : 1,	//Mode 2 (OAM Scan)
		ly_c : 1,	//LY coincidence
		not_used : 1;
};

struct joypad {
	int left, right, up, down;
	int a, b, select, start;
};

struct joypad_map {
	SDL_Scancode a, b, start, select;
	SDL_Scancode left, right, up, down;
	
};


struct hdma_struct {
	uint8_t HDMA1;	//CGB Mode Only - New DMA Source, High
	uint8_t HDMA2;	//CGB Mode Only - New DMA Source, low
	uint8_t HDMA3;		//CGB Mode Only - New DMA Destination, High
	uint8_t HDMA4;		//CGB Mode Only - New DMA Destination, Low
	//CGB Mode Only - New DMA Length/Mode/Start
	uint8_t transf_length : 7,		//length to be trasfered. In bytes: (transf_length + 1)*10
		transfer_mode : 1;		//0 - General Purpose DMA, 1 - H-Blank DMA
};

struct color_palette {
	uint16_t red : 5,
		green : 5,
		blue : 5,
		not_used : 1;
};

struct palette_access_struct {
	uint8_t bg_palette_index : 6,
		not_used_1 : 1,
		bg_inc : 1;
	uint8_t bg_palette_data;
	uint8_t sprite_palette_index : 6,
		not_used_2 : 1,
		sprite_inc : 1;
	uint8_t sprite_palette_data;
};


struct IO_map {
	uint8_t JOYP;	//joypad
	uint8_t SB;		//serial byte
	uint8_t SC;		//serial control
	uint8_t NOT_MAPPED_0;
	uint8_t DIV;	//clock divider
	uint8_t TIMA;	//Timer value
	uint8_t TMA;	//Timer reload
	uint8_t TAC;	//Timer control
	uint8_t NOT_MAPPED_1[7];
	uint8_t IF;		// Interrupts flags
	uint8_t NR10;	//Audio channel 1 sweep period, negate, shift
	uint8_t NR11;	//Audio channel 1 sound length/wave duty
	uint8_t NR12;	//Audio channel 1 starting volume, envelope add mode, period
	uint8_t NR13;	//Audio channel 1 frequency
	uint8_t NR14;	//Audio channel 1 control
	uint8_t NOT_MAPPED_2;
	uint8_t NR21;	//Audio channel 2 sound length/wave duty
	uint8_t NR22;	//Audio channel 2 envelope
	uint8_t NR23;	//Audio channel 2 frequency
	uint8_t NR24;	//Audio channel 2 control
	uint8_t NR30;	//Audio channel 3 enable
	uint8_t NR31;	//Audio channel 3 sound length
	uint8_t NR32;	//Audio channel 3 volume
	uint8_t NR33;	//Audio channel 3 frequency
	uint8_t NR34;	//Audio channel 3 control
	uint8_t NOT_MAPPED_3;
	uint8_t NR41;	//Audio channel 4 sound length
	uint8_t NR42;	//Audio channel 4 volume
	uint8_t NR43;	//Audio channel 4 frequency
	uint8_t NR44;	//Audio channel 4 control
	uint8_t NR50;	//Audio output mapping
	uint8_t NR51;	//Audio channel mapping
	uint8_t NR52;	//Audio channel control
	uint8_t NOT_MAPPED_4[9];
	uint8_t WP[16];	//Wave pattern
	uint8_t LCDC;	//LCD control
	uint8_t STAT;	//LCD status
	uint8_t SCY;	//Background vert. scroll
	uint8_t SCX;	//Background horiz. scroll
	uint8_t LY;		//LCD Y coordinate
	uint8_t LYC;	//LCD Y compare
	uint8_t DMA;	//OAM DMA source address
	uint8_t BGP;	//Background palette
	uint8_t OBP0;	//OBJ palette 0
	uint8_t OBP1;	// OBJ palette 1
	uint8_t WY;		//Window Y coord
	uint8_t WX;		//Window X coord
	uint8_t NOT_MAPPED_5;
	uint8_t KEY1;	//prepare speed switch
	uint8_t NOT_MAPPED_6;
	uint8_t VBK;	//vram bank number. Only bit 0 in used
	uint8_t BRC;	//Boot ROM control
	hdma_struct HDMA;	//LCD VRAM DMA Transfers
	uint8_t RP;		//CGB Mode Only - Infrared Communications Port
	uint8_t HR1[0X11];	//high ram block. 0xff51-0xfffe, overlaps with other registers in gbc mode
	palette_access_struct PLT;	//palette access registers
	uint8_t HR2[0X4];	//high ram block. 0xff51-0xfffe, overlaps with other registers in gbc mode
	uint8_t SVBK; //CGB Mode Only - WRAM Bank
	uint8_t HR3[0X8e];	//high ram block. 0xff51-0xfffe, overlaps with other registers in gbc mode
	uint8_t IE;		//Interrupts enabled
};



#endif
