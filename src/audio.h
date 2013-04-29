#ifndef AUDIO_H
#define AUDIO_H

#include <SDL_types.h>

extern BOOL bDisableSound;

extern void Call_PlaySFX ();
extern void Call_PlayMusic ();
extern void Call_StopMusic ();
extern void Call_IsMusicPlaying ();
extern void Audio_Init(void);
extern void Audio_UnInit(void);
extern void Audio_EnableAudio(BOOL bEnable);

#endif  /* AUDIO_H */
