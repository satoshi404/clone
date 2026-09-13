#include <backend/keyboard.hpp>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Keyboard KEYBOARD_DEFAULT;
Keyboard *Keyboard::keyboard = &KEYBOARD_DEFAULT;


Keyboard::State &Keyboard::state()
{
	return keyboard->keyboardState;
}


void Keyboard::reset_active()
{
	Keyboard::keyboard = &KEYBOARD_DEFAULT;
}


void Keyboard::set_active( Keyboard &keyboard )
{
	Keyboard::keyboard = &keyboard;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Keyboard::keyboard_check( u8 key )
{
	return keyboardState.keyCurrent[key];
}


bool Keyboard::keyboard_check_pressed( u8 key )
{
	return keyboardState.keyCurrent[key] && !keyboardState.keyPrevious[key];
}


bool Keyboard::keyboard_check_pressed_repeat( u8 key )
{
	return keyboard_check_pressed( key ) || keyboardState.keyRepeat[key];
}


bool Keyboard::keyboard_check_released( u8 key )
{
	return !keyboardState.keyCurrent[key] && keyboardState.keyPrevious[key];
}


bool Keyboard::keyboard_check_any()
{
	for( int i = 0; i < U8_MAX; i++ )
	{
		if( Keyboard::keyboard_check( i ) ) { return true; }
	}

	return false;
}


bool Keyboard::keyboard_check_pressed_any()
{
	for( int i = 0; i < U8_MAX; i++ )
	{
		if( Keyboard::keyboard_check_pressed( i ) ) { return true; }
	}

	return false;
}


bool Keyboard::keyboard_check_pressed_repeat_any()
{
	for( int i = 0; i < U8_MAX; i++ )
	{
		if( Keyboard::keyboard_check_pressed_repeat( i ) ) { return true; }
	}

	return false;
}


bool Keyboard::keyboard_check_released_any()
{
	for( int i = 0; i < U8_MAX; i++ )
	{
		if( Keyboard::keyboard_check_released( i ) ) { return true; }
	}

	return false;
}

bool Keyboard::keyboard_has_input()
{
	return keyboardState.inputBuffer[0] != '\0';
}

char *Keyboard::keyboard_input_buffer()
{
	return keyboardState.inputBuffer;
}


void Keyboard::keyboard_update( u64 delta )
{
	// Key State
	for( int key = 0; key < U8_MAX; key++ )
	{
		keyboardState.keyPrevious[key] = keyboardState.keyCurrent[key];
		keyboardState.keyRepeat[key] = false;
	}

	// Reset Input Buffer
	keyboardState.inputBuffer[0] = '\0';
}


void Keyboard::keyboard_clear()
{
	// Reset Key States
	for( int key = 0; key < U8_MAX; key++ )
	{
		keyboardState.keyCurrent[key] = false;
		keyboardState.keyPrevious[key] = false;
		keyboardState.keyRepeat[key] = false;
		keyboardState.keyRepeatTimer[key] = 0.0;
	}

	// Clear Input Buffer
	keyboardState.inputBuffer[0] = '\0';
}
