
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "dict.h"
#include "as68k.h"
#include "output.h"

static op_t pending;
static enum M68K_OPS next_op_type;
static char *pending_labels, *cur_labels;
static int cur_bitpos;
/* last instruction was a jsr/bsr, so must create an entry
 * in the computed jump table for return point (this instr) */
static int return_target;

#define LABBUF_LEN	32768

/* enable bounds checking and debug info in the generated turd */
#define GEN_DEBUG
//#define DUMP_LOADS_OF_CRAP

/* which flags an M68K_OP thingy may set */
#define fZ	(1<<0)
#define fNVC	(1<<1)
#define fX	(1<<2)
#define fNZVCX	(fZ | fNVC | fX)
#define fNZVC	(fZ | fNVC)
#define fNVCX	(fX | fNVC)
#define _fSafe	0
/* _fSafe are ones i haven't got round to doing yet. */

/* does the flag need to be set? if the next instruction will
 * set the flag then no. */
#define ifZ	if(!(sets_flags[next_op_type] & fZ))
#define ifNVC	if(!(sets_flags[next_op_type] & fNVC))
#define ifZNVC	if((!(sets_flags[next_op_type] & fZ)) || (!(sets_flags[next_op_type] & fNVC)))
#define ifNVCX	if((!(sets_flags[next_op_type] & fNVCX)))
#define ifX	if(!(sets_flags[next_op_type] & fX))

/* XXX ONLY SET X FLAG FOR INSTRUCTIONS THAT OVERWRITE
 * THE X FLAG __WITHOUT__ USING IT */

static int sets_flags[OP_MAX+1] = {
	_fSafe,	/* OP_NONE */
	0, // OP_JMP
	fZ, // OP_BCHG
	fZ, // OP_BCLR
	fZ, // OP_BSET
	fZ, // OP_BTST
	fNZVC, // OP_MULS
	fNZVC, // OP_MULU,
	fNZVC, // OP_AND
	fNZVC, // OP_OR
	fNZVC, // OP_XOR
	fNZVC, // OP_NOT
	fNZVC, // OP_NEGX
	fNZVC, // OP_NEG
	fNZVC, // OP_DIVU
	fNZVC, // OP_DIVS
	fNZVC, // OP_ASL,
	fNZVCX, // OP_LSL
	fNZVC, // OP_ROXR
	fNZVC, // OP_ROXL
	fNZVC, // OP_ROL
	fNZVC, // OP_ROR
	fNZVC, // OP_ASR
	fNZVC, // OP_LSR,
	0, // OP_MOVEM
	fNZVC, // OP_SUBX
	fNZVC, // OP_ADDX
	fNZVCX, // OP_ADD
	fNZVC, // OP_CMPA
	fNZVC, // OP_CMP
	fNZVCX, // OP_SUB,
	_fSafe, // OP_ADDA
	_fSafe, // OP_SUBA
	fNZVC, // OP_SWAP
	_fSafe, // OP_JSR
	0, // OP_BCC
	0, // OP_DBCC
	fNZVC, // OP_CLR,
	0, // OP_EXG
	fNZVC, // OP_EXT
	0, // OP_RTE
	0, // OP_RTS
	0, // OP_ILLEGAL
	0, // OP_HCALL
	0, // OP_LINK,
	0, // OP_UNLK
	fNZVC, // OP_MOVE
	_fSafe, // OP_MOVEA
	fNZVC, // OP_TST
	0, // OP_PEA
	0, // OP_LEA
	0, // OP_SCC
	_fSafe, // OP_MAX.....
};

FILE *asm_out;

static void output_op (op_t *op, op_t *next);
static void init_regalloc ();
static void xflush_all ();

/* raw output */
static void rout (const char *str)
{
	fputs (str, asm_out);
}

/* formatted output */
static void xout (const char *format, ...)
{
	va_list argptr;

	va_start (argptr, format);
	vfprintf (asm_out, format, argptr);
	va_end (argptr);
}
static void xln (const char *format, ...)
{
	va_list argptr;

	fputc ('\t', asm_out);
	va_start (argptr, format);
	vfprintf (asm_out, format, argptr);
	va_end (argptr);
	fputc ('\n', asm_out);
}

static int regpos (int num)
{
	assert ((num >=0) && (num < 16));
	return -64 + 4*num;
}

#ifdef GEN_DEBUG
static void set_line (int num)
{
	char buf[256];

	snprintf (buf, sizeof (buf), "	movl	$%d, line_no\n", num);
	strcat (cur_labels, buf);
}
#endif

static int pending_flush, cur_flush;

void i386_label (const char *lab)
{
	char buf[256];

	/* lines that might be jumped to must have a consistent
	 * register state, so flush registers to memory before
	 * these lines. */
	cur_flush = 1;
	
	snprintf (buf, sizeof (buf), "__N%s:\n", lab);
	strcat (cur_labels, buf);

	add_fixup (0, C_ADDR, lab);
}

void i386_addr_label (int labelled)
{
	char buf[256];

	if (labelled) {
		cur_flush = 1;
	}
	
	if (labelled || return_target) {
		snprintf (buf, sizeof (buf), "__l%x", get_bitpos ());
		add_fixup (get_bitpos (), C_ADDR, buf);
	
		snprintf (buf, sizeof (buf), "__l%x:\n", get_bitpos ());
		strcat (cur_labels, buf);
	} else {
#ifdef GEN_DEBUG
		snprintf (buf, sizeof (buf), "/* 0x%x */\n", get_bitpos ());
		strcat (cur_labels, buf);
#endif
	}
	
	cur_bitpos = get_bitpos ();
	
#ifdef GEN_DEBUG
	set_line (line_no);

#ifdef DUMP_LOADS_OF_CRAP
	strcat (cur_labels, "	call DumpRegsChanged\n");
#endif /* DUMP_LOADS_OF_CRAP */
#endif /* GEN_DEBUG */
}

#ifdef WIN32
void winsym (const char *sym)
{
	xout ("#define %s _%s\n", sym, sym);
}
#endif /* WIN32 */

void i386_begin (const char *src_filename, const char *bin_filename)
{
	char buf[128];
	snprintf (buf, sizeof (buf), ".%s.S", src_filename);
	if ((asm_out = fopen (buf, "w"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}

	init_regalloc ();

	pending_labels = NULL;
	cur_labels = calloc (LABBUF_LEN, 1);
	
	xout ("#define MEM_SIZE	0x110000\n");
	xout ("#define PROC_STATE 64\n");
	xout ("#define MemBase	STData+PROC_STATE\n\n");

	xln (".data");
	/* memory size + registers */
	xln (".comm	STData,MEM_SIZE+PROC_STATE");
	xln (".comm	X,4");
	xln (".comm	N,4");
	xln (".comm	Z,4");
	xln (".comm	V,4");
	xln (".comm	C,4");
	/* register backing store for during exception handling */
	xln (".comm	bX,4");
	xln (".comm	bN,4");
	xln (".comm	bZ,4");
	xln (".comm	bV,4");
	xln (".comm	bC,4");
	/* exception return address */
	xln (".comm	rdest,4");
	xln (".comm	exceptions_pending_mask,4");
	xln (".comm	exceptions_pending_nums,32");
	xln (".comm	exception_handlers,4*32");
	xln (".comm	line_no,4");
#ifdef GEN_DEBUG
	xout ("debugstr:	.string \"$%%x $%%x $%%x $%%x $%%x $%%x $%%x $%%x*$%%x $%%x $%%x $%%x $%%x $%%x $%%x $%%x:%%d\\n\"\n");
#endif
	xout ("	.global	STRam\n");
	xout ("STRam:	.long	STData+PROC_STATE\n");
	
	xout ("	.section	.rodata\n");
	xout ("bin_filename:	.string \"%s\"\n", bin_filename);
	xout ("open_mode:	.string \"r\"\n\n");
	xout ("dreg_fmt:		.string \"D%%d = $%%08x\t\"\n");
	xout ("areg_fmt:	.string \"A%%d = $%%08x\\n\"\n");
	xout ("cr:		.string \"\\n\"\n");
	xout ("cc_names:	.string \"XNZVC\"\n");
	xout ("hex_fmt:		.string \"$%%02x \"\n");
	xout ("illegal_fmt:	.string \"Illegal instruction at line %%d.\\n\"\n");

	xout ("	.text\n");
	
	xout (".globl SetZFlag\n");
	xout ("SetZFlag:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movb	%al,Z\n"
"	ret\n");
	
	xout (".globl GetXFlag\n");
	xout ("GetXFlag:\n");
	rout (
"	movl	X,%eax\n"
"	ret\n");
	
	xout (".globl GetZFlag\n");
	xout ("GetZFlag:\n");
	rout (
"	movl	Z,%eax\n"
"	ret\n");
	
	xout (".globl GetCFlag\n");
	xout ("GetCFlag:\n");
	rout (
"	movl	C,%eax\n"
"	ret\n");
	
	xout (".globl GetNFlag\n");
	xout ("GetNFlag:\n");
	rout (
"	movl	N,%eax\n"
"	ret\n");
	
	xout (".globl GetVFlag\n");
	xout ("GetVFlag:\n");
	rout (
"	movl	V,%eax\n"
"	ret\n");

	xout (".globl DumpRegs\n");
	xout ("DumpRegs:\n");
	rout (
"	pushl	%ebp\n"
"	pushl	%ebx\n"
"	pushl	%ecx\n"
"	pushl	%esi\n"
"	movl	$STData,%ebx\n"
"	xorl	%eax,%eax\n"
"1:	pushl	%eax\n"
"	pushl	(%ebx,%eax,4)\n"
"	pushl	%eax\n"
"	pushl	$dreg_fmt\n"
"	call	printf\n"
"	addl	$12,%esp\n"
"	movl	(%esp),%eax\n"
"	pushl	8*4(%ebx,%eax,4)\n"
"	pushl	%eax\n"
"	pushl	$areg_fmt\n"
"	call	printf\n"
"	addl	$12,%esp\n"
"	popl	%eax\n"
"	inc	%eax\n"
"	cmpl	$8,%eax\n"
"	jl	1b\n"
/* print status flags */
"	movb	X,%al\n"
"	movl	$0,%ecx\n"
"	call	print_scat\n"
"	movb	N,%al\n"
"	movl	$1,%ecx\n"
"	call	print_scat\n"
"	movb	Z,%al\n"
"	movl	$2,%ecx\n"
"	call	print_scat\n"
"	movb	V,%al\n"
"	movl	$3,%ecx\n"
"	call	print_scat\n"
"	movb	C,%al\n"
"	movl	$4,%ecx\n"
"	call	print_scat\n"
"	pushl	$cr\n"
"	call	printf\n"
"	addl	$4,%esp\n"
/* print memory contents */

"	movl	$STData+PROC_STATE,%esi\n"
"	movl	$MEM_SIZE,%ebx\n"
"1:	movzbl	(%esi),%eax\n"
"	pushl	%eax\n"
"	pushl	$hex_fmt\n"
"	call	printf\n"
"	addl	$8,%esp\n"
"	inc	%esi\n"
"	dec	%ebx\n"

"	testl	$15,%ebx\n"
"	jz	3f\n"

"2:	testl	%ebx,%ebx\n"
"	jnz	1b\n"


"	popl	%esi\n"
"	popl	%ecx\n"
"	popl	%ebx\n"
"	popl	%ebp\n"
"	ret\n"

"3:	pushl	$cr\n"
"	call	printf\n"
"	addl	$4,%esp\n"
"	jmp	2b\n"

"print_scat:\n"
"	xorl	%ebx,%ebx\n"
"	cmpb	%al,%bl\n"
"	jz	1f\n"
"	movb	cc_names(%ecx),%al\n"
"	pushl	%eax\n"
"	call	putchar\n"
"	addl	$4,%esp\n"
"1:	ret\n"
);
	
	xout (".globl FlagException\n");
	xout ("FlagException:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	btsl	%eax,exceptions_pending_mask\n"
"	incb	exceptions_pending_nums(%eax)\n"
"	ret\n");
	
	xout (".globl MemReadByte\n");
	xout ("MemReadByte:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movsbl	STData+PROC_STATE(%eax),%eax\n"
"	ret\n");

	xout (".globl MemReadWord\n");
	xout ("MemReadWord:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movswl	STData+PROC_STATE(%eax),%eax\n"
"	xchg	%al,%ah\n"
"	ret\n");

	xout (".globl MemReadLong\n");
	xout ("MemReadLong:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movl	STData+PROC_STATE(%eax),%eax\n"
"	bswap	%eax\n"
"	ret\n");

	xout (".globl MemWriteByte\n");
	xout ("MemWriteByte:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movl	8(%esp),%edx\n"
"	movb	%dl,STData+PROC_STATE(%eax)\n"
"	ret\n");

	xout (".globl MemWriteWord\n");
	xout ("MemWriteWord:\n");
	rout (
"	movl	8(%esp),%edx\n"
"	movl	4(%esp),%eax\n"
"	xchg	%dl,%dh\n"
"	movw	%dx,STData+PROC_STATE(%eax)\n"
"	ret\n");

	xout (".globl MemWriteLong\n");
	xout ("MemWriteLong:\n");
	rout (
"	movl	8(%esp),%edx\n"
"	movl	4(%esp),%eax\n"
"	bswap	%edx\n"
"	movl	%edx,STData+PROC_STATE(%eax)\n"
"	ret\n");

	xout (".globl GetReg\n");
	xout ("GetReg:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movl	STData(,%eax,4),%eax\n"
"	ret\n");

	xout (".globl SetReg\n");
	xout ("SetReg:\n");
	rout (
"	movl	4(%esp),%eax\n"
"	movl	8(%esp),%edx\n"
"	movl	%edx,STData(,%eax,4)\n"
"	ret\n");

	xout (".globl Init680x0\n");
	xout ("Init680x0:\n");
	rout (
"	pushl	%esi\n"
"	pushl	%ebx\n"
"	pushl	$open_mode\n"
"	pushl	$bin_filename\n"
"	call	fopen\n"
"	movl	%eax, %ebx\n"
"	popl	%eax\n"
"	xorl	%eax, %eax\n"
"	testl	%ebx, %ebx\n"
"	popl	%edx\n"
"	je	1f\n"
"	pushl	$2\n"
"	pushl	$0\n"
"	pushl	%ebx\n"
"	call	fseek\n"
"	pushl	%ebx\n"
"	call	ftell\n"
"	pushl	$0\n"
"	movl	%eax, %esi\n"
"	pushl	$0\n"
"	pushl	%ebx\n"
"	call	fseek\n"
"	addl	$28, %esp\n"
"	xorl	%eax, %eax\n"
"	cmpl	$MEM_SIZE, %esi\n"
"	jge	1f\n"
"	pushl	%ebx\n"
"	pushl	%esi\n"
"	pushl	$1\n"
"	pushl	$STData+PROC_STATE\n"
"	call	fread\n"
"	pushl	%ebx\n"
"	call	fclose\n"
"	movl	$1, %eax\n"
"	addl	$20, %esp\n"
"1:\n"
"	popl	%ebx\n"
"	popl	%esi\n"
"	ret\n");

	xout (".globl Start680x0\n");
	xout ("Start680x0:\n");
	xout ("	pushl	%%ebp\n");
	xout ("	pushl	%%esi\n");
	xout ("	pushl	%%edi\n");
	xout ("	pushl	%%edx\n");
	xout ("	pushl	%%ecx\n");
	xout ("	pushl	%%ebx\n");
	xout ("	movl	$STData+PROC_STATE,%%ebp\n\n");
	//xout ("	movl	$0x110000,Regs+15*4");
//	xout ("	movl	$0x1c,%%edx");

//	xout ("	jmp	jumptable");
}

static void do_pending ();

void i386_end (const char *src_filename)
{
	/* some more crap needs to be added to the top */
	int prev, i;
	char c, buf[128];
	FILE *f;
	struct Fixup *fix;
	struct Label *lab;
	
	/* output last instruction */
	next_op_type = OP_NONE;
	do_pending ();
	
	xout ("\n");
	xflush_all ();
	xln ("popl	%%ebx");
	xln ("popl	%%ecx");
	xln ("popl	%%edx");
	xln ("popl	%%edi");
	xln ("popl	%%esi");
	xln ("popl	%%ebp");
	xln ("ret");

	fclose (asm_out);

	snprintf (buf, sizeof (buf), "%s.S", src_filename);
	if ((asm_out = fopen (buf, "w"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}

#ifdef WIN32
	winsym ("printf");
	winsym ("hcalls");
	winsym ("fopen");
	winsym ("fseek");
	winsym ("ftell");
	winsym ("fclose");
	winsym ("abort");
	winsym ("putchar");
	winsym ("fread");
	winsym ("SetReg");
	winsym ("GetReg");
	winsym ("STRam");
	winsym ("MemReadLong");
	winsym ("MemReadWord");
	winsym ("MemReadByte");
	winsym ("MemWriteLong");
	winsym ("MemWriteWord");
	winsym ("MemWriteByte");
	winsym ("GetZFlag");
	winsym ("GetNFlag");
	winsym ("GetVFlag");
	winsym ("GetCFlag");
	winsym ("GetXFlag");
	winsym ("Init680x0");
	winsym ("Start680x0");
	winsym ("FlagException");
	winsym ("line_no");
	winsym ("exception_handlers");
#endif /* WIN32 */
	
	/* address 'fixups' */
	fix = fix_first;
	for (; fix != NULL; fix = fix->next) {
		if (fix->size != C_ADDR) continue;

		lab = get_label (fix->label);
		if ((fix->adr != 0) || (!lab)) {
			continue;
		}
		xout ("#define __D%s 0x%x\n", lab->name, lab->val+BASE);
	}

	/* computed jump table */
	xln (".align 4");
	xout ("jtab:\n");
	
	prev = -2;
	fix = fix_first;
	for (; fix != NULL; fix = fix->next) {
		if (fix->size != C_ADDR) continue;

		lab = get_label (fix->label);
		
		if (!fix->adr) {
			/* ignore. let pass2 throw the error. */
			continue;
		}
		/* add ones since prev */
		for (i=prev+2; i<fix->adr; i+=2) {
			xln (".long	bad_jmp");
		}
		xln (".long	%s", fix->label);
		prev = fix->adr;
	}
	xout ("jumptable:\n");
	xln ("jmp	*jtab(,%%eax,2)");
#ifdef GEN_DEBUG
	xout ("	.section	.rodata\n");
	xout ("bad_jmp_str:	.string \"Error. Bad jump target: $%%x at line %%d.\\n\"\n");
	xln (".text");
	xout ("bad_jmp:\n");
	xln ("shll	$1,%%eax");
	xln ("pushl	line_no");
	xln ("pushl	%%eax");
	xln ("pushl	$bad_jmp_str");
	xln ("call	printf");
	xln ("addl	$8,%%esp");
	xln ("call	abort");
#else
	xout ("bad_jmp:\n");
	xout ("bad_jmp2:\n");
	xln ("call	abort");
#endif
	
	/* the code we made */
	snprintf (buf, sizeof (buf), ".%s.S", src_filename);
	if ((f = fopen (buf, "r"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}
	while ((c = fgetc (f)) != EOF) fputc (c, asm_out);
	fclose (f);
	remove (buf);

	xout ("handle_exception:\n");
	
	/* no exceptions while in an exception handler */
	xln ("movl	rdest,%%ebx");
	xln ("testl	%%ebx,%%ebx");
	xln ("jnz	jumptable");

	xln ("movl	%%eax,rdest");
	
	xln ("movl	exceptions_pending_mask,%%edx");
	xln ("xorl	%%ecx,%%ecx");
	xout ("1:\n");
	xln ("btl	%%ecx,%%edx");
	xln ("jnc	2f");

	/* yay. exception num %ecx pending */
	xln ("movl	$exception_handlers,%%ebx");
	xln ("movb	exceptions_pending_nums(%%ecx),%%al");
	xln ("decb	%%al");
	xln ("jnz	3f");
	xln ("btrl	%%ecx,exceptions_pending_mask");
	xln ("3: movb	%%al,exceptions_pending_nums(%%ecx)");
	xln ("movl	(%%ebx,%%ecx,4),%%eax");
	xln ("movb	X,%%bh");
	xln ("movb	N,%%bl");
	xln ("movb	Z,%%ch");
	xln ("movb	V,%%cl");
	xln ("movb	C,%%dh");
	xln ("movb	%%bh,bX");
	xln ("movb	%%bl,bN");
	xln ("movb	%%ch,bZ");
	xln ("movb	%%cl,bV");
	xln ("movb	%%dh,bC");
	xln ("jmp	jumptable");

	xout ("2:\n");
	xln ("inc	%%ecx");
	xln ("cmpl	$32,%%ecx");
	xln ("jl	1b");
	/* shouldn't get here... */
	xln ("movl	rdest,%%eax");
	xln ("movl	$0,rdest");
	xln ("jmp	jumptable");
	fclose (asm_out);
}

static int is_reg (ea_t *ea)
{
	return ((ea->mode == 0) || (ea->mode == 1));
}

static int is_immediate (ea_t *ea)
{
	return ((ea->mode == 7) && (ea->reg == 4));
}

/* only x86 regs used in effective address shit. */
enum X86REG {
	EAX, EBX, ECX, EDX, ESI, EDI, X86REG_MAX
};

/* m68k -> i386 reg allocation. age goes up with disuse */
struct reg_alloc {
	int m68k_reg;
	int age;
	int dirty;
	int locked;
};

#define xreg_unlock(reg) i386_regalloc[reg].locked--;
#define xdirty_reg(reg)	{if (reg!=-1) i386_regalloc[reg].dirty = 1; }

/* set m68k_reg to REG_TEMP when allocating a reg as a temporary value,
 * ie not holding a specific m68k reg's value */
#define REG_TEMP	16

static struct reg_alloc i386_regalloc[X86REG_MAX];

/* x86 asm syntax is a pain in the arse for auto-generated stuff */
static const char *reg_turd[3][X86REG_MAX] = {
	{ "%al", "%bl", "%cl", "%dl", "%invalid1", "%invalid2" },
	{ "%ax", "%bx", "%cx", "%dx", "%si", "%di" },
	{ "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi" }
};

static const char *szsconv[] = { "sbl", "swl", "l" };
//static const char *szuconv[] = { "zbl", "zwl", "l" };
static const char *szmov[] = { "b", "w", "l" };
static const char *szfull[] = { "l", "l", "l" };

static const char *xreg (enum X86REG _reg, int size)
{
	return reg_turd[size][_reg];
}

static void init_regalloc ()
{
	int i;
	for (i=0; i<X86REG_MAX; i++) {
		i386_regalloc[i].m68k_reg = -1;
		i386_regalloc[i].locked = 0;
	}
}

static void xflush_reg (int x86_reg)
{
	int m68k_reg = i386_regalloc[x86_reg].m68k_reg;

	if (i386_regalloc[x86_reg].locked != 0) {
		bug_error ("Internal compiler error. Attept to flush locked register.");
	}
	if (m68k_reg == -1) return;
	assert (m68k_reg <= REG_TEMP);

	/* temps can simply be discarded. */
	if (m68k_reg == REG_TEMP) {
		i386_regalloc[x86_reg].m68k_reg = -1;
		return;
	}
	/* while real m68k reg values must be flushed to memory */
	if (i386_regalloc[x86_reg].dirty)
		xln ("movl	%s,%d(%%ebp)", xreg (x86_reg, LONG), regpos (m68k_reg));
	i386_regalloc[x86_reg].m68k_reg = -1;
	i386_regalloc[x86_reg].dirty = 0;
}

static void xflush_all ()
{
	int i;
	for (i=0; i<X86REG_MAX; i++) {
		if (i386_regalloc[i].m68k_reg != -1) 
			xflush_reg (i);
	}
}

/* return -1 if not cached */
static int xfind_reg (int m68k_reg)
{
	int i;
	for (i=0; i<X86REG_MAX; i++) {
		if (i386_regalloc[i].m68k_reg == m68k_reg) {
			i386_regalloc[i].age = 0;
			return i;
		}
	}
	return -1;
}

static void xregalloc_age ()
{
	int i;
	for (i=0; i<X86REG_MAX; i++) {
		i386_regalloc[i].age++;
	}
}

/* set x86reg_preferred = whatever X86REG you MUST HAVE.
 * need_bswap=1 means that */
static enum X86REG xalloc_reg (int size, int x86reg_preferred)
{
	int i, top, oldest, age;
	if (x86reg_preferred != -1) {
		xflush_reg (x86reg_preferred);
		i386_regalloc[x86reg_preferred].m68k_reg = REG_TEMP;
		i386_regalloc[x86reg_preferred].dirty = 0;
		return x86reg_preferred;
	}
	/* look for unused reg */
	/* if we want BYTE size then regs esi,edi are no use */
	oldest = -1;
	age = -1;
	top = ((size == BYTE) ? ESI : X86REG_MAX);
	for (i=0; i<top; i++) {
		if (i386_regalloc[i].locked) continue;
		/* unallocated and unlocked temps can always be discarded */
		if ((i386_regalloc[i].m68k_reg == -1) ||
		    (i386_regalloc[i].m68k_reg == REG_TEMP)) {
			i386_regalloc[i].m68k_reg = REG_TEMP;
			i386_regalloc[i].dirty = 0;
			i386_regalloc[i].locked = 1;
			return i;
		}
		if (oldest == -1) {
			oldest = i;
			age = i386_regalloc[i].age;
		} else if (i386_regalloc[i].age > age) {
			oldest = i;
			age = i386_regalloc[i].age;
		}	
	}
	/* oh well.. we must evict a cached reg */
	if (oldest == -1) {
		error ("Internal compiler error: Run out of x86 registers (leaked temporaries?)");
	}
	xflush_reg (oldest);
	i386_regalloc[oldest].m68k_reg = REG_TEMP;
	i386_regalloc[oldest].dirty = 0;
	i386_regalloc[oldest].locked = 1;
	return oldest;
}

#define XLOADREG_FOR_OVERWRITE	(1<<0)

static enum X86REG xmove_reg (int size, enum X86REG src, enum X86REG dest)
{
	assert (!i386_regalloc[src].locked);
	i386_regalloc[src].locked = 1;
	dest = xalloc_reg (size, dest);
	xln ("movl	%s,%s", xreg (src,LONG), xreg (dest,LONG));
	i386_regalloc[dest] = i386_regalloc[src];
	i386_regalloc[dest].age = 0;
	i386_regalloc[src].locked = 0;
	i386_regalloc[src].dirty = 0;
	i386_regalloc[src].m68k_reg = -1;
	return dest;
}

static enum X86REG xload_reg2 (int m68k_reg, int size, int required_x86_reg, int flags, const char **conv)
{
	int i;
	//static int crap;
	/* is it already loaded? */
	for (i=0; i<X86REG_MAX; i++) {
		if (i386_regalloc[i].m68k_reg != m68k_reg) {
			continue;
		} else {
			if ((required_x86_reg != -1) &&
			    (i != required_x86_reg)) {
				/* reg loaded but not in desired place. do it */
				return xmove_reg (size, i, required_x86_reg);
			}
		}

		if ((size == BYTE) && (i >= ESI)) {
			/* can't operate on byte values in the esi/edi regs */
			//printf ("stupid esi/edi shit (no worries mate (n %d, line %d)\n", crap++, line_no);
			
			xflush_reg (i);
			//return xmove_reg (size, i);
		} else {
			i386_regalloc[i].age = 0;
			i386_regalloc[i].locked++;
			return i;
		}
	}
	/* otherwise allocate.. */
	i = xalloc_reg (size, required_x86_reg);

	/* registers loaded for overwrite size long do not need the original value */
	if (! ((flags & XLOADREG_FOR_OVERWRITE) && (size == LONG)))
		xln ("mov%s	%d(%%ebp),%s", conv[size], regpos (m68k_reg), xreg (i, LONG));

	i386_regalloc[i].m68k_reg = m68k_reg;
	i386_regalloc[i].age = 0;
	i386_regalloc[i].dirty = 0;
	i386_regalloc[i].locked = 1;
	return i;
}
static enum X86REG xload_reg (int m68k_reg, int size, int required_x86_reg)
{
	return xload_reg2 (m68k_reg, size, required_x86_reg, 0, szfull);
}

enum RegConversion { XCONV_NONE, XCONV_SWL, XCONV_ZWL };
/* returns a temporary reg alloc copy of an m68k register, that you can do
 * what the fuck you like with */
#define xcopy_reg(m68k_reg,size,required_x86_reg) \
	xcopy_reg2 (m68k_reg, size, required_x86_reg, 0)
static enum X86REG xcopy_reg2 (int m68k_reg, int size, int required_x86_reg, enum RegConversion conversion)
{
	int reg, copy_reg;

	/* i wonder if it is an optimisation to load directly to copy reg
	 * when the reg is not already cached.. */
	reg = xload_reg (m68k_reg, size, -1);
	copy_reg = xalloc_reg (size, required_x86_reg);
	
	switch (conversion) {
		case XCONV_NONE:
			xln ("mov%s	%s,%s", szmov[size], xreg (reg, size), xreg (copy_reg, size));
			break;
		case XCONV_SWL:
			assert (size == LONG);
			xln ("movswl	%s,%s", szmov[size], xreg (reg, WORD), xreg (copy_reg, LONG));
			break;
		case XCONV_ZWL:
			assert (size == LONG);
			xln ("movzwl	%s,%s", szmov[size], xreg (reg, WORD), xreg (copy_reg, LONG));
			break;
	}
	xreg_unlock (reg);
	return copy_reg;
}

static int rset[2] = { EAX, EBX };
static int rdirty[2] = { ECX, EDX };

#define sreg(reg)	xreg ((reg), size)

static void x_postea (ea_t *ea)
{
	int inc, reg;
	
	inc = 1<<ea->op_size;
	if ((ea->reg == 15) && (inc == 1)) inc = 2;
	/* deallocate temporary register allocations */
	if (ea->x86_reg != -1) {
		xreg_unlock (ea->x86_reg);
	}
	if (ea->x86_reg_writeback != -1) {
		xreg_unlock (ea->x86_reg_writeback);
	}
	
	switch (ea->mode) {
		/* areg postinc */
		case 3:
			reg = xload_reg (ea->reg, LONG, -1);
			xdirty_reg (reg);
			if (inc == 1)
				xln ("incl	%s", xreg (reg, LONG));
			else
				xln ("addl	$%d,%s", inc, xreg (reg, LONG));
			xreg_unlock (reg);
			break;
		/* areg predec */
		case 4:
			reg = xload_reg (ea->reg, LONG, -1);
			xdirty_reg (reg);
			if (inc == 1)
				xln ("decl	%s", xreg (reg, LONG));
			else
				xln ("subl	$%d,%s", inc, xreg (reg, LONG));
			xreg_unlock (reg);
			break;
		default: break;
	}
}

#define	_S(x)	if (stage[arg] == (x))

/*
 * Note on these multi-stage pipeline turds. The value must be actually
 * set in val_reg on the final stage, final instruction, so that the
 * condition codes are available.
 *
 * Actually used stages should be the final ones, if not all are needed.
 */

/*
 * Returns zero when done.
 */
static void xold_ea_get_address (ea_t *ea, int arg, int advance_stage)
{
	static int stage[2] = {1,1};
	int rpos, inc;

	if (!advance_stage) {
		if (stage[arg] != 1)	error ("xold_ea_get_address stages missed. (as68k bug).");

		stage[arg] = 0;
	} else {
		stage[arg]++;
	}
	
	ea->x86_reg = -1;
	
	switch (ea->mode) {
		/* dreg, areg */
		case 0: case 1: assert (0);
		/* areg indirect, postinc */
		case 2: case 3:
			_S(1) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rset[arg],LONG));
			break;
		/* areg predec */
		case 4: 
			inc = 1<<ea->op_size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rset[arg],LONG));
			//XXX
			_S(1) {
				if (inc == 1) xln ("dec	%s", xreg (rset[arg],LONG));
				else xln ("subl	$%d,%s", inc, xreg (rset[arg],LONG));
			}
			break;
		/* areg offset */
		case 5:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rset[arg],LONG));
			//XXX
			_S(1) xln ("addl	$%d,%s", ea->imm.val, xreg (rset[arg],LONG));
			break;
		/* areg offset + reg */
		case 6:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rset[arg],LONG));
			if (ea->ext._.size) {
				_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[arg], LONG));
			} else {
				_S(0) xln ("movswl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[arg], LONG));
			}
			_S(0) xln ("addl	$%d,%s", ea->ext._.displacement, xreg (rset[arg],LONG));
			//XXX
			_S(1) xln ("addl	%s,%s", xreg (rdirty[arg], LONG), xreg (rset[arg],LONG));
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				_S(1) xln ("movl	$%d,%s", ea->imm.val, xreg (rset[arg],LONG));
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					_S(1) xln ("movl	$__D%s,%s", ea->imm.label, xreg (rset[arg],LONG));
				} else {
					_S(1) xln ("movl	$%d,%s", ea->imm.val, xreg (rset[arg],LONG));
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				assert (0);
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				if (ea->imm.has_label) {
					_S(1) xln ("movl	$__D%s,%s", ea->imm.label, xreg (rset[arg],LONG));
				} else
					error ("Absolute value not allowed.");
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				if (!ea->imm.has_label) error ("Absolute value not allowed.");
				rpos = regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0));
				if (ea->ext._.size) {
					_S(0) xln ("movl	%d(%%ebp),%s", rpos, xreg (rset[arg],LONG));
				} else {
					_S(0) xln ("movswl	%d(%%ebp),%s", rpos, xreg (rset[arg],LONG));
				}
				// XXX
				_S(1) xln ("addl	$__D%s,%s", ea->imm.label, xreg (rset[arg],LONG));
			}
			break;
		default:
			assert (0);
	}
}

static void do_bswap (enum X86REG val, int size)
{
	switch (size) {
		case BYTE:	return;
		case WORD:	xln ("rorw	$8,%s", xreg (val, WORD));
				return;
		case LONG:	xln ("bswap	%s", xreg (val, LONG));
				return;
	}
}

/*
 * To avoid pipeline stalls the function outputs code until
 * a pipeline stall occurs, and then returns.
 * First call should be made with advance_stage == 0, subsequent
 * calls with advance_stage==1.
 * And then put shit inbetween.
 *
 * flag XQUICK_REGS means that for "reading" immediate values,
 * and XQUICK_REGS values in registers, nothing is done and instead ea->identifier
 * string is set to (for example) "$1234" or the i386 EA of the reg.
 *
 * XCAN_OVERWRITE means that you can modify the returned value because
 * it is a copy of the cached register (if it is a register, otherwise
 * this flag has no effect).
 */
#define XQUICK_REGS	(1<<0)
#define XQUICK_IMMS	(1<<1)
#define XQUICK_WRITEBACK	(1<<2)
#define XCAN_OVERWRITE	(1<<3)
#define XSIGN_EXTEND_REGS	(1<<4)

#define xold_readea(ea,arg,adv)	xold_readea2(ea,arg,adv,0);
static void xold_readea2 (ea_t *ea, int arg, int advance_stage, int flags)
{
	static int stage[2] = {2,2};
	int inc, size;
	size = ea->op_size;

	if (!advance_stage) {
		if (stage[arg] != 2)	error ("xold_readea stages missed. (as68k bug).");

		stage[arg] = 0;
	} else {
		stage[arg]++;
	}

	ea->do_writeback = 1;
	ea->x86_reg_writeback = -1;
	ea->x86_reg = -1;
	
	switch (ea->mode) {
		/* dreg, areg */
		case 0:
		case 1:
			if (flags & XQUICK_REGS) {
				/* operate in-place (in memory) */
				_S(0) snprintf (ea->identifier, sizeof (ea->identifier), "%d(%%ebp)", regpos (ea->reg));
				ea->do_writeback = 0;
				return;
			}
			_S(2) xln ("mov%s	%d(%%ebp),%s", szmov[size], regpos (ea->reg), xreg (rset[arg], size));
			break;
		/* areg indirect */
		case 2:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[arg], LONG));
			_S(1) xln ("mov%s	(%%ebp,%s),%s", szmov[size], xreg (rdirty[arg], LONG), xreg (rset[arg], size));
			_S(2) do_bswap (rset[arg], size);
			break;
		/* areg postinc */
		case 3:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[arg], LONG));
			_S(1) xln ("mov%s	(%%ebp,%s),%s", szmov[size], xreg (rdirty[arg], LONG), xreg (rset[arg], size));
			_S(2) do_bswap (rset[arg], size);
			break;
		/* areg predec */
		case 4:
			inc = 1<<size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[arg], LONG));
			_S(1) xln ("mov%s	-%d(%%ebp,%s),%s", szmov[size], inc, xreg (rdirty[arg], LONG), xreg (rset[arg], size));
			_S(2) do_bswap (rset[arg], size);
			break;
		/* areg offset */
		case 5:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[arg], LONG));
			_S(1) xln ("mov%s	%d(%%ebp,%s),%s", szmov[size], ea->imm.val, xreg (rdirty[arg], LONG), xreg (rset[arg], size));
			_S(2) do_bswap (rset[arg], size);
			break;
		/* areg offset + reg */
		case 6:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[arg],LONG));
			if (ea->ext._.size) {
				_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rset[arg],LONG));
			} else {
				_S(0) xln ("movswl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rset[arg], LONG));
			}
			_S(0) xln ("addl	%%ebp,%s", xreg (rdirty[arg],LONG));
			_S(1) xln ("mov%s	%d(%s,%s),%s", szmov[size], ea->ext._.displacement, xreg (rdirty[arg],LONG), xreg (rset[arg],LONG), xreg (rset[arg], size));
			_S(2) do_bswap (rset[arg], size);
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				_S(1) xln ("mov%s	%d(%%ebp),%s", szmov[size], ea->imm.val, xreg (rset[arg], size));
				_S(1) do_bswap (rset[arg], size);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					_S(1) xln ("mov%s	__D%s(%%ebp),%s", szmov[size], ea->imm.label, xreg (rset[arg], size));
					_S(2) do_bswap (rset[arg], size);
				} else {
					_S(1) xln ("mov%s	%d(%%ebp),%s", szmov[size], ea->imm.val, xreg (rset[arg], size));
					_S(2) do_bswap (rset[arg], size);
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				if (flags & XQUICK_IMMS) {
					if (ea->imm.has_label) {
						_S(0) snprintf (ea->identifier, sizeof (ea->identifier),
							"$__D%s", ea->imm.label);
					} else {
						_S(0) snprintf (ea->identifier, sizeof (ea->identifier),
							"$%d", ea->imm.val);
					}
					return;
				}
				if (ea->imm.has_label) {
					_S(2) xln ("movl	$__D%s,%s", ea->imm.label, xreg (rset [arg], LONG));
				} else {
					_S(2) xln ("movl	$%d,%s", ea->imm.val, xreg (rset [arg], LONG));
				}
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				assert (ea->imm.has_label);

				_S(1) xln ("mov%s	__D%s(%%ebp),%s", szmov[size], ea->imm.label, xreg (rset[arg], size));
				_S(2) do_bswap (rset[arg], size);
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				assert (ea->imm.has_label);
				if (ea->ext._.size) {
					_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[arg],LONG));
				} else {
					_S(0) xln ("movswl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[arg], LONG));
				}
				_S(1) xln ("mov%s	__D%s(%%ebp,%s),%s", szmov[size], ea->imm.label, xreg (rdirty[arg],LONG), xreg (rset[arg], size));
				_S(2) do_bswap (rset[arg], size);
			}
			break;
		default:
			error ("nasty error in c_readea()");
	}
	_S(0) snprintf (ea->identifier, sizeof (ea->identifier),
			"%s", reg_turd[size][arg]);
}

/*
 * Uses dirty regs of arg1 and arg2 (so they can't be pipelined together).
 */
static void xold_writeea (ea_t *ea, int size, enum X86REG val, int advance_stage)
{
	int inc;
	const int arg = 1; /* hack. _S macro ;-) */
	static int stage[2] = {2,2};

	assert ((val == EAX) || (val == EBX));
	
	if (!advance_stage) {
		if (stage[arg] != 2)	error ("xold_writeea stages missed. (as68k bug).");

		stage[arg] = 0;
	} else {
		stage[arg]++;
	}
	
	if (!ea->do_writeback) return;
	ea->x86_reg_writeback = -1;
	ea->x86_reg = -1;

	switch (ea->mode) {
		/* dreg, areg */
		case 0: case 1:
			_S(2) xln ("mov%s	%s,%d(%%ebp)", szmov[size], xreg (val, size), regpos (ea->reg));
			break;
		/* areg indirect */
		case 2:
			_S(1) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[0], LONG));
			_S(1) do_bswap (val, size);
			_S(2) xln ("mov%s	%s,(%%ebp,%s)", szmov[size], xreg (val, size), xreg (rdirty[0], LONG));
			break;
		/* areg postinc */
		case 3:
			_S(1) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[0], LONG));
			_S(1) do_bswap (val, size);
			_S(2) xln ("mov%s	%s,(%%ebp,%s)", szmov[size], xreg (val, size), xreg (rdirty[0], LONG));
			break;
		/* areg predec */
		case 4:
			inc = 1<<size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			_S(1) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[0], LONG));
			_S(1) do_bswap (val, size);
			_S(2) xln ("mov%s	%s,-%d(%%ebp,%s)", szmov[size], xreg (val, size), inc, xreg (rdirty[0], LONG));
			break;
		/* areg offset */
		case 5:
			_S(1) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[0], LONG));
			_S(1) do_bswap (val, size);
			_S(2) xln ("mov%s	%s,%d(%%ebp,%s)", szmov[size], xreg (val, size), ea->imm.val, xreg (rdirty[0], LONG));
			break;
		/* areg offset + reg */
		case 6:
			_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->reg), xreg (rdirty[1],LONG));
			if (ea->ext._.size) {
				_S(0) xln ("movl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[0],LONG));
			} else {
				_S(0) xln ("movswl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[0], LONG));
			}
			_S(0) xln ("addl	%%ebp,%s", xreg (rdirty[1],LONG));
			_S(0) do_bswap (val, size);
			_S(1) xln ("mov%s	%s,%d(%s,%s)", szmov[size], xreg (val, size), ea->ext._.displacement, xreg (rdirty[1],LONG), xreg (rdirty[0],LONG));
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				_S(1) do_bswap (val, size);
				_S(2) xln ("mov%s	%s,%d(%%ebp)", szmov[size], xreg (val, size), ea->imm.val);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					_S(1) do_bswap (val, size);
					_S(2) xln ("mov%s	%s,__D%s(%%ebp)", szmov[size], xreg (val, size), ea->imm.label);
				} else {
					_S(1) do_bswap (val, size);
					_S(2) xln ("mov%s	%s,%d(%%ebp)", szmov[size], xreg (val, size), ea->imm.val);
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				assert (0);
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				assert (ea->imm.has_label);

				_S(1) do_bswap (val, size);
				_S(2) xln ("mov%s	%s,__D%s(%%ebp)", szmov[size], xreg (val, size), ea->imm.label);
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				assert (ea->imm.has_label);
				if (ea->ext._.size) {
					_S(1) xln ("movl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[0],LONG));
				} else {
					_S(1) xln ("movswl	%d(%%ebp),%s", regpos (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0)), xreg (rdirty[0], LONG));
				}
				_S(1) do_bswap (val, size);
				_S(2) xln ("mov%s	%s,__D%s(%%ebp,%s)", szmov[size], xreg (val,size), ea->imm.label, xreg (rdirty[0],LONG));
			}
			else {
				error ("wtf in c_writeea ()");
			}
			break;
		default:
			error ("nasty error in c_writeea()");
	}
}

/* loads effective address, not value pointed to */
static void x_loadea (ea_t *ea)
{
	int reg2, reg3, inc, out_reg;

	ea->do_writeback = 1;
	ea->x86_reg_writeback = -1;
	
	switch (ea->mode) {
		/* dreg, areg */
		case 0: case 1: assert (0);
		/* areg indirect, postinc */
		case 2: case 3:
			out_reg = xcopy_reg (ea->reg, LONG, -1);
			break;
		/* areg predec */
		case 4: 
			inc = 1<<ea->op_size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			out_reg = xcopy_reg (ea->reg, LONG, -1);
			if (inc == 1) xln ("dec	%s", xreg (out_reg,LONG));
			else xln ("subl	$%d,%s", inc, xreg (out_reg,LONG));
			break;
		/* areg offset */
		case 5:
			out_reg = xcopy_reg (ea->reg, LONG, -1);
			xln ("addl	$%d,%s", ea->imm.val, xreg (out_reg,LONG));
			break;
		/* areg offset + reg */
		case 6:
			out_reg = xcopy_reg (ea->reg, LONG, -1);
			if (ea->ext._.size) {
				reg2 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
			} else {
				/* need fucking 16-bit chunk of variable sign extended to 32... */
				reg3 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
				reg2 = xalloc_reg (LONG, -1);
				xln ("movswl	%s,%s", xreg (reg3, WORD), xreg (reg2, LONG));
				xreg_unlock (reg3);

			}
			xln ("leal	%d(%s,%s),%s", ea->ext._.displacement, xreg (reg2, LONG), xreg(out_reg, LONG), xreg (out_reg, LONG));
			xreg_unlock (reg2);
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				out_reg = xalloc_reg (LONG, -1);
				xln ("movl	$%d,%s", ea->imm.val, out_reg, LONG);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				out_reg = xalloc_reg (LONG, -1);
				if (ea->imm.has_label) {
					xln ("movl	$__D%s,%s", ea->imm.label, xreg (out_reg,LONG));
				} else {
					xln ("movl	$%d,%s", ea->imm.val, xreg (out_reg,LONG));
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				assert (0);
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				if (ea->imm.has_label) {
					out_reg = xalloc_reg (LONG, -1);
					xln ("movl	$__D%s,%s", ea->imm.label, xreg (out_reg,LONG));
				} else
					error ("Absolute value not allowed.");
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				if (!ea->imm.has_label) error ("Absolute value not allowed.");
				out_reg = xalloc_reg (LONG, -1);
				if (ea->ext._.size) {
					reg2 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
				} else {
					/* need fucking 16-bit chunk of variable sign extended to 32... */
					reg3 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
					reg2 = xalloc_reg (LONG, -1);
					xln ("movswl	%s,%s", xreg (reg3, WORD), xreg (reg2, LONG));
					xreg_unlock (reg3);
				}
				// XXX
				xln ("leal	__D%s(%s),%s", ea->imm.label, xreg (reg2, LONG), xreg (out_reg, LONG));
				xreg_unlock (reg2);
			}
			break;
		default:
			assert (0);
	}
	ea->x86_reg = out_reg;
	snprintf (ea->identifier, sizeof (ea->identifier),
			"%s", xreg (out_reg, LONG));
}

/* that is, preferred x86 reg.
 * XXX nothing preferred about it. it is a fucking demand. */
static void x_loadval (ea_t *ea, int preferred_reg, int flags)
{
	int inc, size, reg1, reg2, reg3, out_reg;
	size = ea->op_size;

	ea->do_writeback = 1;
	ea->x86_reg_writeback = -1;
	
	switch (ea->mode) {
		/* dreg, areg */
		case 0:
		case 1:
			if (flags & XCAN_OVERWRITE) {
				out_reg = xcopy_reg (ea->reg, LONG, preferred_reg);
			} else
			if ((flags & XQUICK_REGS) && (xfind_reg (ea->reg)==-1)) {
				/* operate in-place (in memory) */
				snprintf (ea->identifier, sizeof (ea->identifier), "%d(%%ebp)", regpos (ea->reg));
				ea->do_writeback = 0;
				return;
			} else {
				out_reg = xload_reg2 (ea->reg, size, preferred_reg,
						0, (flags & XSIGN_EXTEND_REGS ? szsconv : szfull));
			}
			break;
		/* areg indirect */
		case 2:
			reg1 = xload_reg (ea->reg, LONG, -1);
			out_reg = xalloc_reg (size, preferred_reg);
			
			xln ("mov%s	(%%ebp,%s),%s", szmov[size], xreg (reg1, LONG), xreg (out_reg, size));
			xreg_unlock (reg1);
			do_bswap (out_reg, size);
			break;
		/* areg postinc */
		case 3:
			reg1 = xload_reg (ea->reg, LONG, -1);
			out_reg = xalloc_reg (size, preferred_reg);
			
			xln ("mov%s	(%%ebp,%s),%s", szmov[size], xreg (reg1, LONG), xreg (out_reg, size));
			xreg_unlock (reg1);
			do_bswap (out_reg, size);
			break;
		/* areg predec */
		case 4:
			inc = 1<<size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			reg1 = xload_reg (ea->reg, LONG, -1);
			out_reg = xalloc_reg (size, preferred_reg);
			xln ("mov%s	-%d(%%ebp,%s),%s", szmov[size], inc, xreg (reg1, LONG), xreg (out_reg, size));
			xreg_unlock (reg1);
			do_bswap (out_reg, size);
			break;
		/* areg offset */
		case 5:
			reg1 = xload_reg (ea->reg, LONG, -1);
			out_reg = xalloc_reg (size, preferred_reg);
			
			xln ("mov%s	%d(%%ebp,%s),%s", szmov[size], ea->imm.val, xreg (reg1, LONG), xreg (out_reg, size));
			xreg_unlock (reg1);
			do_bswap (out_reg, size);
			break;
		/* areg offset + reg */
		case 6:
			reg1 = xload_reg (ea->reg, LONG, -1);
			out_reg = xalloc_reg (size, preferred_reg);
			if (ea->ext._.size) {
				reg2 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
			} else {
				/* need fucking 16-bit chunk of variable sign extended to 32... */
				reg3 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
				reg2 = xalloc_reg (LONG, -1);
				xln ("movswl	%s,%s", xreg (reg3, WORD), xreg (reg2, LONG));
			}
			if (flags & XQUICK_WRITEBACK) {
				xln ("lea	(MemBase+%d)(%s,%s),%s", ea->ext._.displacement, xreg (reg1,LONG), xreg (reg2,LONG), xreg (reg2, LONG));
				xln ("mov%s	(%s),%s", szmov[size], xreg (reg2,LONG), xreg (out_reg, size));
				ea->x86_reg_writeback = reg2;
				
				xreg_unlock (reg1);
				xreg_unlock (reg3);
			} else {
				xln ("mov%s	(MemBase+%d)(%s,%s),%s", szmov[size], ea->ext._.displacement, xreg (reg1,LONG), xreg (reg2,LONG), xreg (out_reg, size));
				if (!ea->ext._.size) xreg_unlock (reg3);
				xreg_unlock (reg1);
				xreg_unlock (reg2);
			}
			do_bswap (out_reg, size);
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				out_reg = xalloc_reg (size, preferred_reg);
				xln ("mov%s	%d(%%ebp),%s", szmov[size], ea->imm.val, xreg (out_reg, size));
				do_bswap (out_reg, size);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				out_reg = xalloc_reg (size, preferred_reg);
				if (ea->imm.has_label) {
					xln ("mov%s	__D%s(%%ebp),%s", szmov[size], ea->imm.label, xreg (out_reg, size));
				} else {
					xln ("mov%s	%d(%%ebp),%s", szmov[size], ea->imm.val, xreg (out_reg, size));
				}
				do_bswap (out_reg, size);
			}
			/* immediate */
			else if (ea->reg == 4) {
				if (flags & XQUICK_IMMS) {
					if (ea->imm.has_label) {
						snprintf (ea->identifier, sizeof (ea->identifier),
							"$__D%s", ea->imm.label);
					} else {
						snprintf (ea->identifier, sizeof (ea->identifier),
							"$%d", ea->imm.val);
					}
					ea->x86_reg = -1;
					return;
				}
				out_reg = xalloc_reg (size, preferred_reg);
				if (ea->imm.has_label) {
					xln ("movl	$__D%s,%s", ea->imm.label, xreg (out_reg, LONG));
				} else {
					xln ("movl	$%d,%s", ea->imm.val, xreg (out_reg, LONG));
				}
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				assert (ea->imm.has_label);

				out_reg = xalloc_reg (size, preferred_reg);
				xln ("mov%s	__D%s(%%ebp),%s", szmov[size], ea->imm.label, xreg (out_reg, size));
				do_bswap (out_reg, size);
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				assert (ea->imm.has_label);
				
				out_reg = xalloc_reg (size, preferred_reg);
				if (ea->ext._.size) {
					reg2 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
				} else {
					/* need fucking 16-bit chunk of variable sign extended to 32... */
					reg3 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
					reg2 = xalloc_reg (LONG, -1);
					xln ("movswl	%s,%s", xreg (reg3, WORD), xreg (reg2, LONG));
					xreg_unlock (reg3);
				}
				xln ("mov%s	__D%s(%%ebp,%s),%s", szmov[size], ea->imm.label, xreg (reg2,LONG), xreg (out_reg, size));
				do_bswap (out_reg, size);
				xreg_unlock (reg2);
			}
			break;
		default:
			error ("nasty error in c_readea()");
	}
			
	ea->x86_reg = out_reg;
	snprintf (ea->identifier, sizeof (ea->identifier),
			"%s", xreg (out_reg, size));
}

static void x_saveval (ea_t *ea, int size)
{
	int inc, reg1, reg2, reg3;

	if (!ea->do_writeback) return;

	if (ea->x86_reg_writeback != -1) {
		do_bswap (ea->x86_reg, size);
		xln ("mov%s	%s,(%s)", szmov[size], xreg (ea->x86_reg, size), xreg (ea->x86_reg_writeback, LONG));
		return;
	}
	
	switch (ea->mode) {
		/* dreg, areg */
		case 0: case 1:
			/* don't need to act until xflush_reg... */
			break;
		/* areg indirect */
		case 2:
			reg1 = xload_reg (ea->reg, LONG, -1);
			do_bswap (ea->x86_reg, size);
			xln ("mov%s	%s,(%%ebp,%s)", szmov[size], xreg (ea->x86_reg, size), xreg (reg1, LONG));
			xreg_unlock (reg1);
			break;
		/* areg postinc */
		case 3:
			reg1 = xload_reg (ea->reg, LONG, -1);
			do_bswap (ea->x86_reg, size);
			xln ("mov%s	%s,(%%ebp,%s)", szmov[size], xreg (ea->x86_reg, size), xreg (reg1, LONG));
			xreg_unlock (reg1);
			break;
		/* areg predec */
		case 4:
			inc = 1<<size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			reg1 = xload_reg (ea->reg, LONG, -1);
			do_bswap (ea->x86_reg, size);
			xln ("mov%s	%s,-%d(%%ebp,%s)", szmov[size], xreg (ea->x86_reg, size), inc, xreg (reg1, LONG));
			xreg_unlock (reg1);
			break;
		/* areg offset */
		case 5:
			reg1 = xload_reg (ea->reg, LONG, -1);
			do_bswap (ea->x86_reg, size);
			xln ("mov%s	%s,%d(%%ebp,%s)", szmov[size], xreg (ea->x86_reg, size), ea->imm.val, xreg (reg1, LONG));
			xreg_unlock (reg1);
			break;
		/* areg offset + reg */
		case 6:
			reg1 = xload_reg (ea->reg, LONG, -1);
			if (ea->ext._.size) {
				reg2 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
			} else {
				/* need fucking 16-bit chunk of variable sign extended to 32... */
				reg3 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
				reg2 = xalloc_reg (LONG, -1);
				xln ("movswl	%s,%s", xreg (reg3, WORD), xreg (reg2, LONG));
				xreg_unlock (reg3);
			}
			do_bswap (ea->x86_reg, size);
			xln ("mov%s	%s,(MemBase+%d)(%s,%s)", szmov[size], xreg (ea->x86_reg, size), ea->ext._.displacement, xreg (reg1,LONG), xreg (reg2,LONG));
			xreg_unlock (reg1);
			xreg_unlock (reg2);
			break;
		/* yes */
		case 7:
			/* XXX TO DO */
			/* $xxx.w */
			if (ea->reg == 0) {
				do_bswap (ea->x86_reg, size);
				xln ("mov%s	%s,%d(%%ebp)", szmov[size], xreg (ea->x86_reg, size), ea->imm.val);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					do_bswap (ea->x86_reg, size);
					xln ("mov%s	%s,__D%s(%%ebp)", szmov[size], xreg (ea->x86_reg, size), ea->imm.label);
				} else {
					do_bswap (ea->x86_reg, size);
					xln ("mov%s	%s,%d(%%ebp)", szmov[size], xreg (ea->x86_reg, size), ea->imm.val);
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				assert (0);
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				assert (ea->imm.has_label);

				do_bswap (ea->x86_reg, size);
				xln ("mov%s	%s,__D%s(%%ebp)", szmov[size], xreg (ea->x86_reg, size), ea->imm.label);
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				assert (ea->imm.has_label);
				if (ea->ext._.size) {
					reg2 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
				} else {
					/* need fucking 16-bit chunk of variable sign extended to 32... */
					reg3 = xload_reg (ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), LONG, -1);
					reg2 = xalloc_reg (LONG, -1);
					xln ("movswl	%s,%s", xreg (reg3, WORD), xreg (reg2, LONG));
					xreg_unlock (reg3);
				}
				do_bswap (ea->x86_reg, size);
				xln ("mov%s	%s,__D%s(%%ebp,%s)", szmov[size], xreg (ea->x86_reg,size), ea->imm.label, xreg (reg2,LONG));
				xreg_unlock (reg2);
			}
			else {
				error ("wtf in c_writeea ()");
			}
			break;
		default:
			error ("nasty error in c_writeea()");
	}
}

static void x_ea_set_val_2_reg (ea_t *ea, int size, int x86_reg)
{
	int i;
	assert (size == LONG);
	
	ea->do_writeback = 1;
	ea->x86_reg_writeback = -1;

	if ((ea->mode == 0) || (ea->mode == 1)) {
		i = xfind_reg (ea->reg);

		if (i != -1) {
			/* reg already loaded, but we are wiping
			 * the bastard with a full LONG write so drop
			 * the existing regalloc */
			assert (i386_regalloc[i].locked == 0);
			i386_regalloc[i].m68k_reg = -1;
			i386_regalloc[i].dirty = 0;
		}
		i386_regalloc[x86_reg].m68k_reg = ea->reg;
		i386_regalloc[x86_reg].dirty = 1;
		
		ea->x86_reg = x86_reg;
	} else {
		i386_regalloc[x86_reg].m68k_reg = REG_TEMP;
		ea->x86_reg = x86_reg;
	}
	i386_regalloc[x86_reg].locked++;
	i386_regalloc[x86_reg].age = 0;
}

static void x_cond_end ()
{
	xout ("2:\n");
}

static void x_cond_begin (int cond)
{
	switch (cond) {
		/* t */
		case 0: break;
		/* f */
		case 1: xln ("jmp	2f"); break;
		/* hi */
		case 2: 
			xln ("movb	C,%%al");
			xln ("orb	Z,%%al");
			xln ("jnz	2f");
			break;
			//cout ("((!C) && !Z)"); break;
		/* ls */
		case 3:
			xln ("movb	C,%%al");
			xln ("orb	Z,%%al");
			xln ("jz	2f");
			break;
			//cout ("(C || Z)"); break;
		/* cc */
		case 4: 
			xln ("testb	$1,C");
			xln ("jnz	2f");
			break;
			//cout ("(!C)"); break;
		/* cs */
		case 5: 
			xln ("testb	$1,C");
			xln ("jz	2f");
			break;
			//cout ("C"); break;
		/* ne */
		case 6: 
			xln ("testb	$1,Z");
			xln ("jnz	2f");
			break;
			//cout ("!Z"); break;
		/* eq */
		case 7: 
			xln ("testb	$1,Z");
			xln ("jz	2f");
			break;
			//cout ("(Z)"); break;
		/* vc */
		case 8: 
			xln ("testb	$1,V");
			xln ("jnz	2f");
			break;
			//cout ("(!V)"); break;
		/* vs */
		case 9: 
			xln ("testb	$1,V");
			xln ("jz	2f");
			break;
			//cout ("V"); break;
		/* pl */
		case 10: 
			xln ("testb	$1,N");
			xln ("jnz	2f");
			break;
			//cout ("(!N)"); break;
		/* mi */
		case 11: 
			xln ("testb	$1,N");
			xln ("jz	2f");
			break;
			//cout ("N"); break;
		/* ge */
		case 12:
			xln ("movb	N,%%al");
			xln ("xorb	V,%%al");
			xln ("jnz	2f");
			break;
			//cout ("((N && V) || ((!N) && (!V)))"); break;
		/* lt */
		case 13:
			xln ("movb	V,%%al");
			xln ("xorb	$1,%%al");
			xln ("xorb	N,%%al");
			xln ("jnz	2f");
			break;
			//cout ("((N && (!V)) || ((!N) && V))"); break;
		/* gt */
		case 14:
			xln ("movb	N,%%al");
			xln ("xorb	V,%%al");
			xln ("orb	Z,%%al");
			xln ("jnz	2f");
			break;
			//cout ("((N && V && (!Z)) || ((!N) && (!V) && (!Z)))"); break;
		/* le */
		case 15:
			xln ("movb	Z,%%ah");
			xln ("movb	V,%%al");
			xln ("xorb	$1,%%ah");
			xln ("xorb	$1,%%al");
			xln ("xorb	N,%%al");
			xln ("andb	%%ah,%%al");
			xln ("jnz	2f");
			break;
			//cout ("((Z) || (N && (!V)) || ((!N) && V))"); break;
	}
}

void i386_jump (ea_t *ea)
{
	xflush_all ();
	if (((ea->mode == 7) && (ea->reg == 1)) ||
	    ((ea->mode == 7) && (ea->reg == 2))) {
		xln ("jmp	__N%s", ea->imm.label);
	} else {
		xold_ea_get_address (ea, 0, 0);
		xold_ea_get_address (ea, 0, 1);
		x_postea (ea);
		//xln ("jmp	jumptable");
		xln ("jmp	*jtab(,%%eax,2)");
	}
}

/* Check for pending exceptions also */
void i386_jump_e (ea_t *ea)
{
	xflush_all ();
	xln ("cmpl	$0,exceptions_pending_mask");

	/* yay! exception */
	if (((ea->mode == 7) && (ea->reg == 1)) ||
	    ((ea->mode == 7) && (ea->reg == 2))) {
		xln ("movl	$__D%s,%%eax", ea->imm.label);
	} else {
		xold_ea_get_address (ea, 0, 0);
		xold_ea_get_address (ea, 0, 1);
		x_postea (ea);
	}
	xln ("jnz	handle_exception");
	i386_jump (ea);
}

static void i386_func_bitop (ea_t *src, ea_t *dest, int size, const char *op)
{
	int len = size2len [size];
	
	xflush_all ();
	
	if (is_immediate (src)) {
		xold_readea (dest, 1, 0);
		xold_readea (dest, 1, 1);
		xold_readea (dest, 1, 1);
		
		xln ("%sl	$%d,%%ebx", op, src->imm.val & (len-1));
		ifZ xln ("setnc	Z");

		xold_writeea (dest, size, EBX, 0);
		xold_writeea (dest, size, EBX, 1);
		xold_writeea (dest, size, EBX, 1);

		x_postea (dest);
		return;
	}
	
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	if (size == BYTE) xln ("andl	$7,%%eax");
	xln ("%sl	%%eax,%%ebx", op, src->imm.val);
	ifZ xln ("setnc	Z");
	
	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);

	x_postea (src);
	x_postea (dest);
}

void i386_func_bchg (ea_t *src, ea_t *dest, int size)
{
	i386_func_bitop (src, dest, size, "btc");
}

void i386_func_bclr (ea_t *src, ea_t *dest, int size)
{
	i386_func_bitop (src, dest, size, "btr");
}

void i386_func_bset (ea_t *src, ea_t *dest, int size)
{
	i386_func_bitop (src, dest, size, "bts");
}

void i386_func_btst (ea_t *src, ea_t *dest, int size)
{
	int len = size2len [size];
	
	xflush_all ();
	
	if (is_immediate (src)) {
		xold_readea (dest, 1, 0);
		xold_readea (dest, 1, 1);
		xold_readea (dest, 1, 1);
		
		xln ("btl	$%d,%%ebx", src->imm.val & (len-1));
		xln ("setnc	Z");

		x_postea (dest);
		return;
	}
	
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	if (size == BYTE) xln ("andl	$7,%%eax");
	xln ("btl	%%eax,%%ebx", src->imm.val);
	xln ("setnc	Z");

	x_postea (src);
	x_postea (dest);
}

void i386_func_muls (ea_t *src, ea_t *dest)
{
	assert (dest->mode == 0);
	src->op_size = WORD;
	x_loadval (src, -1, XCAN_OVERWRITE);
	dest->op_size = WORD;
	x_loadval (dest, -1, 0);//XSIGN_EXTEND_REGS);

	xln ("movswl	%s,%s", src->identifier, xreg (src->x86_reg, LONG));
	xln ("movswl	%s,%s", dest->identifier, xreg (dest->x86_reg, LONG));
		
		xln ("imull	%s,%s", xreg (src->x86_reg, LONG), xreg (dest->x86_reg, LONG));
	ifNVC 	xln ("movb	$0,C");
	ifNVC 	xln ("movb	$0,V");
	ifZNVC	xln ("testl	%s,%s", xreg (dest->x86_reg, LONG), xreg (dest->x86_reg, LONG));
	ifNVC 	xln ("sets	N");
	ifZ	xln ("setz	Z");

	xdirty_reg (dest->x86_reg);
	x_saveval (dest, WORD);
	x_postea (src);
	x_postea (dest);
}
void i386_func_mulu (ea_t *src, ea_t *dest)
{
	xflush_all ();
	xold_readea2 (src, 1, 0, XQUICK_REGS);
	xold_readea2 (dest, 0, 0, XQUICK_REGS);
	xold_readea2 (src, 1, 1, XQUICK_REGS);
	xold_readea2 (dest, 0, 1, XQUICK_REGS);
	xold_readea2 (src, 1, 1, XQUICK_REGS);
	xold_readea2 (dest, 0, 1, XQUICK_REGS);
	
	xln ("movzwl	%s,%%ebx", src->identifier);
	xln ("movzwl	%s,%%eax", dest->identifier);

		xln ("mull	%%ebx");
	ifNVC	xln ("movb	$0,C");
	ifNVC	xln ("movb	$0,V");
	ifZNVC	xln ("testl	%%eax,%%eax");
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");

	/* hack - force writeback, because we didn't operate in-place */
	dest->do_writeback = 1;
	
	xold_writeea (dest, LONG, EAX, 0);
	xold_writeea (dest, LONG, EAX, 1);
	xold_writeea (dest, LONG, EAX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_logop (ea_t *src, ea_t *dest, int size, char op)
{
	x_loadval (src, -1, XQUICK_REGS | XQUICK_IMMS);
	x_loadval (dest, -1, XQUICK_WRITEBACK);

	switch (op) {
		case '&': xln ("and%s	%s,%s", szmov [size], src->identifier, dest->identifier); break;
		case '|': xln ("or%s	%s,%s", szmov [size], src->identifier, dest->identifier); break;
		case '^': xln ("xor%s	%s,%s", szmov [size], src->identifier, dest->identifier); break;
		default: assert (0);
	}

	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");
	ifNVC	xln ("movb	$0,V");
	ifNVC	xln ("movb	$0,C");

	xdirty_reg (dest->x86_reg);
	x_saveval (dest, size);
	x_postea (src);
	x_postea (dest);
}

void i386_func_not (ea_t *ea, int size)
{
	int flags = XQUICK_WRITEBACK;
	/* mem to mem operations are crap */
	//ifZNVC {} else flags |= XQUICK_REGS;
	
	x_loadval (ea, -1, flags);

	xln ("not%s	%s", szmov [size], ea->identifier);

	ifZNVC	xln ("test%s	%s,%s", szmov [size], ea->identifier, ea->identifier);
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");
	ifNVC	xln ("movb	$0,V");
	ifNVC	xln ("movb	$0,C");
	
	xdirty_reg (ea->x86_reg);
	x_saveval (ea, size);
	x_postea (ea);
}

void i386_func_negx (ea_t *ea, int size)
{
	xflush_all ();
	xold_readea (ea, 0, 0);
	xold_readea (ea, 0, 1);
	xold_readea (ea, 0, 1);

	// XXX this is crap. do without jumps (negx)
	// then again, negx and subx caused so many problems
	// and took so long to get right that who cares..
	xln ("xorl	%%ebx,%%ebx");
	xln ("movb	X,%%cl");
	xln ("testb	%%cl,%%cl");
	xln ("jz	1f");
	xln ("stc");
	xln ("jmp	2f");
	
	xout ("1:\n");
	xln ("clc");
	
	xout ("2:\n");
	xln ("sbb%s	%s,%s", szmov [size], sreg (EAX), sreg (EBX));
	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
		xln ("setc	C");
	}
		xln ("setc	X");
	ifZ {
		xln ("test%s	%s,%s", szmov[size], sreg (EBX), sreg (EBX));
		xln ("jz	1f");
		xln ("movb	$0,Z");
		xout ("1:\n");
	}
	
	xold_writeea (ea, size, EBX, 0);
	xold_writeea (ea, size, EBX, 1);
	xold_writeea (ea, size, EBX, 1);
	x_postea (ea);
}
void i386_func_neg (ea_t *ea, int size)
{
	x_loadval (ea, -1, XQUICK_WRITEBACK);

	xln ("neg%s	%s", szmov [size], ea->identifier);
	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
		xln ("setc	C");
	}
	ifZ	xln ("setz	Z");
		xln ("setc	X");
	
	xdirty_reg (ea->x86_reg);
	x_saveval (ea, size);
	x_postea (ea);
}

void i386_func_divu (ea_t *ea, int reg)
{
	xflush_all ();
	ea->op_size = WORD;
	xold_readea (ea, 1, 0);
	xold_readea (ea, 1, 1);
	xold_readea (ea, 1, 1);

	xln ("movl	%d(%%ebp),%%eax", regpos (reg));
	xln ("movzwl	%%bx,%%ebx");
	xln ("xorl	%%edx,%%edx");
	xln ("divl	%%ebx");

	xln ("testl	$0xffff0000,%%eax");
	/* overflow */
	xln ("jnz	1f");

	ifZNVC	xln ("testl	%%eax,%%eax");
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");
	ifNVC	xln ("movb	$0,V");
	ifNVC	xln ("movb	$0,C");

	xln ("shll	$16,%%edx");
	xln ("movw	%%ax,%%dx");

	xln ("movl	%%edx,%d(%%ebp)", regpos (reg));
	xln ("jmp	2f");
	xout ("1:\n");
	ifZNVC {
		xln ("movb	$0,C");
		xln ("movb	$1,N");
		xln ("movb	$0,Z");
		xln ("movb	$1,V");
	}
	xout ("2:\n");
	x_postea (ea);
}

void i386_func_divs (ea_t *ea, int reg)
{
	xflush_all ();
	ea->op_size = WORD;
	xold_readea (ea, 1, 0);
	xold_readea (ea, 1, 1);
	xold_readea (ea, 1, 1);

	xln ("movl	%d(%%ebp),%%eax", regpos (reg));
	xln ("movswl	%%bx,%%ebx");
	/* sign-extend eax into edx */
	xln ("cltd");
	xln ("idivl	%%ebx");
	xln ("jmp	3f");
	/* success */
	xout ("1:\n");
	
	ifZNVC	xln ("testl	%%eax,%%eax");
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");

		xln ("shll	$16,%%edx");
	ifNVC	xln ("movb	$0,V");
		xln ("movw	%%ax,%%dx");
	ifNVC 	xln ("movb	$0,C");
		xln ("movl	%%edx,%d(%%ebp)", regpos (reg));
		xln ("jmp	2f");
	/* overflow */
	xout ("3:\n");
	xln ("movl	%%eax,%%ecx");
	xln ("andl	$0xffff8000,%%ecx");
	xln ("jz	1b");
	xln ("cmpl	$0xffff8000,%%ecx");
	xln ("jz	1b");
	ifNVC {
		xln ("movb	$0,C");
		xln ("movb	$1,N");
		xln ("movb	$1,V");
	} ifZ	xln ("movb	$0,Z");
	xout ("2:\n");
	x_postea (ea);
}

/* src is the shift */
/* these are such cunts */
void i386_func_asl (ea_t *src, ea_t *dest, int size)
{
	/* special case we can optimise happily. no flags except X || Z needed, and
	 * immediate src, reg dest. */
	ifNVC { } else if (is_immediate (src) && is_reg (dest)) {
		x_loadval (src, -1, XQUICK_IMMS);
		x_loadval (dest, -1, XQUICK_WRITEBACK);

		xln ("shl%s	%s,%s", szmov [size], src->identifier, dest->identifier);
		xln ("setc	X");
		ifZ	xln ("setz	Z");
		
		xdirty_reg (dest->x86_reg);
		x_saveval (dest, size);
		x_postea (src);
		x_postea (dest);
		return;
	}
	xflush_all ();
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	/* sets flags wrong. can use with flag optimisations one day */
	
	/* V flag not set by sal on x86 :( */
	
		xln ("and%s	$63,%s", szmov[size], sreg (EAX));
		xln ("cmp%s	$%d,%s", szmov[size], size2len[size], sreg (EAX));
		xln ("jl	2f");

		xln ("test%s	%s,%s", szmov[size], sreg (EBX), sreg (EBX));
		xln ("setnz	V");
		xln ("movb	$1,Z");
		xln ("movb	$0,N");
	/* XXX what should be done about C and X... */
		xln ("xor%s	%s,%s", szmov[size], sreg (EBX), sreg (EBX));
		xln ("jmp	4f");
		
		/* mask in edx */
		xout ("2:\n");
	ifNVC {
		xln ("movb	$%d,%%cl", size2len[size]-1);
		xln ("mov%s	$0x%s,%s", szmov[size],
				(size == BYTE ? "ff" :
				 (size == WORD ? "ffff" : "ffffffff")), sreg (EDX));
		xln ("subb	%%al,%%cl");
		xln ("movb	$0,V");
		xln ("shl%s	%%cl,%s", szmov[size], sreg (EDX));
		xln ("mov%s	%s,%s", szmov[size], sreg (EDX), sreg (ECX));

		/* V = ((val & mask) != mask &&) 
		 * ((val & mask != 0) */
		xln ("test%s	%s,%s", szmov[size], sreg (EBX), sreg (EDX));
		xln ("jz	3f");
		xln ("and%s	%s,%s", szmov[size], sreg (EBX), sreg (EDX));
		xln ("cmp%s	%s,%s", szmov[size], sreg (ECX), sreg (EDX));
		xln ("setnz	V");
		/* ^^ huge amount of code for one fucking flag... */
	
		xout ("3:\n");
	}
	xln ("movb	%%al,%%cl");
	xln ("shl%s	%%cl,%s", szmov [size], sreg (EBX));

	ifNVC {
		xln ("sets	N");
		xln ("setc	C");
	}
	xln ("setc	X");
	
	if (!is_immediate (src)) {
		/* shifts of zero bits set the Z flag on x86, apparently */
		/* (immediate shifts can't be zero) */
		ifZ	xln ("test%s	%s,%s", szmov[size], sreg (EBX), sreg (EBX));
	}
	ifZ	xln ("setz	Z");

	xout ("4:\n");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_lsl (ea_t *src, ea_t *dest, int size)
{
	x_loadval (src, ECX, XQUICK_IMMS);
	x_loadval (dest, -1, XQUICK_WRITEBACK);
	
	if (is_immediate (src)) {
		xln ("shl%s	%s,%s", szmov [size], src->identifier, dest->identifier);
	} else {
		xln ("shl%s	%%cl,%s", szmov [size], dest->identifier);
	}

	ifZ	xln ("setz	Z");
	ifNVC {
		xln ("sets	N");
		xln ("setc	C");
		xln ("movb	$0,V");
	}
	ifX	xln ("setc	X");

	xdirty_reg (dest->x86_reg);
	x_saveval (dest, size);
	x_postea (src);
	x_postea (dest);
#if 0
	xflush_all ();
	xold_readea2 (src, 0, 0, XQUICK_REGS | XQUICK_IMMS);
	xold_readea2 (dest, 1, 0, XQUICK_REGS);
	xold_readea2 (src, 0, 1, XQUICK_REGS | XQUICK_IMMS);
	xold_readea2 (dest, 1, 1, XQUICK_REGS);
	xold_readea2 (src, 0, 1, XQUICK_REGS | XQUICK_IMMS);
	xold_readea2 (dest, 1, 1, XQUICK_REGS);

	if (is_immediate (src)) {
		xln ("shl%s	%s,%s", szmov [size], src->identifier, dest->identifier);
	} else {
		xln ("movl	%s,%%ecx", src->identifier);
		xln ("shl%s	%%cl,%s", szmov [size], dest->identifier);
	}

	ifZ	xln ("setz	Z");
	ifNVC {
		xln ("sets	N");
		xln ("setc	C");
		xln ("movb	$0,V");
	}
	xln ("setc	X");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
#endif
}

void i386_func_roxr (ea_t *src, ea_t *dest, int size)
{
	xflush_all ();
	if ((src->mode != F_IMM) && (src->imm.val != 1)) {
		error ("X86 output of roxr unfinished. Only supports 1 bit immediate shifts.");
	}
	
	xold_readea (dest, 1, 0);
	xold_readea (dest, 1, 1);
	xold_readea (dest, 1, 1);

		xln ("movb	X,%%al");
		xln ("shr%s	$1,%s", szmov [size], sreg (EBX));
	ifNVC	xln ("setc	C");
		xln ("setc	X");
		xln ("shl%s	$%d,%s", szmov[size], size2len [size]-1, sreg (EAX));
	ifNVC	xln ("movb	$0,V");
		xln ("or%s	%s,%s", szmov [size], sreg (EAX), sreg (EBX));
	ifZNVC	xln ("test%s	%s,%s", szmov [size], sreg (EBX), sreg (EBX));
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (dest);
}
void i386_func_roxl (ea_t *src, ea_t *dest, int size)
{
	xflush_all ();
	if ((src->mode != F_IMM) && (src->imm.val != 1)) {
		error ("X86 output of roxr unfinished. Only supports 1 bit immediate shifts.");
	}
	
	xold_readea (dest, 1, 0);
	xold_readea (dest, 1, 1);
	xold_readea (dest, 1, 1);

		xln ("movzbl	X,%%eax");
		xln ("shl%s	$1,%s", szmov [size], sreg (EBX));
	ifNVC	xln ("setc	C");
		xln ("setc	X");
		xln ("or%s	%s,%s", szmov [size], sreg (EAX), sreg (EBX));
	ifNVC	xln ("movb	$0,V");
	ifZNVC	xln ("test%s	%s,%s", szmov [size], sreg (EBX), sreg (EBX));
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (dest);
}
void i386_func_rol (ea_t *src, ea_t *dest, int size)
{
	int len;
	len = size2len[size];
	/* fast case */
	ifZNVC {} else if (is_immediate (src)) {
		x_loadval (src, -1, XQUICK_IMMS);
		x_loadval (dest, -1, XQUICK_REGS);
	
		xln ("rol%s	%s,%s", szmov [size], src->identifier, dest->identifier);
		
		xdirty_reg (dest->x86_reg);
		x_saveval (dest, size);
		x_postea (src);
		x_postea (dest);
		return;
	}
	xflush_all ();
	src->op_size = BYTE;
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	xln ("movb	%%al,%%cl");
	xln ("decb	%%cl");
		xln ("rol%s	%%cl,%s", szmov [size], sreg (EBX));
		xln ("btl	$%d,%%ebx", len-1);
	ifNVC	xln ("setc	C");
		xln ("rol%s	$1,%s", szmov [size], sreg (EBX));
	ifNVC	xln ("movb	$0,V");
	ifZNVC	xln ("test%s	%s,%s", szmov [size], sreg (EBX), sreg (EBX));
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_ror (ea_t *src, ea_t *dest, int size)
{
	/* fast case */
	ifZNVC {} else if (is_immediate (src)) {
		x_loadval (src, -1, XQUICK_IMMS);
		x_loadval (dest, -1, 0);
	
		xln ("ror%s	%s,%s", szmov [size], src->identifier, dest->identifier);
		
		xdirty_reg (dest->x86_reg);
		x_saveval (dest, size);
		x_postea (src);
		x_postea (dest);
		return;
	}
	xflush_all ();
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	/* x86 ror doesn't set flags... */
		xln ("movb	%%al,%%cl");
		xln ("decb	%%cl");
		xln ("ror%s	%%cl,%s", szmov [size], sreg (EBX));
		xln ("btl	$0,%%ebx");
	ifNVC	xln ("setc	C");
		xln ("ror%s	$1,%s", szmov [size], sreg (EBX));
	ifNVC	xln ("movb	$0,V");
	ifZNVC	xln ("test%s	%s,%s", szmov [size], sreg (EBX), sreg (EBX));
	ifNVC	xln ("sets	N");
	ifZ	xln ("setz	Z");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_asr (ea_t *src, ea_t *dest, int size)
{
	/* special case we can optimise happily. immediate src. */
	if (is_immediate (src) && is_reg (dest)) {
		x_loadval (src, -1, XQUICK_IMMS);
		x_loadval (dest, -1, XQUICK_WRITEBACK);
	
		xln ("sar%s	%s,%s", szmov [size], src->identifier, dest->identifier);
		ifNVC {
			xln ("seto	V");
			xln ("setc	C");
		}
		ifX	xln ("setc	X");
		ifZ	xln ("setz	Z");
		ifNVC	xln ("sets	N");

		xdirty_reg (dest->x86_reg);
		x_saveval (dest, size);
		x_postea (src);
		x_postea (dest);
		return;
	}
	xflush_all ();
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

/* XXX warning if correct shift count zero behaviour matters then uncomment these */
//	xln ("movb	X,%%ch");
	xln ("movb	%%al,%%cl");
	xln ("cmpb	$%d,%%al", size2len[size]);
	xln ("jns	2f");
	xln ("sar%s	%%cl,%s", szmov [size], sreg (EBX));

	ifNVC {
		xln ("seto	V");
		xln ("setc	C");
	}
	ifX	xln ("setc	X");
	ifZ	xln ("setz	Z");
	ifNVC	xln ("sets	N");

	/* shift count zero do not set X... */
//		xln ("testb	%%al,%%al");
//		xln ("jnz	1f");
	
//		xln ("movb	%%ch,X");
	
		xout ("1:\n");
	/* shift count > thingy. smeg */
	//	xln ("cmpb	$%d,%%al", size2len[size]);
	//	xln ("jns	2f");
		
	//ifZNVC	xln ("test%s	%s,%s", szmov[size], sreg (EBX), sreg (EBX));
	//ifZ	xln ("setz	Z");

	/* Sign flag is crap with val=0 */
	//ifNVC	xln ("sets	N");

		xln ("jmp	3f");
		xout ("2:\n");
		xln ("mov%s	$0,%s", szmov[size], sreg (EBX));
		xln ("movb	$0,Z");
		xout ("3:\n");

	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}
void i386_func_lsr (ea_t *src, ea_t *dest, int size)
{
	/* special case we can optimise happily. no flags except X || Z needed, and
	 * immediate src, reg dest. */
	if (is_immediate (src) && is_reg (dest)) {
		x_loadval (src, -1, XQUICK_IMMS);
		x_loadval (dest, -1, XQUICK_WRITEBACK);

		xln ("shr%s	%s,%s", szmov [size], src->identifier, dest->identifier);
		ifZ	xln ("setz	Z");
		ifX	xln ("setc	X");
		ifNVC {	
			xln ("sets	N");
			xln ("setc	C");
			xln ("movb	$0,V");
		}
	
		xdirty_reg (dest->x86_reg);
		x_saveval (dest, size);
		x_postea (src);
		x_postea (dest);
		return;
	}
	xflush_all ();
	xold_readea2 (src, 0, 0, XQUICK_REGS);
	xold_readea2 (dest, 1, 0, XQUICK_REGS);
	xold_readea2 (src, 0, 1, XQUICK_REGS);
	xold_readea2 (dest, 1, 1, XQUICK_REGS);
	xold_readea2 (src, 0, 1, XQUICK_REGS);
	xold_readea2 (dest, 1, 1, XQUICK_REGS);

	xln ("movl	%s,%%ecx", src->identifier);
	xln ("movb	X,%%ah");
	xln ("shr%s	%%cl,%s", szmov [size], dest->identifier);

	ifZ	xln ("setz	Z");
		xln ("setc	X");
	ifNVC {	
		xln ("sets	N");
		xln ("setc	C");
		xln ("movb	$0,V");
	}
	
	/* shift count of zero is special.. (and never in immediate case) */
	if (!is_immediate (src)) {
		xln ("testb	%%al,%%al");
		xln ("jnz	1f");
		xln ("movb	$0,C");
		xln ("movb	%%ah,X");
	}
	
	xout ("1:\n");
	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_movem (ea_t *ea, int sz, int dr, int reg_mask)
{
	int i, offset, reg;
	sz++;
	ea_t dummy;

	x_loadea (ea);

	if (dr == 0) {
		/* reg to memory */
		if (ea->mode == 4) {
			/* pre-decrement mode */
			offset = 0;
			for (i=0; i<16; i++) {
				if (reg_mask & (1<<i)) {
					reg = xcopy_reg (15-i, sz, -1);
					do_bswap (reg, sz);
					xln ("mov%s	%s,-%d(%%ebp,%s)", szmov[sz], xreg (reg, sz), offset, ea->identifier);
					xreg_unlock (reg);
					offset += 1<<sz;
				}
			}
			reg = xload_reg (ea->reg, LONG, -1);
			xln ("subl	$%d,%s", offset, xreg (reg, LONG));
			xdirty_reg (reg);
			xreg_unlock (reg);
		} else {
			offset = 0;
			for (i=0; i<16; i++) {
				if (reg_mask & (1<<i)) {
					reg = xcopy_reg (i, sz, -1);
					do_bswap (reg, sz);
					xln ("mov%s	%s,%d(%%ebp,%s)", szmov[sz], xreg (reg, sz), offset, ea->identifier);
					xreg_unlock (reg);
					offset += 1<<sz;
				}
			}
		}
	} else {

		/* mem to reg */
		offset = 0;
		for (i=0; i<16; i++) {
			if (reg_mask & (1<<i)) {
				dummy.mode = 0;
				dummy.op_size = LONG;
				dummy.x86_reg = -1;
				dummy.x86_reg_writeback = -1;
				dummy.reg = i;
				reg = xalloc_reg (sz, -1);
				x_ea_set_val_2_reg (&dummy, LONG, reg);
				
				xln ("mov%s	%d(%%ebp,%s),%s", szmov[sz], offset, ea->identifier, xreg (reg, sz));
				do_bswap (reg, sz);
				if (sz == WORD) xln ("movswl	%s,%s", xreg (reg, WORD), xreg (reg, LONG));
				offset += 1<<sz;

				/* once for xalloc_reg and once for x_ea_set_val_2_reg */
				xreg_unlock (reg);
				xreg_unlock (reg);
			}
		}
		/* post-inc mode */
		if (ea->mode == 3) {
			reg = xload_reg (ea->reg, LONG, -1);
			xln ("addl	$%d,%s", offset, xreg (reg, LONG));
			xdirty_reg (reg);
			xreg_unlock (reg);
		}
	}
	/* deallocate temporary register allocations */
	if (ea->x86_reg != -1) {
		xreg_unlock (ea->x86_reg);
	}
}

void i386_func_subx (ea_t *src, ea_t *dest, int size)
{
	xflush_all ();
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	/* fucking X flag */
	ifNVCX {
		xln ("mov%s	%s,%s", szmov[size], sreg (EAX), sreg (ECX));
		xln ("mov%s	%s,%s", szmov[size], sreg (EBX), sreg (EDX));
		xln ("shr%s	$%d,%s", szmov[size], size2len[size]-1, sreg (ECX));
		xln ("shr%s	$%d,%s", szmov[size], size2len[size]-1, sreg (EDX));
	}
	xln ("add%s	X,%s", szmov [size], sreg (EAX));
	xln ("sub%s	%s,%s", szmov [size], sreg (EAX), sreg (EBX));

	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
	}
	
	ifZ {
		xln ("jz	1f");
		xln ("movb	$0,Z");
		xout ("1:\n");
	}

	ifNVCX {
		/* fucking X flag */
		xln ("mov%s	%s,%s", szmov[size], sreg (EBX), sreg (EAX));
		xln ("shr%s	$%d,%s", szmov[size], size2len[size]-1, sreg (EAX));
		/* ecx=flgs, edx=flgo, eax=flgn */
		xln ("xor%s	%s,%s", szmov[size], sreg (EAX), sreg (EDX));
		xln ("xor%s	%s,%s", szmov[size], sreg (ECX), sreg (EAX));
		xln ("and%s	%s,%s", szmov[size], sreg (EDX), sreg (EAX));
		xln ("xor%s	%s,%s", szmov[size], sreg (EAX), sreg (ECX));
		ifX	xln ("movb	%%cl,X");
		ifNVC	xln ("movb	%%cl,C");
	}
	
	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_addx (ea_t *src, ea_t *dest, int size)
{
	xflush_all ();
	xold_readea (src, 0, 0);
	xold_readea (dest, 1, 0);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);
	xold_readea (src, 0, 1);
	xold_readea (dest, 1, 1);

	/* fucking X flag */
	ifNVCX {
		xln ("mov%s	%s,%s", szmov[size], sreg (EAX), sreg (ECX));
		xln ("mov%s	%s,%s", szmov[size], sreg (EBX), sreg (EDX));
		xln ("shr%s	$%d,%s", szmov[size], size2len[size]-1, sreg (ECX));
		xln ("shr%s	$%d,%s", szmov[size], size2len[size]-1, sreg (EDX));
	}
	xln ("add%s	X,%s", szmov [size], sreg (EAX));
	xln ("add%s	%s,%s", szmov [size], sreg (EAX), sreg (EBX));

	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
	}
	
	ifZ {
		xln ("jz	1f");
		xln ("movb	$0,Z");
		xout ("1:\n");
	}

	ifNVCX {
		/* fucking X flag */
		xln ("mov%s	%s,%s", szmov[size], sreg (EBX), sreg (EAX));
		xln ("shr%s	$%d,%s", szmov[size], size2len[size]-1, sreg (EAX));
		/* ecx=flgs, edx=flgo, eax=flgn */
		xln ("xor%s	%s,%s", szmov[size], sreg (EDX), sreg (EAX));
		xln ("xor%s	%s,%s", szmov[size], sreg (ECX), sreg (EDX));
		xln ("and%s	%s,%s", szmov[size], sreg (EDX), sreg (EAX));
		xln ("xor%s	%s,%s", szmov[size], sreg (EAX), sreg (ECX));
		ifX	xln ("movb	%%cl,X");
		ifNVC	xln ("movb	%%cl,C");
	}
	xold_writeea (dest, size, EBX, 0);
	xold_writeea (dest, size, EBX, 1);
	xold_writeea (dest, size, EBX, 1);
	x_postea (src);
	x_postea (dest);
}

void i386_func_add (ea_t *src, ea_t *dest, int size)
{
	x_loadval (src, -1, XQUICK_REGS | XQUICK_IMMS);
	x_loadval (dest, -1, XQUICK_WRITEBACK);
	
	xln ("add%s	%s,%s", szmov [size], src->identifier, dest->identifier);
		
	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
		xln ("setc	C");
	}
	ifZ	xln ("setz	Z");
	ifX	xln ("setc	X");

	xdirty_reg (dest->x86_reg);
	x_saveval (dest, size);
	x_postea (src);
	x_postea (dest);
}

void i386_func_cmpa (ea_t *src, ea_t *dest, int size)
{
	src->op_size = size;
	dest->op_size = LONG;
	x_loadval (src, -1, (size != WORD ? 0 : XCAN_OVERWRITE));
	x_loadval (dest, -1, 0);

	if (size == WORD) xln ("movswl	%s,%s", xreg (src->x86_reg, WORD), xreg (src->x86_reg, LONG));

	xln ("cmpl	%s,%s", xreg (src->x86_reg, LONG), dest->identifier);

	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
		xln ("setc	C");
	}
	ifZ	xln ("setz	Z");

	x_postea (src);
	x_postea (dest);
}

void i386_func_cmp (ea_t *src, ea_t *dest, int size)
{
	x_loadval (src, -1, XQUICK_IMMS);
	x_loadval (dest, -1, 0);
	
	xln ("cmp%s	%s,%s", szmov [size], src->identifier, dest->identifier);

	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
		xln ("setc	C");
	}
	ifZ	xln ("setz	Z");

	x_postea (src);
	x_postea (dest);
}

void i386_func_sub (ea_t *src, ea_t *dest, int size)
{
	x_loadval (src, -1, XQUICK_REGS | XQUICK_IMMS);
	x_loadval (dest, -1, XQUICK_WRITEBACK);
	
	xln ("sub%s	%s,%s", szmov [size], src->identifier, dest->identifier);

	ifNVC {
		xln ("sets	N");
		xln ("seto	V");
		xln ("setc	C");
	}
	ifZ	xln ("setz	Z");
	ifX	xln ("setc	X");

	xdirty_reg (dest->x86_reg);
	x_saveval (dest, size);
	x_postea (src);
	x_postea (dest);
}

void i386_func_adda (ea_t *src, ea_t *dest, int size)
{
	src->op_size = size;
	dest->op_size = LONG;
	x_loadval (src, -1, (size != WORD ? 0 : XCAN_OVERWRITE));
	x_loadval (dest, -1, 0);
	
	if (size == WORD) xln ("movswl	%s,%s", xreg (src->x86_reg, WORD), xreg (src->x86_reg, LONG));
	
	xln ("addl	%s,%s", xreg (src->x86_reg, LONG), dest->identifier);
		
	xdirty_reg (dest->x86_reg);
	x_saveval (dest, LONG);
	x_postea (src);
	x_postea (dest);
}

void i386_func_suba (ea_t *src, ea_t *dest, int size)
{
	src->op_size = size;
	dest->op_size = LONG;
	x_loadval (src, -1, (size != WORD ? 0 : XCAN_OVERWRITE));
	x_loadval (dest, -1, 0);
	
	if (size == WORD) xln ("movswl	%s,%s", xreg (src->x86_reg, WORD), xreg (src->x86_reg, LONG));
	
	xln ("subl	%s,%s", xreg (src->x86_reg, LONG), dest->identifier);
		
	xdirty_reg (dest->x86_reg);
	x_saveval (dest, LONG);
	x_postea (src);
	x_postea (dest);
}

void i386_func_swap (int reg)
{
	struct ea_t ea;

	ea.mode = 0;
	ea.reg = reg;
	ea.op_size = LONG;
	ea.x86_reg = -1;
	ea.x86_reg_writeback = -1;

	ifZNVC {
		x_loadval (&ea, -1, 0);
		ifNVC	xln ("movb	$0,V");
			xln ("rorl	$16,%s", ea.identifier);
		ifNVC	xln ("movb	$0,C");
		ifZNVC	xln ("testl	%s,%s", ea.identifier, ea.identifier);
		ifNVC	xln ("sets	N");
		ifZ	xln ("setz	Z");
	} else {
		x_loadval (&ea, -1, 0);
		xln ("rorl	$16,%s", ea.identifier);
	}
	xdirty_reg (ea.x86_reg);
	x_saveval (&ea, LONG);
	x_postea (&ea);
}

void i386_func_jsr (ea_t *dest)
{
	char buf[256];
	xflush_all ();
	snprintf (buf, sizeof (buf), "%x", cur_bitpos);
	add_label (buf, C_ADDR, cur_bitpos - BASE);
	add_fixup (0, C_ADDR, buf);

	/* XXX this is a bit inefficient, since the label is a
	 * constant and could be bswapped at compile-time
	 * -- but the scheduling makes this irrelevant */
	xln ("movl	%d(%%ebp),%%ebx", regpos (15));
	xln ("movl	$__D%x,%%eax", cur_bitpos);
	xln ("subl	$4,%%ebx");
	xln ("bswap	%%eax");
	xln ("movl	%%ebx,%d(%%ebp)", regpos (15));
	xln ("movl	%%eax,(%%ebp,%%ebx)");
	
	i386_jump (dest);
}

void i386_func_bcc (ea_t *dest, int cond)
{
	xflush_all ();
	if (cond == 0) {
		i386_jump (dest);
	} else {
		x_cond_begin (cond);
		i386_jump_e (dest);
		x_cond_end (cond);
	}
}

/* XXX note: this does not call i386_jump and therefore does not
 * check for pending exceptions. an infinite DBcc loop will not
 * allow exceptions to occur.
 * does it matter? */
void i386_func_dbcc (const char *label, int cond, int reg)
{
	xflush_all ();
	if (cond == 1) {
		xln ("decw	%d(%%ebp)", regpos (reg));
		xln ("cmpw	$-1,%d(%%ebp)", regpos (reg));
		xln ("jne	__N%s", label);
	} else {
		/* relies on naming of x_cond_begin labels */
		x_cond_begin (cond);
		xln ("jmp	10f");
		x_cond_end ();
		xln ("decw	%d(%%ebp)", regpos (reg));
		xln ("cmpw	$-1,%d(%%ebp)", regpos (reg));
		xln ("jne	__N%s", label);
		xout ("10:\n");
	}
}

void i386_func_clr (ea_t *dest)
{
	xflush_all ();
		xln ("xorl	%%eax,%%eax");
	ifZ	xln ("movb	$1,Z");
	ifNVC	xln ("movb	%%al,N");
	ifNVC	xln ("movb	%%al,V");
	ifNVC	xln ("movb	%%al,C");
	
	xold_writeea (dest, dest->op_size, EAX, 0);
	xold_writeea (dest, dest->op_size, EAX, 1);
	xold_writeea (dest, dest->op_size, EAX, 1);
	x_postea (dest);
}

void i386_func_exg (int reg1, int reg2)
{
	xflush_all ();
	xln ("movl	%d(%%ebp),%%eax", regpos (reg1));
	xln ("movl	%d(%%ebp),%%ebx", regpos (reg2));
	xln ("movl	%%eax,%d(%%ebp)", regpos (reg2));
	xln ("movl	%%ebx,%d(%%ebp)", regpos (reg1));
}

void i386_func_ext (int reg, int size)
{
	int x86reg = xload_reg (reg, size-1, -1);

	if (size == WORD) {
		xln ("movsbw	%s,%s", xreg (x86reg, BYTE), xreg (x86reg, WORD));
	} else {
		xln ("movswl	%s,%s", xreg (x86reg, WORD), xreg (x86reg, LONG));
	}

	ifNVC	xln ("movb	$0,V");
	ifNVC	xln ("movb	$0,C");
	ifZNVC	xln ("test%s	%s,%s", szmov[size], xreg (x86reg,size), xreg (x86reg,size));
	ifZ	xln ("setz	Z");
	ifNVC	xln ("sets	N");
	xdirty_reg (x86reg);
	xreg_unlock (x86reg);
}

void i386_func_rte ()
{
	xflush_all ();
	xln ("movb	bX,%%bh");
	xln ("movb	bN,%%bl");
	xln ("movb	bZ,%%ch");
	xln ("movb	bV,%%cl");
	xln ("movb	bC,%%dh");
	xln ("movb	%%bh,X");
	xln ("movb	%%bl,N");
	xln ("movb	%%ch,Z");
	xln ("movb	%%cl,V");
	xln ("movb	%%dh,C");
	xln ("movl	rdest,%%eax");
	xln ("movl	$0,rdest");
	//xln ("jmp	jumptable");
	xln ("jmp	*jtab(,%%eax,2)");
}

void i386_func_rts ()
{
	xflush_all ();
	xln ("movl	%d(%%ebp),%%ebx", regpos (15));
	xln ("movl	(%%ebx,%%ebp),%%eax");
	xln ("addl	$4,%%ebx");
	xln ("bswap	%%eax");
	xln ("movl	%%ebx,%d(%%ebp)", regpos (15));
	//xln ("jmp	jumptable");
	xln ("jmp	*jtab(,%%eax,2)");
}

void i386_func_illegal ()
{
	xflush_all ();
	xln ("pushl	line_no");
	xln ("pushl	$illegal_fmt");
	xln ("call	printf");
	xln ("addl	$8,%%esp");
	xln ("call	abort");
}

void i386_func_hcall (int val)
{
	xflush_all ();
	xln ("call	*hcalls+%d", val*4);
}

void i386_func_link (int reg, int val)
{
	xflush_all ();
	xln ("movl	%d(%%ebp),%%ebx", regpos (15));
	xln ("subl	$4,%%ebx");
	
	xln ("movl	%d(%%ebp),%%eax", regpos (reg));
	xln ("bswap	%%eax");
	
	xln ("movl	%%eax,(%%ebp,%%ebx)");
	xln ("addl	$%d,%d(%%ebp)", val-4, regpos (15));
	xln ("movl	%%ebx,%d(%%ebp)", regpos (reg));
}

void i386_func_unlk (int reg)
{
	xflush_all ();
	xln ("movl	%d(%%ebp),%%eax", regpos (reg));
	xln ("movl	(%%ebp,%%eax),%%ebx");
	xln ("addl	$4,%%eax");
	xln ("bswap	%%ebx");
	xln ("movl	%%ebx,%d(%%ebp)", regpos (reg));
	xln ("movl	%%eax,%d(%%ebp)", regpos (15));
}

void i386_func_move (ea_t *src, ea_t *dest, int size)
{
	x_loadval (src, -1, XQUICK_IMMS);

	ifNVC xln ("movb	$0,V");
	ifNVC xln ("movb	$0,C");

	if (is_immediate (src)) {
		ifZ	xln ("movb	$%d,Z", src->imm.val == 0);
		ifNVC	xln ("movb	$%d,N", src->imm.val < 0);
	} else {
		ifZNVC xln ("test%s	%s,%s", szmov [size], src->identifier, src->identifier);
		ifZ xln ("setz	Z");
		ifNVC xln ("sets	N");
	}
	
	if (is_reg (dest)) {
		dest->x86_reg = xload_reg2 (dest->reg, size, -1, XLOADREG_FOR_OVERWRITE, szfull);
	} else {
		dest->x86_reg = xalloc_reg (size, -1);
	}
	xln ("mov%s	%s,%s", szmov[size], src->identifier,
			xreg (dest->x86_reg, size));
	xdirty_reg (dest->x86_reg);
	
	x_saveval (dest, size);
	x_postea (src);
	x_postea (dest);
}

void i386_func_movea (ea_t *src, int reg_dest)
{
#if 0
	xflush_all ();
	xold_readea (src, 0, 0);
	xold_readea (src, 0, 1);
	xold_readea (src, 0, 1);
	if (src->op_size == WORD) xln ("movswl	%%ax,%%eax");
	xln ("movl	%%eax,%d(%%ebp)", regpos (reg_dest));
	x_postea (src);
#endif
	int reg;
	struct ea_t dest;

	dest.mode = 1;
	dest.reg = reg_dest;
	dest.op_size = LONG;
	dest.x86_reg = -1;
	dest.x86_reg_writeback = -1;
	
	x_loadval (src, -1, 0);
	
	reg = xalloc_reg (LONG, -1);

	if (src->op_size == WORD)
		xln ("movswl	%s,%s", xreg (src->x86_reg, WORD), xreg (reg, LONG));
	else
		xln ("movl	%s,%s", xreg (src->x86_reg, LONG), xreg (reg, LONG));
	
	x_ea_set_val_2_reg (&dest, LONG, reg);

	xreg_unlock (reg);
	xdirty_reg (reg);
	
	x_saveval (&dest, LONG);
	x_postea (src);
	x_postea (&dest);
}

void i386_func_tst (ea_t *ea)
{
	x_loadval (ea, -1, 0);
	ifNVC	xln ("movb	$0,C");
	ifNVC	xln ("movb	$0,V");
	xln ("test%s	%s,%s", szmov[ea->op_size], ea->identifier, ea->identifier);
	ifZ	xln ("setz	Z");
	ifNVC	xln ("sets	N");
	x_postea (ea);
}

void i386_func_pea (ea_t *ea)
{
	int a7;
	
	x_loadea (ea);
	a7 = xload_reg (15, LONG, -1);

	xln ("subl	$4,%s", xreg (a7, LONG));
	xln ("bswap	%s", ea->identifier);
	xln ("movl	%s,(%%ebp,%s)", ea->identifier, xreg (a7, LONG));
	
	xdirty_reg (a7);
	xreg_unlock (a7);
	x_postea (ea);
}

void i386_func_lea (ea_t *ea, int reg)
{
	struct ea_t dest;

	dest.mode = 1;
	dest.reg = reg;
	dest.op_size = LONG;
	dest.x86_reg = -1;
	dest.x86_reg_writeback = -1;

	x_loadea (ea);

	x_ea_set_val_2_reg (&dest, LONG, ea->x86_reg);
	x_saveval (&dest, LONG);
	x_postea (ea);
	x_postea (&dest);
}

void i386_func_scc (ea_t *dest, int cond)
{
	xflush_all ();
	x_cond_begin (cond);
	
	xln ("movb	$0xff,%%al");
	xold_writeea (dest, BYTE, EAX, 0);
	xold_writeea (dest, BYTE, EAX, 1);
	xold_writeea (dest, BYTE, EAX, 1);
	x_postea (dest);
	xln ("jmp	10f");
	
	x_cond_end ();

	xln ("xorb	%%al,%%al");
	xold_writeea (dest, BYTE, EAX, 0);
	xold_writeea (dest, BYTE, EAX, 1);
	xold_writeea (dest, BYTE, EAX, 1);
	x_postea (dest);
	
	xout ("10:\n");
}

static void output_op (op_t *op, op_t *next)
{
	xregalloc_age ();

	op->src.x86_reg = -1;
	op->src.x86_reg_writeback = -1;
	op->dest.x86_reg = -1;
	op->dest.x86_reg_writeback = -1;
	
	switch (op->optype) {
		case OP_MULS: i386_func_muls (&op->src, &op->dest); break;
		case OP_MULU: i386_func_mulu (&op->src, &op->dest); break;
		case OP_ADDA: i386_func_adda (&op->src, &op->dest, op->size); break;
		case OP_SUBA: i386_func_suba (&op->src, &op->dest, op->size); break;
		case OP_ADD: i386_func_add (&op->src, &op->dest, op->size); break;
		case OP_SUB: i386_func_sub (&op->src, &op->dest, op->size); break;
		case OP_NOT: i386_func_not (&op->src, op->size); break;
		case OP_NEGX: i386_func_negx (&op->src, op->size); break;
		case OP_NEG: i386_func_neg (&op->src, op->size); break;
		case OP_DIVS: i386_func_divs (&op->src, op->dest.reg); break;
		case OP_DIVU: i386_func_divu (&op->src, op->dest.reg); break;
		case OP_ADDX: i386_func_addx (&op->src, &op->dest, op->size); break;
		case OP_SUBX: i386_func_subx (&op->src, &op->dest, op->size); break;
		case OP_BCHG: i386_func_bchg (&op->src, &op->dest, op->size); break;
		case OP_BCLR: i386_func_bclr (&op->src, &op->dest, op->size); break;
		case OP_BSET: i386_func_bset (&op->src, &op->dest, op->size); break;
		case OP_BTST: i386_func_btst (&op->src, &op->dest, op->size); break;
		case OP_ASL: i386_func_asl (&op->src, &op->dest, op->size); break;
		case OP_ASR: i386_func_asr (&op->src, &op->dest, op->size); break;
		case OP_LSL: i386_func_lsl (&op->src, &op->dest, op->size); break;
		case OP_LSR: i386_func_lsr (&op->src, &op->dest, op->size); break;
		case OP_ROL: i386_func_rol (&op->src, &op->dest, op->size); break;
		case OP_ROR: i386_func_ror (&op->src, &op->dest, op->size); break;
		case OP_ROXL: i386_func_roxl (&op->src, &op->dest, op->size); break;
		case OP_ROXR: i386_func_roxr (&op->src, &op->dest, op->size); break;
		case OP_OR: i386_func_logop (&op->src, &op->dest, op->size, '|'); break;
		case OP_AND: i386_func_logop (&op->src, &op->dest, op->size, '&'); break;
		case OP_XOR: i386_func_logop (&op->src, &op->dest, op->size, '^'); break;
		case OP_SWAP: i386_func_swap (op->src.reg); break;
		case OP_JSR: i386_func_jsr (&op->src); break;
		case OP_BCC: i386_func_bcc (&op->src, op->opcode.Bcc.cond); break;
		case OP_DBCC: i386_func_dbcc (op->dest.imm.label, op->opcode.Bcc.cond, op->src.reg); break;
		case OP_CLR: i386_func_clr (&op->src); break;
		case OP_CMP: i386_func_cmp (&op->src, &op->dest, op->size); break;
		case OP_CMPA: i386_func_cmpa (&op->src, &op->dest, op->size); break;
		case OP_EXG: i386_func_exg (op->src.reg, op->dest.reg); break;
		case OP_EXT: i386_func_ext (op->src.reg, op->size); break;
		case OP_MOVEM: i386_func_movem (&op->src, op->opcode.movem.sz, op->opcode.movem.dr, op->dest.reg); break;
		case OP_RTE: i386_func_rte (); break;
		case OP_RTS: i386_func_rts (); break;
		case OP_ILLEGAL: i386_func_illegal (); break;
		case OP_HCALL: i386_func_hcall (op->src.imm.val); break;
		case OP_LINK: i386_func_link (op->src.reg, op->dest.imm.val); break;
		case OP_UNLK: i386_func_unlk (op->src.reg); break;
		case OP_MOVE: i386_func_move (&op->src, &op->dest, op->size); break;
		case OP_MOVEA: i386_func_movea (&op->src, op->dest.reg); break;
		case OP_TST: i386_func_tst (&op->src); break;
		case OP_PEA: i386_func_pea (&op->src); break;
		case OP_LEA: i386_func_lea (&op->src, op->dest.reg); break;
		case OP_JMP: i386_jump (&op->src); break;
		case OP_SCC: i386_func_scc (&op->src, op->opcode.DBcc.cond); break;

		default: 
			     printf ("type %d\n", op->optype);
			     assert (0);
	}

/*	int i;
	int fucked = 0;
	for (i=0; i<X86REG_MAX; i++) {
		printf ("l%d X86-reg %d: contained 68k reg %d, age %d, dirty %d, locked %d\n",
				line_no,
				i, i386_regalloc[i].m68k_reg,
				i386_regalloc[i].age,
				i386_regalloc[i].dirty,
				i386_regalloc[i].locked);
		if (i386_regalloc[i].locked) fucked = 1;
	}
	printf ("\n");
	assert (fucked == 0);*/
}

static void do_pending ()
{
	if (pending_flush) {
		xflush_all ();
	}
	pending_flush = cur_flush;
	cur_flush = 0;

#ifdef DUMP_LOADS_OF_CRAP
	/* state must be consistent to print reg contents */
	xflush_all ();
#endif /* DUMP_LOADS_OF_CRAP */
	
	if (pending_labels) {
		rout (pending_labels);
		free (pending_labels);
	}
	pending_labels = cur_labels;
	cur_labels = calloc (LABBUF_LEN, 1);
	strcat (cur_labels, "\n");
	
	if (pending.optype) output_op (&pending, NULL);
}

void i386_push_op (enum M68K_OPS type, ea_t *src, ea_t *dest, int size)
{
	i386_push_op2 (type, src, dest, size, 0);
}
void i386_push_op2 (enum M68K_OPS type, ea_t *src, ea_t *dest, int size, int opcode)
{
	op_t op;

	op.size = size;
	op.opcode.code = opcode;
	op.optype = type;
	if (type == OP_JSR)
		return_target = 1;
	else
		return_target = 0;
	if (src) {
		op.src = *src;
	}
	if (dest) {
		op.dest = *dest;
	}
	next_op_type = op.optype;

	do_pending ();
	pending = op;
}
void i386_push_op_basic (enum M68K_OPS type)
{
	i386_push_op2 (type, NULL, NULL, 0, 0);
}

