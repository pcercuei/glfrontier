
#ifndef _INPUT_H
#define _INPUT_H

#include <SDL_keyboard.h>

#define	SIZE_KEYBUF	16
#define SIZE_MOUSEBUF	16

typedef struct {
	unsigned char key_states[SDLK_LAST];
	unsigned char key_buf[SIZE_KEYBUF];
	unsigned char mousebut_buf[SIZE_MOUSEBUF];
	int buf_head, buf_tail;
	int mbuf_head, mbuf_tail;
	int cur_mousebut_state;
	
	/* change in mouse pos since last polled, absolute position */
	int motion_x, motion_y;
	int abs_x, abs_y;
	/* mouse button state when last polled, and now */
	int mouse_buttons_prev, mouse_buttons_now;
} INPUT;

extern INPUT input;

void Input_PressSTKey (unsigned char ScanCode, BOOL bPress);
void Call_GetMouseInput ();
void Call_GetKeyboardEvent ();
void Input_Update ();
void Input_MousePress (int button);
void Input_MouseRelease (int button);

#endif /* _INPUT_H */

