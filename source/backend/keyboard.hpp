#pragma once

#include <core/types.hpp>
#include <pipeline.hpp>

class Keyboard
{
private:
	static Keyboard *keyboard;

public:
	Keyboard()
	{
		keyboard_clear();
	}

	struct State
	{
		bool keyCurrent[256];
		bool keyPrevious[256];
		bool keyRepeat[256];
		float keyRepeatTimer[256];
		char inputBuffer[16];
	};

	static State &state();

	static void reset_active();
	static void set_active(Keyboard &keyboard);

	static bool check(u8 key) { return keyboard->keyboard_check(key); }
	static bool check_pressed(u8 key) { return keyboard->keyboard_check_pressed(key); }
	static bool check_pressed_repeat(u8 key) { return keyboard->keyboard_check_pressed_repeat(key); }
	static bool check_released(u8 key) { return keyboard->keyboard_check_released(key); }
	static bool check_any() { return keyboard->keyboard_check_any(); }
	static bool check_pressed_any() { return keyboard->keyboard_check_pressed_any(); }
	static bool check_pressed_repeat_any() { return keyboard->keyboard_check_pressed_repeat_any(); }
	static bool check_released_any() { return keyboard->keyboard_check_released_any(); }

	static bool has_input() { return keyboard->keyboard_has_input(); }
	static char *input_buffer() { return keyboard->keyboard_input_buffer(); }

	static void update(u64 delta) { return keyboard->keyboard_update(delta); }
	static void clear() { return keyboard->keyboard_clear(); }

private:
	bool keyboard_check(u8 key);
	bool keyboard_check_pressed(u8 key);
	bool keyboard_check_pressed_repeat(u8 key);
	bool keyboard_check_released(u8 key);

	bool keyboard_check_any();
	bool keyboard_check_pressed_any();
	bool keyboard_check_pressed_repeat_any();
	bool keyboard_check_released_any();

	bool keyboard_has_input();
	char *keyboard_input_buffer();

	void keyboard_update(u64 delta);
	void keyboard_clear();

public:
	State keyboardState;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////