/*
  Hatari
*/

typedef void (*ShortCutFunction_t)(void);

enum {
  SHORTCUT_NOTASSIGNED,
  SHORTCUT_FULLSCREEN,
  SHORTCUT_MOUSEMODE,

  NUM_SHORTCUTS
};

typedef struct {
  unsigned short Key;
  BOOL bShiftPressed;
  BOOL bCtrlPressed;
} SHORTCUT_KEY;

extern char *pszShortCutTextStrings[NUM_SHORTCUTS+1];
extern char *pszShortCutF11TextString[];
extern char *pszShortCutF12TextString[];
extern SHORTCUT_KEY ShortCutKey;

extern void ShortCut_ClearKeys(void);
extern void ShortCut_CheckKeys(void);
extern void ShortCut_FullScreen(void);
extern void ShortCut_MouseMode(void);
extern void ShortCut_ColdReset(void);
