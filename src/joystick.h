#ifndef JOYSTICK_H
#define JOYSTICK_H

extern int currentTimeMode;

void Keymap_JoystickUpDown(unsigned int button, int pressed);
void joystick_motion(unsigned int axis, int value);
void joystick_read_config(const char *path);
const char *mode_name(void);
int in_mouse_mode(void);

#endif
