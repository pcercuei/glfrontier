
#include <SDL_endian.h>
#include <SDL_mouse.h>

#include "main.h"
#include "input.h"
#include "../m68000.h"
#include "shortcut.h"
#include "screen.h"

INPUT input;

void Call_GetMouseInput ()
{
	short *mouse_mov, *mouse_abs;
	unsigned long params;
	
	params = GetReg (REG_A7);
	params -= SIZE_WORD;
	/* Pointer passed to struct:
	 *   word mouse_motion_x
	 *   word mouse_motion_y
	 *   word mouse_buttons
	 */

	mouse_mov = (short*)(STRam + STMemory_ReadLong (params+SIZE_WORD));
	mouse_abs = (short*)(STRam + STMemory_ReadLong (params+SIZE_WORD+SIZE_LONG));

	/* lazy lazy lazy lazy */
	if ((abs (input.motion_x) > 100) || (abs (input.motion_y) > 100)) {
			//printf ("That fucking input bug! %d,%d\n", input.motion_x, input.motion_y);
			input.motion_x = input.motion_y = 0;
	}
	
	mouse_mov[0] = SDL_SwapBE16 (SDL_SwapBE16 (mouse_mov[0]) + input.motion_x);
	mouse_mov[1] = SDL_SwapBE16 (SDL_SwapBE16 (mouse_mov[1]) + input.motion_y);
	
	mouse_abs[0] = SDL_SwapBE16 (320*input.abs_x/screen_w);
	mouse_abs[1] = SDL_SwapBE16 (200*input.abs_y/screen_h);
	
	//if (input.mbuf_head != input.mbuf_tail) {
	//	mouse_mov[2] = SDL_SwapBE16 (0xf8 | input.mousebut_buf [input.mbuf_head++]);
	//	input.mbuf_head %= SIZE_KEYBUF;
	//} else {
		mouse_mov[2] = SDL_SwapBE16 (0xf8 | input.cur_mousebut_state);
	//}
	
	input.motion_x = input.motion_y = 0;
}

void Call_GetKeyboardEvent ()
{
	if ((input.buf_head) != (input.buf_tail)) {
		SetReg (REG_D0, input.key_buf [input.buf_head++]);
		input.buf_head %= SIZE_KEYBUF;
	} else {
		SetReg (REG_D0, 0);
	}
}




/* Interrupt as required */
void Input_Update ()
{
	if ((input.buf_head != input.buf_tail) ||
	    (input.motion_x) ||
	    (input.motion_y) ||
	    (input.mbuf_head != input.mbuf_tail)) {
		//FlagException (1);
	}
}

void Input_PressSTKey (unsigned char ScanCode, BOOL bPress)
{
	if (!bPress) ScanCode |= 0x80;
	input.key_buf [input.buf_tail++] = ScanCode;
	input.buf_tail %= SIZE_KEYBUF;
}

static void do_mouse_grab ()
{
	/* grab mouse on right-button hold for correct controls */
	if (input.cur_mousebut_state & 0x1) {
		SDL_WM_GrabInput (SDL_GRAB_ON);
	} else {
		SDL_WM_GrabInput (SDL_GRAB_OFF);
	}
}

void Input_MousePress (int button)
{
	if (button == SDL_BUTTON_RIGHT) input.cur_mousebut_state |= 0x1;
	else if (button == SDL_BUTTON_LEFT) input.cur_mousebut_state |= 0x2;
	else {
		return;
	}
	do_mouse_grab ();
	
	input.mousebut_buf [input.mbuf_tail++] = input.cur_mousebut_state;
	input.mbuf_tail %= SIZE_MOUSEBUF;
}

void Input_MouseRelease (int button)
{
	if (button == SDL_BUTTON_RIGHT) input.cur_mousebut_state &= ~0x1;
	else if (button == SDL_BUTTON_LEFT) input.cur_mousebut_state &= ~0x2;
	else return;
	input.mousebut_buf [input.mbuf_tail++] = input.cur_mousebut_state;
	input.mbuf_tail %= SIZE_MOUSEBUF;

	do_mouse_grab ();
}

