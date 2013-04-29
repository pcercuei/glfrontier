/*
  Hatari - main.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Main initialization and event handling routines.
*/

#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "../m68000.h"
#include "hostcall.h"
#include "input.h"
#include "keymap.h"
#include "screen.h"
#include "shortcut.h"


#define FORCE_WORKING_DIR                 /* Set default directory to cwd */


BOOL bQuitProgram=FALSE;                  /* Flag to quit program cleanly */
BOOL bUseFullscreen=FALSE;
BOOL bEmulationActive=TRUE;               /* Run emulation when started */
BOOL bAppActive = FALSE;
char szBootDiscImage[MAX_FILENAME_LENGTH] = { "" };

char szWorkingDir[MAX_FILENAME_LENGTH] = { "" };
char szCurrentDir[MAX_FILENAME_LENGTH] = { "" };

/*-----------------------------------------------------------------------*/
/*
  Error handler
*/
void Main_SysError(char *Error,char *Title)
{
  fprintf(stderr,"%s : %s\n",Title,Error);
}


/*-----------------------------------------------------------------------*/
/*
  Bring up message(handles full-screen as well as Window)
*/
int Main_Message(char *lpText, char *lpCaption/*,unsigned int uType*/)
{
  int Ret=0;

  /* Show message */
  fprintf(stderr,"%s: %s\n", lpCaption, lpText);

  return(Ret);
}


/*-----------------------------------------------------------------------*/
/*
  Pause emulation, stop sound
*/
void Main_PauseEmulation(void)
{
  if( bEmulationActive )
  {
    Audio_EnableAudio(FALSE);
    bEmulationActive = FALSE;
  }
}

/*-----------------------------------------------------------------------*/
/*
  Start emulation
*/
void Main_UnPauseEmulation(void)
{
  if( !bEmulationActive )
  {
    Audio_EnableAudio(1);
    bEmulationActive = TRUE;
  }
}

/* ----------------------------------------------------------------------- */
/*
  Message handler
  Here we process the SDL events (keyboard, mouse, ...) and map it to
  Atari IKBD events.
*/
void Main_EventHandler()
{
  SDL_Event event;

  while (SDL_PollEvent (&event))
   switch( event.type )
   {
    case SDL_QUIT:
       bQuitProgram = TRUE;
       SDL_Quit ();
       exit (0);
       break;
    case SDL_MOUSEMOTION:               /* Read/Update internal mouse position */
       input.motion_x += event.motion.xrel;
       input.motion_y += event.motion.yrel;
       input.abs_x = event.motion.x;
       input.abs_y = event.motion.y;
       break;
    case SDL_MOUSEBUTTONDOWN:
       Input_MousePress (event.button.button);
       break;
    case SDL_MOUSEBUTTONUP:
       Input_MouseRelease (event.button.button);
       break;
    case SDL_KEYDOWN:
       Keymap_KeyDown(&event.key.keysym);
       break;
    case SDL_KEYUP:
       Keymap_KeyUp(&event.key.keysym);
       break;
   }
  Input_Update ();
}


/*-----------------------------------------------------------------------*/
/*
  Check for any passed parameters
*/
void Main_ReadParameters(int argc, char *argv[])
{
  int i;

  /* Scan for any which we can use */
  for(i=1; i<argc; i++)
  {
    if (strlen(argv[i])>0)
    {
      if (!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h"))
      {
        printf("Usage:\n frontier [options]\n"
               "Where options are:\n"
               "  --help or -h          Print this help text and exit.\n"
               "  --fullscreen or -f    Try to use fullscreen mode.\n"
               "  --nosound             Disable sound (faster!).\n"
               "  --size w h            Start at specified window size.\n"
              );
        exit(0);
      }
      else if (!strcmp(argv[i],"--fullscreen") || !strcmp(argv[i],"-f"))
      {
        bUseFullscreen=TRUE;
      }
      else if ( !strcmp(argv[i],"--nosound") )
      {
        bDisableSound=TRUE;
      }
      else if ( !strcmp(argv[i],"--size") )
      {
	screen_h = 0;
	if (++i < argc)	screen_w = atoi (argv[i]);
	if (++i < argc)	screen_h = atoi (argv[i]);
	/* fe2 likes 1.6 aspect ratio until i fix the mouse position
	 * to 3d object position code... */
	if (screen_h == 0) screen_h = 5*screen_w/8;
      }
      else
      {
	      /* some time make it possible to read alternative
	       * names for fe2.bin from command line */
	      fprintf(stderr,"Illegal parameter: %s\n",argv[i]);
      }
    }
  }
}


/*-----------------------------------------------------------------------*/
/*
  Initialise emulation
*/
void Main_Init(void)
{
  /* Init SDL's video subsystem. Note: Audio and joystick subsystems
     will be initialized later (failures there are not fatal). */
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0)
  {
    fprintf(stderr, "Could not initialize the SDL library:\n %s\n", SDL_GetError() );
    exit(-1);
  }
  
  Screen_Init();
  Init680x0();                  /* Init CPU emulation */
  Audio_Init();
  Keymap_Init();

  if(bQuitProgram)
  {
    SDL_Quit();
    exit(-2);
  }
}


/*-----------------------------------------------------------------------*/
/*
  Un-Initialise emulation
*/
void Main_UnInit(void)
{
  Audio_UnInit();
  Screen_UnInit();

  /* SDL uninit: */
  SDL_Quit();
}

static Uint32 vbl_callback ()
{
	FlagException (0);
	return 20;
}

void sig_handler (int signum)
{
	if (signum == SIGSEGV) {
		printf ("Segfault! All is lost! Abandon ship!\n");
		Call_DumpDebug ();
		abort ();
	}
}

/*-----------------------------------------------------------------------*/
/*
  Main
*/
int main(int argc, char *argv[])
{
  signal (SIGSEGV, sig_handler);
	
  /* Generate random seed */
  srand( time(NULL) );

  /* Check for any passed parameters */
  Main_ReadParameters(argc, argv);

  /* Init emulator system */
  Main_Init();

  /* Switch immediately to fullscreen if user wants to */
  if( bUseFullscreen )
    Screen_ToggleFullScreen();

  SDL_AddTimer (20, &vbl_callback, NULL);
  
  /* Run emulation */
  Main_UnPauseEmulation();
  Start680x0();                 /* Start emulation */

  /* Un-init emulation system */
  Main_UnInit();

  return(0);
}


