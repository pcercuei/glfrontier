/*
  Hatari - screen.h

  This file is distributed under the GNU Public License, version 2 or at your
  option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_SCREEN_H
#define HATARI_SCREEN_H

#include <SDL_video.h>    /* for SDL_Surface */

extern unsigned long VideoBase;
extern unsigned char *VideoRaster;

extern unsigned int MainRGBPalette[256];
extern unsigned int CtrlRGBPalette[16];
extern int fe2_bgcol;

extern SDL_Surface *sdlscrn;
extern BOOL bGrabMouse;
extern BOOL bInFullScreen;
enum RENDERERS {
	R_OLD,
	R_GLWIRE,
	R_GL,
	R_MAX
};
extern enum RENDERERS use_renderer;

/* palette length changes as dynamic colours change */
extern int len_main_palette;
extern unsigned short MainPalette[256];
extern unsigned short CtrlPalette[16];

/* XXX this crap is only needed for the software renderer */
/* Do not use directly - they are just locations in STRam */
extern unsigned long logscreen, logscreen2, physcreen, physcreen2;
/* Use these instead. They read the value */
#define LOGSCREEN	(STRam + STMemory_ReadLong (logscreen))
#define LOGSCREEN2	(STRam + STMemory_ReadLong (logscreen2))
#define PHYSCREEN	(STRam + STMemory_ReadLong (physcreen))
#define PHYSCREEN2	(STRam + STMemory_ReadLong (physcreen2))

/* Returns new xpos */
extern int DrawStr (int xpos, int ypos, int col, unsigned char *str, bool shadowed);

extern void Screen_Init(void);
extern void Screen_UnInit(void);
extern void Screen_ToggleFullScreen (void);
extern void Screen_ToggleRenderer ();

extern void Nu_PutComplexStart ();
extern void Nu_PutTriangle ();
extern void Nu_PutQuad ();
extern void Nu_PutLine ();
extern void Nu_PutPoint ();
extern void Nu_PutTwinklyCircle ();
extern void Nu_PutCircle ();
extern void Nu_PutColoredPoint ();
extern void Nu_PutBezierLine ();
extern void Nu_ComplexStart ();
extern void Nu_ComplexSNext ();
extern void Nu_ComplexSBegin ();
extern void Nu_ComplexEnd ();
extern void Nu_3DViewInit ();
extern void Nu_InsertZNode ();
extern void Nu_ComplexStartInner ();
extern void Nu_ComplexBezier ();
extern void Nu_DrawScreen ();
extern void Nu_PutTeardrop ();
extern void Nu_PutOval ();
extern void Nu_IsGLRenderer ();
extern void Nu_GLClearArea ();
extern void Nu_QueueDrawStr ();
extern void Nu_PutCylinder ();
extern void Nu_PutBlob ();
extern void Nu_PutPlanet ();
extern void Nu_Put2DLine ();

extern int screen_w;
extern int screen_h;
extern int mouse_shown;
extern float hack;

#endif  /* ifndef HATARI_SCREEN_H */
