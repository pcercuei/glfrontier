/*
  Hatari

  Shortcut keys
*/

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "screen.h"
#include "shortcut.h"
#include "hostcall.h"
#include "../m68000.h"


/* List of possible short-cuts(MUST match SHORTCUT_xxxx) */
char *pszShortCutTextStrings[NUM_SHORTCUTS+1] = {
  "(not assigned)",
  "Full Screen",
  "Mouse Mode",
  NULL  /*term*/
};

char *pszShortCutF11TextString[] = {
  "Full Screen",
  NULL  /*term*/
};

char *pszShortCutF12TextString[] = {
  "Mouse Mode",
  NULL  /*term*/
};

ShortCutFunction_t pShortCutFunctions[NUM_SHORTCUTS] = {
  NULL,
  ShortCut_FullScreen,
  ShortCut_MouseMode,
};

SHORTCUT_KEY ShortCutKey;

/*-----------------------------------------------------------------------*/
/*
  Clear shortkey structure
*/
void ShortCut_ClearKeys(void)
{
  /* Clear short-cut key structure */
  memset (&ShortCutKey,0, sizeof(SHORTCUT_KEY));
}

/*-----------------------------------------------------------------------*/
/*
  Check to see if pressed any shortcut keys, and call handling function
*/
void ShortCut_CheckKeys(void)
{
  /* Check for supported keys: */
  switch(ShortCutKey.Key) {
     case SDLK_F11:                  /* Switch between fullscreen/windowed mode */
       ShortCut_FullScreen();
       break;
     case SDLK_m:                    /* Toggle mouse mode */
       ShortCut_MouseMode();
       break;
     case SDLK_q:                    /* Quit program */
       SDL_Quit ();
       exit (0);
       bQuitProgram = TRUE;
       break;
     case SDLK_d:
       Call_DumpDebug ();
       break;
     case SDLK_e:
       Screen_ToggleRenderer ();
       break;
  }

    /* And clear */
    ShortCut_ClearKeys();
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle full-screen
*/
void ShortCut_FullScreen(void)
{
	Screen_ToggleFullScreen ();
}


/*-----------------------------------------------------------------------*/
/*
  Shortcut to toggle mouse mode
*/
void ShortCut_MouseMode(void)
{
  bGrabMouse = !bGrabMouse;        /* Toggle flag */

  if(!bInFullScreen)
  {
    if(bGrabMouse)
    {
      SDL_WM_GrabInput(SDL_GRAB_ON);
    }
    else
    {
      SDL_WM_GrabInput(SDL_GRAB_OFF);
    }
  }
}


