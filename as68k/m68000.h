#ifndef _M68K_H
#define _M68K_H

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef signed int s32;
typedef signed short s16;
typedef signed char s8;

typedef void (*HOSTCALL) ();

extern char *STRam;

extern int Init680x0 ();
extern void Start680x0 ();
extern void FlagException (int num);
extern int GetReg (int reg);
extern void SetReg (int reg, int val);
extern void DumpRegs ();
extern unsigned int exception_handlers[32];

#define STMemory_ReadByte	MemReadByte
#define STMemory_ReadWord	MemReadWord
#define STMemory_ReadLong	MemReadLong
#define STMemory_WriteByte	MemWriteByte
#define STMemory_WriteWord	MemWriteWord
#define STMemory_WriteLong	MemWriteLong

#define STRAM_ADDR(Var)  ((u32)STRam+((u32)Var&0x00ffffff))

extern char MemReadByte (unsigned int pos);
extern short MemReadWord (unsigned int pos);
extern int MemReadLong (unsigned int pos);
extern void MemWriteByte (unsigned int pos, int val);
extern void MemWriteWord (unsigned int pos, int val);
extern void MemWriteLong (unsigned int pos, int val);

static inline u32 do_get_mem_long(u32 *a)
{
#ifdef __i386__
	u32 val = *a;
	__asm__ ("bswap	%0\n":"=r"(val):"0"(val));
	return val;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    return (*b << 24) | (*(b+1) << 16) | (*(b+2) << 8) | (*(b+3));
#else
    return *a;
#endif
}

static inline u16 do_get_mem_word(u16 *a)
{
#ifdef __i386__
	u16 val = *a;
	__asm__ ("xchgb %b0,%h0" : "=q" (val) :  "0" (val));
	return val;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    return (*b << 8) | (*(b+1));
#else
    return *a;
#endif
}

static inline u8 do_get_mem_byte(u8 *a)
{
    return *a;
}

static inline void do_put_mem_long(u32 *a, u32 v)
{
#ifdef __i386__
	__asm__ ("bswap	%0\n":"=r"(v):"0"(v));
	*a = v;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    
    *b = v >> 24;
    *(b+1) = v >> 16;    
    *(b+2) = v >> 8;
    *(b+3) = v;
#else
    *a = v;
#endif
}

static inline void do_put_mem_word(u16 *a, u16 v)
{
#ifdef __i386__
	__asm__ ("xchgb %b0,%h0" : "=q" (v) :  "0" (v));
	*a = v;
#elif LITTLE_ENDIAN
    u8 *b = (u8 *)a;
    
    *b = v >> 8;
    *(b+1) = v;
#else
    *a = v;
#endif
}

static inline void do_put_mem_byte(u8 *a, u8 v)
{
    *a = v;
}

#endif /* _M68K_H */
