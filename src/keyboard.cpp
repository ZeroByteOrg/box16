// Commander X16 Emulator
// Copyright (c) 2021-2022 Stephen Horn, et al.
// All rights reserved. License: 2-clause BSD

#include <SDL.h>
#include <list>
#include <nfd.h>
#include <stdio.h>

#include "glue.h"
#include "keyboard.h"
#include "i2c.h"
#include "ring_buffer.h"
#include "rom_symbols.h"
#include "unicode.h"
#include "utf8.h"

#define EXTENDED_FLAG 0x100
#define ESC_IS_BREAK /* if enabled, Esc sends Break/Pause key instead of Esc */

enum class keyboard_event_type {
	key_event,
	text_input
};

struct key_event_data {
	uint16_t ps2_code;
	bool     down;
};

struct text_input_data {
	const char *file_chars;
	const char *c;
	bool        run_after_load;
};

struct keyboard_event {
	keyboard_event_type type;
	union event_data {
		key_event_data  key_event;
		text_input_data text_input;
	} data;
};

static std::list<keyboard_event> Keyboard_event_list;
static ring_buffer<uint8_t, 160> Keyboard_buffer;

static const uint16_t SDL_to_PS2_table[] = {
        0x0000, 0x0000, 0x0000, 0x0000, 0x001c, 0x0032, 0x0021, 0x0023, 0x0024, 0x002b, 0x0034, 0x0033, 0x0043, 0x003b, 0x0042, 0x004b,
        0x003a, 0x0031, 0x0044, 0x004d, 0x0015, 0x002d, 0x001b, 0x002c, 0x003c, 0x002a, 0x001d, 0x0022, 0x0035, 0x001a, 0x0016, 0x001e,
        0x0026, 0x0025, 0x002e, 0x0036, 0x003d, 0x003e, 0x0046, 0x0045, 0x005a, 0x00ff, 0x0066, 0x000d, 0x0029, 0x004e, 0x0055, 0x0054,
        0x005b, 0x005d, 0x0000, 0x004c, 0x0052, 0x000e, 0x0041, 0x0049, 0x004a, 0x0058, 0x0005, 0x0006, 0x0004, 0x000c, 0x0003, 0x000b,
        0x0083, 0x000a, 0x0001, 0x0009, 0x0078, 0x0007, 0x0000, 0x0000, 0x0000, 0x0170, 0x016c, 0x017d, 0x0171, 0x0169, 0x017a, 0x0174,
        0x016b, 0x0172, 0x0175, 0x0000, 0x014a, 0x007c, 0x007b, 0x0079, 0x015a, 0x0069, 0x0072, 0x007a, 0x006b, 0x0073, 0x0074, 0x006c,
        0x0075, 0x007d, 0x0070, 0x0071, 0x0061, 0x012f, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0014, 0x0012, 0x0011, 0x015b, 0x0114, 0x0059, 0x0111, 0x015b, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static bool process_key_event(const key_event_data &data)
{
	if (data.down && data.ps2_code == 0xff) {
		// "Pause/Break" sequence
		Keyboard_buffer.add(0xe1);
		Keyboard_buffer.add(0x14);
		Keyboard_buffer.add(0x77);
		Keyboard_buffer.add(0xe1);
		Keyboard_buffer.add(0xf0);
		Keyboard_buffer.add(0x14);
		Keyboard_buffer.add(0xf0);
		Keyboard_buffer.add(0x77);
	} else {
		if (data.ps2_code & EXTENDED_FLAG) {
			Keyboard_buffer.add(0xe0);
		}
		if (!data.down) {
			Keyboard_buffer.add(0xf0); // BREAK
		}
		Keyboard_buffer.add(data.ps2_code & 0xff);
	}

	return true;
}

static bool process_text_input(text_input_data &data)
{
	while (*data.c && RAM[NDX] < 10) {
		uint32_t c;
		int      e = 0;

		if (data.c[0] == '\\' && data.c[1] == 'X' && data.c[2] && data.c[3]) {
			auto ctol = [](const char c) {
				if (c >= '0' || c <= '9') {
					return c - 0;
				}
				if (c >= 'A' || c <= 'F') {
					return 10 + c - 'A';
				}
				if (c >= 'a' || c <= 'f') {
					return 10 + c - 'a';
				}
				return 0;
			};
			uint8_t hi = ctol(data.c[2]);
			uint8_t lo = ctol(data.c[3]);
			c          = hi << 4 | lo;
			data.c += 4;
		} else {
			data.c = static_cast<const char *>(utf8_decode(data.c, &c, &e));
			c      = iso8859_15_from_unicode(c);
		}
		if (c && !e) {
			RAM[KEYD + RAM[NDX]] = c;
			RAM[NDX]++;
		} else {
			return true;
		}
	}

	return (*data.c == 0);
}

void keyboard_process()
{
	if (Keyboard_event_list.empty()) {
		return;
	}

	keyboard_event &evt = Keyboard_event_list.front();
	switch(evt.type) {
		case keyboard_event_type::key_event:
			process_key_event(evt.data.key_event);
			Keyboard_event_list.pop_front();
			break;
		case keyboard_event_type::text_input:
			if (process_text_input(evt.data.text_input)) {
				Keyboard_event_list.pop_front();
			}
			break;
	}
}

void keyboard_add_event(const bool down, const SDL_Scancode scancode)
{
	if (Options.log_keyboard) {
		printf("%s 0x%02X\n", down ? "DOWN" : "UP", scancode);
		fflush(stdout);
	}

	keyboard_event evt;
	evt.data.key_event.down     = down;
	evt.data.key_event.ps2_code = SDL_to_PS2_table[scancode];

	if (Keyboard_event_list.empty()) {
		evt.type = keyboard_event_type::key_event;
		Keyboard_event_list.push_back(evt);
	} else {
		process_key_event(evt.data.key_event);
	}
}

void keyboard_add_text(char const *const text)
{
	size_t text_len  = strlen(text);
	char * text_copy = new char[text_len + 1];

	strcpy(text_copy, text);

	keyboard_event evt;
	evt.data.text_input.file_chars     = text_copy;
	evt.data.text_input.c              = text_copy;

	evt.type = keyboard_event_type::text_input;
	Keyboard_event_list.push_back(evt);
}

void keyboard_add_file(char const *const path)
{
	SDL_RWops *file = SDL_RWFromFile(path, "r");
	if (file == nullptr) {
		printf("Cannot open text file %s!\n", path);
		return;
	}

	const size_t file_size   = (size_t)SDL_RWsize(file);
	const size_t buffer_size = file_size + 1;

	char *const file_text = new char[buffer_size];

	const size_t read_size = SDL_RWread(file, file_text, 1, file_size);
	if (read_size != file_size) {
		printf("File read error on %s\n", path);
		delete[] file_text;
	} else {
		file_text[read_size] = 0;

		keyboard_event evt;
		evt.type                           = keyboard_event_type::text_input;
		evt.data.text_input.file_chars     = file_text;
		evt.data.text_input.c              = file_text;
		evt.data.text_input.run_after_load = false;

		Keyboard_event_list.push_back(evt);
	}

	SDL_RWclose(file);
}

void keyboard_buffer_flush()
{
	Keyboard_buffer.clear();
}

uint8_t keyboard_get_next_byte()
{
	return (Keyboard_buffer.count() > 0) ? Keyboard_buffer.pop_oldest() : 0;
}

// fake mouse

static ring_buffer<uint8_t, 160> Mouse_buffer;

static uint8_t buttons;
static int16_t mouse_diff_x = 0;
static int16_t mouse_diff_y = 0;

// byte 0, bit 7: Y overflow
// byte 0, bit 6: X overflow
// byte 0, bit 5: Y sign bit
// byte 0, bit 4: X sign bit
// byte 0, bit 3: Always 1
// byte 0, bit 2: Middle Btn
// byte 0, bit 1: Right Btn
// byte 0, bit 0: Left Btn
// byte 2:        X Movement
// byte 3:        Y Movement

static bool mouse_send(int x, int y, int b)
{
	if (Mouse_buffer.size_remaining() >= 3) {
		uint8_t byte0 =
		    ((y >> 9) & 1) << 5 |
		    ((x >> 9) & 1) << 4 |
		    1 << 3 |
		    b;
		Mouse_buffer.add(byte0);
		Mouse_buffer.add(x);
		Mouse_buffer.add(y);

		return true;
	} else {
		//		printf("buffer full, skipping...\n");
		return false;
	}
}

void mouse_button_down(int num)
{
	buttons |= 1 << num;
}

void mouse_button_up(int num)
{
	buttons &= (1 << num) ^ 0xff;
}

void mouse_move(int x, int y)
{
	mouse_diff_x += x;
	mouse_diff_y -= y;
}

uint8_t mouse_read(uint8_t reg)
{
	return 0xff;
}

void mouse_send_state()
{
	do {
		int send_diff_x = []() -> int {
			if (mouse_diff_x > 255) {
				return 255;
			}
			if (mouse_diff_x < -256) {
				return -256;
			}
			return mouse_diff_x;
		}();

		int send_diff_y = []() -> int {
			if (mouse_diff_y > 255) {
				return 255;
			}
			if (mouse_diff_y < -256) {
				return -256;
			}
			return mouse_diff_y;
		}();

		if (!mouse_send(mouse_diff_x, mouse_diff_y, buttons)) {
			break;
		}

		mouse_diff_x -= send_diff_x;
		mouse_diff_y -= send_diff_y;
	} while (mouse_diff_x != 0 && mouse_diff_y != 0);
}

uint8_t mouse_get_next_byte()
{
	return (Mouse_buffer.count() > 0) ? Mouse_buffer.pop_oldest() : 0;
}