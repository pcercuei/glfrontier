#include <ini.h>
#include <SDL.h>

#include "main.h"
#include "input.h"
#include "joystick.h"
#include "screen.h"

#define JS_NB_BUTTONS_MAX	16

#define MODE_AVENTURE	0
#define MODE_BATTLE		1
#define MODE_MOUSE		2
#define MODE_LAST		3

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
#define LOOK_RIGHT	0x4d
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
#define SPECIAL_SWITCH_MODE		0x18
#define SPECIAL_MOUSE_BTN_LEFT		0x19
#define SPECIAL_MOUSE_BTN_MIDDLE	0x1a
#define SPECIAL_MOUSE_BTN_RIGHT		0x1b

#define ARRAY_SIZE(s) \
  (sizeof(s) ? sizeof(s) / sizeof(s[0]) : 0)

#define ACTION(_action) \
	{ .action = #_action, .code = _action }

static const char * const mode_names[] = {
	"Adventure", "Battle", "Mouse",
};

static struct {
	const char *action;
	unsigned char code;
} action_to_code[] = {
	/* TODO: "MOVE_LEFT", "MOVE_RIGHT", "ANALYSER" */
	ACTION(MOVE_UP),
	ACTION(MOVE_DOWN),
	ACTION(RTHRUST),
	ACTION(THRUST),
	ACTION(RADAR),
	ACTION(ZOOM_IN),
	ACTION(ZOOM_OUT),
	ACTION(LOOK_DOWN),
	ACTION(LOOK_UP),
	ACTION(LOOK_LEFT),
	ACTION(LOOK_RIGHT),
	ACTION(PAUSE),
	ACTION(MB4_PHOTO),
	ACTION(HYPERSPACE),
	ACTION(LASER),
	ACTION(ECM),
	ACTION(EJECT),
	ACTION(BOMB),
	ACTION(MISSILE),
	ACTION(MAP_CENTER),
	ACTION(F1),
	ACTION(F2),
	ACTION(F3),
	ACTION(F4),
	ACTION(F5),
	ACTION(F6),
	ACTION(F7),
	ACTION(F8),
	ACTION(F9),
	ACTION(F10),
	ACTION(ALT),

	ACTION(SPECIAL_TIME_DECREASE),
	ACTION(SPECIAL_TIME_INCREASE),
	ACTION(SPECIAL_SWITCH_MODE),
	ACTION(SPECIAL_MOUSE_BTN_LEFT),
	ACTION(SPECIAL_MOUSE_BTN_MIDDLE),
	ACTION(SPECIAL_MOUSE_BTN_RIGHT),
};

/* Joystick button to ST scan code mapping table */
static int JoystickButtonToSTScanCode[MODE_LAST][JS_NB_BUTTONS_MAX];

static unsigned char current_mode = MODE_MOUSE;


const char * mode_name(void)
{
	return mode_names[current_mode];
}

static void read_key_config(struct INI *ini, unsigned char mode)
{
	for (;;) {
		const char *key, *val;
		size_t lkey, lval;
		unsigned int i;
		unsigned char code = 0, button;
		int ret = ini_read_pair(ini, &key, &lkey, &val, &lval);
		if (ret <= 0)
			break;

		for (i = 0; i < ARRAY_SIZE(action_to_code); i++)
			if (!strncmp(action_to_code[i].action, key, lkey)) {
				code = action_to_code[i].code;
				break;
			}

		if (!code) {
			fprintf(stderr, "Skipping unknown key: %.*s\n", (int) lkey, key);
			continue;
		}

		button = atoi(val);
		JoystickButtonToSTScanCode[mode][button] = code;
	}
}

void joystick_read_config(const char *path)
{
	struct INI *ini = ini_open(path);
	if (!ini)
		return;

	for (;;) {
		const char *name;
		size_t len;
		if (ini_next_section(ini, &name, &len) <= 0)
			break;

		if (!strncmp(name, "Aventure Mode", len))
			read_key_config(ini, MODE_AVENTURE);
		else if (!strncmp(name, "Battle Mode", len))
			read_key_config(ini, MODE_BATTLE);
		else if (!strncmp(name, "Mouse Mode", len))
			read_key_config(ini, MODE_MOUSE);
		else
			fprintf(stderr, "WARNING: Skip unsupported section in config file");
	}

	ini_close(ini);
}

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

	code = JoystickButtonToSTScanCode[current_mode][button];
	if (!code)
		return;

	if (code == SPECIAL_MOUSE_BTN_LEFT) {
		if (pressed)
			Input_MousePress(SDL_BUTTON_LEFT);
		else
			Input_MouseRelease(SDL_BUTTON_LEFT);
		return;
	} else if (code == SPECIAL_MOUSE_BTN_RIGHT) {
		if (pressed)
			Input_MousePress(SDL_BUTTON_RIGHT);
		else
			Input_MouseRelease(SDL_BUTTON_RIGHT);
		return;
	} else if (code == SPECIAL_MOUSE_BTN_MIDDLE) {
		if (pressed)
			Input_MousePress(SDL_BUTTON_MIDDLE);
		else
			Input_MouseRelease(SDL_BUTTON_MIDDLE);
		return;
	}

	if (code == SPECIAL_SWITCH_MODE) {
		if (!pressed)
			return;

		if (++current_mode == MODE_LAST)
			current_mode = 0;
		printf("Switching to mode %s\n", mode_name());
		return;
	}

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

void joystick_motion(unsigned int axis, int value)
{
	extern int delta_x, delta_y, abs_delta_x, abs_delta_y;

	value >>= 13;
	if (value == 3)
		value++;

	if (current_mode == MODE_MOUSE) {
		if (axis == 0)
			abs_delta_x = value;
		else
			abs_delta_y = value;
		return;
	}

	if (!delta_x && !delta_y)
		Input_MousePress(SDL_BUTTON_RIGHT);

	if (axis == 0)
		delta_x = value;
	else
		delta_y = value;

	if (!delta_x && !delta_y)
		Input_MouseRelease(SDL_BUTTON_RIGHT);
}

int in_mouse_mode(void)
{
	return current_mode == MODE_MOUSE;
}
