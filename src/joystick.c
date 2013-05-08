#include <SDL.h>

#include "main.h"
#include "input.h"
#include "joystick.h"
#include "screen.h"

/* Joystick button to ST scan code mapping table */
/* XXX: hardcoded for my joystick.
 * TODO: read from a config file. */
static int JoystickButtonToSTScanCode[16] = {
	[0] = 0x3b,
	[2] = 0x39,
	[3] = 0x41,
	[4] = 0x36,
	[5] = 0x1c,
	[6] = 0x10,
	[7] = 0x11,
	[11] = 0x43,
	[12] = 0x48,
	[13] = 0x50,
	[14] = 0x4b,
	[15] = 0x4d,
};

static void inject_mouse_event(unsigned int x, unsigned int y, int pressed)
{
	static unsigned int old_x, old_y;

	if (pressed) {
		old_x = input.abs_x;
		input.abs_x = x;
		old_y = input.abs_y;
		input.abs_y = y;
		Input_MousePress(SDL_BUTTON_LEFT);
	} else {
		Input_MouseRelease(SDL_BUTTON_LEFT);
		input.abs_x = old_x;
		input.abs_y = old_y;
	}
}

void Keymap_JoystickUpDown(unsigned int button, int pressed)
{
	static int currentTimeMode = 1;
	static unsigned int button_already_pressed;
	char code;

	if (button > 16)
		return;

	code = JoystickButtonToSTScanCode[button];
	if (!code)
		return;

	if (code == 0x10 || code == 0x11) {
		if (pressed) {
			if (button_already_pressed)
				return;
			button_already_pressed = button;
		} else if (button != button_already_pressed)
			return;
		else
			button_already_pressed = 0;

		if (pressed && code == 0x11 && currentTimeMode < 5)
			currentTimeMode++;
		if (pressed && code == 0x10 && currentTimeMode)
			currentTimeMode--;
		inject_mouse_event(5 + (screen_w >> 5) * currentTimeMode, screen_h * 9 / 10, pressed);
	} else
		Input_PressSTKey(code, pressed);
}
