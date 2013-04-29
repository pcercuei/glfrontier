#ifndef _M68K_H
#define _M68K_H

/* use this type to mean m68k pointer, which becomes a host machine
 * pointer when STRam (mem base) is added to it). */
typedef unsigned int p68K;

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef signed int s32;
typedef signed short s16;
typedef signed char s8;

typedef void (*HOSTCALL) ();
extern HOSTCALL hcalls [];

extern char *STRam;
extern int line_no;

extern int Init680x0 ();
extern void Start680x0 ();
extern void FlagException (int num);
extern int GetReg (int reg);
extern void SetReg (int reg, int val);
extern p68K exception_handlers[32];

#define STMemory_ReadByte	MemReadByte
#define STMemory_ReadWord	MemReadWord
#define STMemory_ReadLong	MemReadLong
#define STMemory_WriteByte	MemWriteByte
#define STMemory_WriteWord	MemWriteWord
#define STMemory_WriteLong	MemWriteLong

#define STRAM_ADDR(Var)  ((u32)STRam+((u32)Var&0x00ffffff))

extern char MemReadByte (p68K pos);
extern short MemReadWord (p68K pos);
extern int MemReadLong (p68K pos);
extern void MemWriteByte (p68K pos, int val);
extern void MemWriteWord (p68K pos, int val);
extern void MemWriteLong (p68K pos, int val);

extern int GetXFlag ();
extern int GetZFlag ();
extern int GetNFlag ();
extern int GetVFlag ();
extern int GetCFlag ();

extern void SetZFlag (char val);

#endif /* _M68K_H */
