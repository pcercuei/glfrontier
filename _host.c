/*
 * This is used by the C output mode of the 68k assembler.
 * Loads 68k executable into m68k memory and applies all the relocations.
 * Executable is in an atari .prg format.
 */

typedef void (*HOSTCALL) ();
extern HOSTCALL hcalls[];

//#define likely(x)       __builtin_expect((x),1)
//#define unlikely(x)     __builtin_expect((x),0)

#define M68K_DEBUG

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef signed int s32;
typedef signed short s16;
typedef signed char s8;

#define LOAD_BASE	0x0

# ifdef M68K_DEBUG
int line_no;
# endif /* M68K_DEBUG */

#define MEM_SIZE	(0x110000)

union Reg {
	u16	word[2];
	u32	_u32;
	u16	_u16;
	u8	_u8;
	s32	_s32;
	s16	_s16;
	s8	_s8;
};

/* m68000 state ----------------------------------------------------- */
s8 m68kram[MEM_SIZE];
union Reg Regs[16];
/* status flags */
/* it is an optimisation that instead of having a Z (zero) flag we have
 * an nZ (not zero) flag, because this way we can usually just stick
 * the result in nZ. */
s32 N,nZ,V,C,X;
s32 bN,bnZ,bV,bC,bX;
s32 rdest; /* return address from interrupt. zero if none in service */
s32 exceptions_pending;
s32 exceptions_pending_nums[32];
u32 exception_handlers[32];
s8 *STRam;

#ifdef M68K_DEBUG
#define BOUNDS_CHECK
#if 0
static inline void BOUNDS_CHECK (u32 pos, int num)
{
	if ((pos+num) > MEM_SIZE) {
		printf ("Error. 68K memory access out of bounds (address $%x, line %d).\n", pos, line_no);
		abort ();
	}
}
#endif
#endif /* M68K_DEBUG */


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
	__asm__ ("rorw $8,%0" : "=q" (val) :  "0" (val));
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
	__asm__ ("rorw $8,%0" : "=q" (v) :  "0" (v));
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

#ifdef PART2

void SetReg (int reg, int val)
{
	Regs[reg]._s32 = val;
}

int GetReg (int reg)
{
	return Regs[reg]._s32;
}
s32 MemReadLong (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,4);
#endif /* M68K_DEBUG */
	return do_get_mem_long ((u32 *)(m68kram+pos));
}
s16 MemReadWord (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,2);
#endif /* M68K_DEBUG */
	return do_get_mem_word ((u16 *)(m68kram+pos));
}
s8 MemReadByte (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,1);
#endif /* M68K_DEBUG */
	return do_get_mem_byte ((u8 *)(m68kram+pos));
}
void MemWriteByte (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,1);
#endif /* M68K_DEBUG */
	do_put_mem_byte ((u8 *)(m68kram+pos), (u8)val);
}
void MemWriteWord (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,2);
#endif /* M68K_DEBUG */
	do_put_mem_word ((u16 *)(m68kram+pos), (u16)val);
}
void MemWriteLong (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,4);
#endif /* M68K_DEBUG */
	do_put_mem_long ((u32 *)(m68kram+pos), (u32)val);
}

int GetZFlag () { return !nZ; }
int GetNFlag () { return N; }
int GetCFlag () { return C; }
int GetVFlag () { return V; }
int GetXFlag () { return X; }
void SetZFlag (char val) { nZ = !val; }

#endif /* PART2 */

static inline s32 rdlong (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,4);
#endif /* M68K_DEBUG */
	return do_get_mem_long ((u32 *)(m68kram+pos));
}
static inline s16 rdword (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,2);
#endif /* M68K_DEBUG */
	return do_get_mem_word ((u16 *)(m68kram+pos));
}
static inline s8 rdbyte (u32 pos)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,1);
#endif /* M68K_DEBUG */
	return do_get_mem_byte ((u8 *)(m68kram+pos));
}
static inline void wrbyte (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,1);
#endif /* M68K_DEBUG */
	do_put_mem_byte ((u8 *)(m68kram+pos), (u8)val);
}
static inline void wrword (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,2);
#endif /* M68K_DEBUG */
	do_put_mem_word ((u16 *)(m68kram+pos), (u16)val);
}
static inline void wrlong (u32 pos, int val)
{
#ifdef M68K_DEBUG
	BOUNDS_CHECK (pos,4);
#endif /* M68K_DEBUG */
	do_put_mem_long ((u32 *)(m68kram+pos), (u32)val);
}
#ifdef PART1
void FlagException (int num)
{
	if (exception_handlers[num]) {
		exceptions_pending |= 1<<num;
		exceptions_pending_nums[num]++;
	}
}

/* bin loader (in-place) ------------------------------------------------------- */
static s32 buf_pos;

static s32 get_fixup (s32 reloc, s32 code_end)
{
	s32 old_bufpos;
	s32 next;
	static s32 reloc_pos;

	old_bufpos = buf_pos;
	if (reloc == 0) {
		buf_pos = code_end;
		reloc = rdlong (buf_pos);
		buf_pos += 4;
		reloc_pos = buf_pos;
		buf_pos = old_bufpos;
		if (reloc == 0) return 0;
		else return reloc+0x1c+LOAD_BASE;
	} else {
		buf_pos = reloc_pos;
again:
		next = (u8)rdbyte (buf_pos);
		buf_pos++;
		if (next == 0) {
			buf_pos = old_bufpos;
			return 0;
		} else if (next == 1) {
			reloc += 254;
			goto again;
		}
		else reloc += next;
		reloc_pos = buf_pos;
		buf_pos = old_bufpos;
		return reloc;
	}
}

void load_binfile (const char *bin_filename)
{
	s32 reloc, next, pos, code_end, len, i = 0;
	FILE *f;

	if ((f = fopen (bin_filename, "r")) == NULL) {
		fprintf (stderr, "Error opening 68k-binary '%s'\n", bin_filename);
		//SDL_Quit ();
		exit (-2);
	}
	fseek (f, 0, SEEK_END);
	len = ftell (f);
	fseek (f, 0, SEEK_SET);
	assert (len+LOAD_BASE < MEM_SIZE);
	fread (m68kram+LOAD_BASE, 1, len, f);
	fclose (f);
	
	buf_pos = LOAD_BASE + 2;
	code_end = LOAD_BASE + 0x1c + rdlong (buf_pos);
	
	i=0;
	reloc = get_fixup (0, code_end);
	while (reloc) {
		i++;
		pos = buf_pos;
		/* address to be modified */
		buf_pos = reloc;
		next = rdlong (buf_pos);
		next += LOAD_BASE;
		wrlong (buf_pos, next);
		if (next > code_end) {
			fprintf (stderr, "Reloc 0x%x (0x%x) out of range..\n", next, reloc+LOAD_BASE);
		}
		buf_pos = pos;
		reloc = get_fixup (reloc, code_end);
	}
	fprintf (stderr, "%s: 0x%x bytes (code end 0x%x), %d fixups; loaded at 0x%x.\n", bin_filename, len, code_end, i, LOAD_BASE);
}

#ifdef M68K_DEBUG
void m68k_print_line_no ()
{
	printf ("Hello. At fe2.s line %d.\n", line_no);
	fflush (stdout);
}
#endif /* M68K_DEBUG */

#endif /* PART1 */
