#include <SDL.h>

#include "main.h"
#include "input.h"
#include "joystick.h"
#include "screen.h"

/* Key mapping of the game */
#define MOVE_UP		0x2c
#define MOVE_DOWN	0x1e
#define MOVE_LEFT	0x33
#define MOVE_RIGHT	0x34
#define RTHRUST		0x36
#define THRUST		0x1c
#define RADAR		0x13
#define ZOOM_IN		0x4a
#define ZOOM_OUT	0x4e
#define LOOK_UP		0x48
#define LOOK_DOWN	0x50
#define LOOK_LEFT	0x4b
#define LOOK_RIGHT	0x4c
#define PAUSE		0x01
#define MB4_PHOTO	0x20
#define HYPERSPACE	0x23
#define LASER		0x39
#define ECM			0x12
#define EJECT		0x2d
#define BOMB		0x30
#define MISSILE		0x32
#define MAP_CENTER	0x2e
#define F1			0x3b
#define F2			0x3c
#define F3			0x3d
#define F4			0x3e
#define F5			0x3f
#define F6			0x40
#define F7			0x41
#define F8			0x42
#define F9			0x43
#define F10			0x44
#define ALT			0x38

#define SPECIAL_TIME_DECREASE	0x10
#define SPECIAL_TIME_INCREASE	0x11


/* Joystick button to ST scan code mapping table */
/* XXX: hardcoded for my joystick.
 * TODO: read from a config file. */
static int JoystickButtonToSTScanCode[16] = {
	[0] = F1,
	[2] = LASER,
	[3] = F7,
	[4] = RTHRUST,
	[5] = THRUST,
	[6] = SPECIAL_TIME_DECREASE,
	[7] = SPECIAL_TIME_INCREASE,
	[11] = F9,
	[12] = LOOK_UP,
	[13] = LOOK_DOWN,
	[14] = LOOK_LEFT,
	[15] = LOOK_RIGHT,
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

	if (code == SPECIAL_TIME_INCREASE || code == SPECIAL_TIME_DECREASE) {
		if (pressed) {
			if (button_already_pressed)
				return;
			button_already_pressed = button;
		} else if (button != button_already_pressed)
			return;
		else
			button_already_pressed = 0;

		if (pressed && code == SPECIAL_TIME_INCREASE && currentTimeMode < 5)
			currentTimeMode++;
		if (pressed && code == SPECIAL_TIME_DECREASE && currentTimeMode)
			currentTimeMode--;
		inject_mouse_event(5 + (screen_w >> 5) * currentTimeMode, screen_h * 9 / 10, pressed);
	} else
		Input_PressSTKey(code, pressed);
}
