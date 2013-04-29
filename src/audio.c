/*
  Hatari - audio.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/

#include <SDL.h>

#include "main.h"
#include "audio.h"
#include "../m68000.h"

#ifdef OGG_MUSIC
# include <vorbis/codec.h>
# include <vorbis/vorbisfile.h>
#endif /* OGG_MUSIC */

BOOL bDisableSound = FALSE;



#define SND_FREQ	22050

/* Converted frontier SFX to wav samples. */
#define MAX_SAMPLES	33
#define MAX_CHANNELS	4

typedef struct wav_stream {
	Uint8 *buf;
	int buf_pos;
	int buf_len;
	int loop; /* -1 no loop, otherwise specifies loop start pos */
} wav_stream;

wav_stream sfx_buf[MAX_SAMPLES];
wav_stream wav_channels[MAX_CHANNELS];

BOOL bSoundWorking = TRUE;                /* Is sound OK */
volatile BOOL bPlayingBuffer = FALSE;     /* Is playing buffer? */
int SoundBufferSize = 1024;               /* Size of sound buffer */

#ifdef OGG_MUSIC
static OggVorbis_File music_file;
static int music_mode;
static BOOL music_playing = FALSE;
static int enabled_tracks;

static void play_music (int track)
{
	char buf[32];
	FILE *f;

	if (music_playing == TRUE) ov_clear (&music_file);
	
	snprintf (buf, sizeof (buf), "music/%02d.ogg", track);

	f = fopen (buf, "rb");
	if (f == NULL) {
		music_playing = FALSE;
		return;
	}

	if (ov_open (f, &music_file, NULL, 0) < 0) {
		fprintf (stderr, "Libvorbis could not open '%s'. Is it an ogg file?\n", buf);
		fclose (f);
		music_playing = FALSE;
		return;
	}

	music_playing = TRUE;

}

int rand_tracknum ()
{
	int track;
	if (enabled_tracks == 0) return 999;
	do {
		track = rand () % 8;
	} while ((enabled_tracks & (1<<track)) == 0);
	return track;
}
#endif /* OGG_MUSIC */

void Call_PlayMusic ()
{
#ifdef OGG_MUSIC
	/* Playing mode in d0:
	 * -2 = play random track once
	 * -1 = play random tracks continuously
	 * 0+ = play specific track once
	 * d1:d2 is a mask of enabled tracks
	 */
	//printf ("Play track $%x. Enabled tracks $%x%x.\n", GetReg (0), GetReg (1), GetReg(2));
	music_mode = GetReg (0);

	enabled_tracks = 0;

	if (GetReg (1)&0xff000000) enabled_tracks |= 0x1;
	if (GetReg (1)&0xff0000) enabled_tracks |= 0x2;
	if (GetReg (1)&0xff00) enabled_tracks |= 0x4;
	if (GetReg (1)&0xff) enabled_tracks |= 0x8;
	if (GetReg (2)&0xff000000) enabled_tracks |= 0x10;
	if (GetReg (2)&0xff0000) enabled_tracks |= 0x20;
	if (GetReg (2)&0xff00) enabled_tracks |= 0x40;
	if (GetReg (2)&0xff) enabled_tracks |= 0x80;
	
	SDL_LockAudio ();
	switch (music_mode) {
		case -2:
			/* hyperspace and battle music --
			 * don't play blue danube or reward music */
			enabled_tracks &= ~0x40;
			enabled_tracks &= ~0x80;
			play_music (rand_tracknum ());
			break;
		case -1:
			/* any music */
			play_music (rand_tracknum ());
			break;
		default:
			play_music (music_mode);
			break;
	}
	SDL_UnlockAudio ();

#endif /* OGG_MUSIC */
}

#ifdef OGG_MUSIC
static void stop_music ()
{
	music_playing = FALSE;
	//printf ("Stop music.\n");
	ov_clear (&music_file);
}
#endif /* OGG_MUSIC */

void Call_StopMusic ()
{
#ifdef OGG_MUSIC
	SDL_LockAudio ();
	stop_music ();
	SDL_UnlockAudio ();
#endif /* OGG_MUSIC */
}

void Call_IsMusicPlaying ()
{
#ifdef OGG_MUSIC
	SetReg (0, music_playing);
#else
	SetReg (0, 0);
#endif /* OGG_MUSIC */
}

void Call_PlaySFX ()
{
	int sample, chan;

	SDL_LockAudio ();
	
	sample = (short) GetReg (REG_D0);
	chan = (short) GetReg (REG_D1);
	//printf ("Playing sample %d on channel %d.\n", sample, chan);

	wav_channels[chan].buf_pos = 0;
	wav_channels[chan].buf_len = sfx_buf[sample].buf_len;
	wav_channels[chan].buf = sfx_buf[sample].buf;
	wav_channels[chan].loop = sfx_buf[sample].loop;

	SDL_UnlockAudio ();
}

/*-----------------------------------------------------------------------*/
/*
  SDL audio callback function - copy emulation sound to audio system.
*/
void Audio_CallBack(void *userdata, Uint8 *pDestBuffer, int len)
{
	Sint8 *pBuffer;
	int i, j;
	short sample;
	BOOL playing = FALSE;
	
	pBuffer = pDestBuffer;
	
	for (i=0; i<MAX_CHANNELS; i++) {
		if (wav_channels[i].buf != NULL) {
			playing = TRUE;
			break;
		}
	}

	memset (pDestBuffer, 0, len);

#ifdef OGG_MUSIC
	if (music_playing) {
		i = 0;
		while (i < len) {
			int amt;
			int music_section;
			amt = ov_read (&music_file, (char *)&pDestBuffer[i],
					(len - i), 0, 2, 1, &music_section);
			i += amt;

			/* end of stream */
			if (amt == 0) {
				//printf ("ogg stream ended.\n");
				if (music_mode == -1) {
					play_music (rand_tracknum ());
				} else {
					stop_music ();
				}
				break;
			}
		}
	}
#endif /* OGG_MUSIC */
	
	if (!playing) return;
	
	for (i = 0; i < len; i+=4) {
		sample = 0;
		for (j=0; j<MAX_CHANNELS; j++) {
			if (wav_channels[j].buf == NULL) continue;
			sample += *(short *)(wav_channels[j].buf + wav_channels[j].buf_pos);
			wav_channels[j].buf_pos += 2;
			if (wav_channels[j].buf_pos >= wav_channels[j].buf_len) {
				/* end of sample. either loop or terminate */
				if (wav_channels[j].loop != -1) {
					wav_channels[j].buf_pos = wav_channels[j].loop;
				} else {
					wav_channels[j].buf = NULL;
				}
			}
		}
		/* stereo! */
		*((short*)pBuffer) += sample;
		pBuffer += 2;
		*((short*)pBuffer) += sample;
		pBuffer += 2;
	}
}

/*
 * Loaded samples must be SND_FREQ, 16-bit signed. Reject
 * other frequencies but convert 8-bit unsigned.
 */
void check_sample_format (SDL_AudioSpec *spec, Uint8 **buf, int *len, const char *filename)
{
	Uint8 *old_buf = *buf;
	short *new_buf;
	int i;

	if (spec->freq != SND_FREQ) {
		printf ("Sample %s is the wrong sample rate (wanted %dHz). Ignoring.\n", filename, SND_FREQ);
		SDL_FreeWAV (*buf);
		*buf = NULL;
		return;
	}

	if (spec->format == AUDIO_U8) {
		new_buf = malloc ((*len)*2);
		for (i=0; i<(*len); i++) {
			new_buf[i] = (old_buf[i] ^ 128) << 8;
		}
		*len *= 2;
		SDL_FreeWAV (old_buf);
		*buf = (char *)new_buf;
	} else if (spec->format != AUDIO_S16) {
		printf ("Sample %s is not 16-bit-signed or 8-bit unsigned. Ignoring.", filename);
		SDL_FreeWAV (*buf);
		*buf = NULL;
		return;
	}
}

/*-----------------------------------------------------------------------*/
/*
  Initialize the audio subsystem. Return TRUE if all OK.
  We use direct access to the sound buffer, set to a unsigned 8-bit mono stream.
*/
void Audio_Init(void)
{
	int i;
	char filename[32];
  SDL_AudioSpec desiredAudioSpec;    /* We fill in the desired SDL audio options here */

  /* Is enabled? */
  if(bDisableSound)
  {
    /* Stop any sound access */
    printf("Sound: Disabled\n");
    bSoundWorking = FALSE;
    return;
  }

  /* Init the SDL's audio subsystem: */
  if( SDL_WasInit(SDL_INIT_AUDIO)==0 )
  {
    if( SDL_InitSubSystem(SDL_INIT_AUDIO)<0 )
    {
      fprintf(stderr, "Could not init audio: %s\n", SDL_GetError() );
      bSoundWorking = FALSE;
      return;
    }
  }

  /* Set up SDL audio: */
  desiredAudioSpec.freq = SND_FREQ;
  desiredAudioSpec.format = AUDIO_S16;           /* 8 Bit unsigned */
  desiredAudioSpec.channels = 2;                /* Mono */
  desiredAudioSpec.samples = 1024;              /* Buffer size */
  desiredAudioSpec.callback = Audio_CallBack;
  desiredAudioSpec.userdata = NULL;

  if( SDL_OpenAudio(&desiredAudioSpec, NULL) )  /* Open audio device */
  {
    fprintf(stderr, "Can't use audio: %s\n", SDL_GetError());
    bSoundWorking = FALSE;
    //ConfigureParams.Sound.bEnableSound = FALSE;
    return;
  }

  SoundBufferSize = desiredAudioSpec.size;      /* May be different than the requested one! */

  for (i=0; i<MAX_SAMPLES; i++) {
	  snprintf (filename, sizeof (filename), "sfx/sfx_%02d.wav", i);
	  if (SDL_LoadWAV (filename, &desiredAudioSpec, &sfx_buf[i].buf,
				  &sfx_buf[i].buf_len) == NULL) {
	  	printf ("Error loading %s: %s\n", filename, SDL_GetError ());
		sfx_buf[i].buf = NULL;
	  }
	  check_sample_format (&desiredAudioSpec, &sfx_buf[i].buf, &sfx_buf[i].buf_len, filename);
	  
	  /* 19 (hyperspace) and 23 (noise) loop */
	  if (i == 19) sfx_buf[i].loop = SND_FREQ; /* loop to about 0.5 sec in */
	  else if (i == 23) sfx_buf[i].loop = 0;
	  else sfx_buf[i].loop = -1;
  }
  
  /* All OK */
  bSoundWorking = TRUE;
  /* And begin */
  Audio_EnableAudio(TRUE);
}


/*-----------------------------------------------------------------------*/
/*
  Free audio subsystem
*/
void Audio_UnInit(void)
{
  /* Stop */
  Audio_EnableAudio(FALSE);

  SDL_CloseAudio();
}


/*-----------------------------------------------------------------------*/
/*
  Start/Stop sound buffer
*/
void Audio_EnableAudio(BOOL bEnable)
{
  if(bEnable && !bPlayingBuffer)
  {
    /* Start playing */
    SDL_PauseAudio(FALSE);
    bPlayingBuffer = TRUE;
  }
  else if(!bEnable && bPlayingBuffer)
  {
    /* Stop from playing */
    SDL_PauseAudio(!bEnable);
    bPlayingBuffer = bEnable;
  }
}

