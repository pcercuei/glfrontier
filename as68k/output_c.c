
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "dict.h"
#include "as68k.h"
#include "output.h"

//#define DUMP_LOADS_OF_CRAP

static op_t pending;
static char pending_func_name[LAB_LEN];
static enum M68K_OPS next_op_type;

#define GEN_CALL	0
#define GEN_BODY	1
static int gen_mode;
static int return_target;

/* which flags an M68K_OP thingy may set */
#define fN	(1<<0)
#define fZ	(1<<1)
#define fV	(1<<2)
#define fC	(1<<3)
#define fX	(1<<4)
#define fNZVCX	(fN | fZ | fV | fC | fX)
#define fNZVC	(fN | fZ | fV | fC)
#define _fSafe	0
/* _fSafe are ones i haven't got round to doing yet. */

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
	fNZVCX, // OP_NEGX
	fNZVCX, // OP_NEG
	fNZVC, // OP_DIVU
	fNZVC, // OP_DIVS
	fNZVCX, // OP_ASL,
	fNZVCX, // OP_LSL
	fNZVCX, // OP_ROXR
	fNZVCX, // OP_ROXL
	fNZVC, // OP_ROL
	fNZVC, // OP_ROR
	fNZVCX, // OP_ASR
	fNZVCX, // OP_LSR,
	0, // OP_MOVEM
	fNZVCX, // OP_SUBX
	fNZVCX, // OP_ADDX
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

/* does the flag need to be set? if the next instruction will
 * set the flag then no. */
#define ifZ	if(!(sets_flags[next_op_type] & fZ))
#define ifN	if(!(sets_flags[next_op_type] & fN))
#define ifV	if(!(sets_flags[next_op_type] & fV))
#define ifC	if(!(sets_flags[next_op_type] & fC))
//#define ifCX	if((!(sets_flags[next_op_type] & fC)) && (!(sets_flags[next_op_type] & fX)))
//#define ifX	if(!(sets_flags[next_op_type] & fX))
#define ifVoN	if((!(sets_flags[next_op_type] & fV)) || (!(sets_flags[next_op_type] & fN)))

FILE *f_temp;
FILE *c_out;
FILE *c_out2; /* writing functions here */

/* useless stats */
static int num_funcs;

#define SWAP_COUT	{ f_temp = c_out; c_out = c_out2; c_out2 = f_temp; }

static void output_op (op_t *op, op_t *next);

static void cout (const char *format, ...)
{
	va_list argptr;

	va_start (argptr, format);
	vfprintf (c_out, format, argptr);
	va_end (argptr);
}
static void cln (const char *format, ...)
{
	va_list argptr;

	fputc ('\t', c_out);
	va_start (argptr, format);
	vfprintf (c_out, format, argptr);
	va_end (argptr);
	fputc ('\n', c_out);
}
/* C code generation shit */
const char *c_usizes[] = { "u8", "u16", "u32" };
const char *c_ssizes[] = { "s8", "s16", "s32" };
const char *mem_read_funcs[] = { "rdbyte", "rdword", "rdlong" };
const char *mem_write_funcs[] = { "wrbyte", "wrword", "wrlong" };

void c_label (const char *lab)
{
	cout ("__N%s:\n", lab);
	add_fixup (0, C_ADDR, lab);
}

void c_addr_label (int labelled)
{
	if (labelled || return_target || (get_bitpos () == 0x1c)) {
		cout ("\tcase (0x%x):\n", get_bitpos ());
	}
	cln ("#ifdef M68K_DEBUG");
	cln ("	line_no = %d;", line_no);
#ifdef DUMP_LOADS_OF_CRAP
	cln ("	DumpRegsChanged ();");
#endif /* DUMP_LOADS_OF_CRAP */
	cln ("#endif");
}

void c_begin (const char *src_filename, const char *bin_filename)
{
	char buf[128];
	snprintf (buf, sizeof (buf), ".%s.c", src_filename);
	if ((c_out = fopen (buf, "w"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}
	snprintf (buf, sizeof (buf), ".%s.fn.c", src_filename);
	if ((c_out2 = fopen (buf, "w"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}

	cout ("void Init680x0 () {\n");
	cln ("STRam = &m68kram[0];");
	cln ("load_binfile (\"%s\");", bin_filename);
	cout ("}\n");
	
	cout ("void Start680x0 ()\n{\n");
	cout ("\ts32 i, jdest = 0x1c;\n");
	cout ("\tRegs[15]._u32 = MEM_SIZE;\n\n");
	cout ("jumptable:\n\tswitch (jdest) {\n");
}

void c_end (const char *src_filename)
{
	/* some more crap needs to be added to the top */
	char c, buf[128];
	FILE *f;
	struct Fixup *fix;
	struct Label *lab;

	/* remember to do last pending instruction */
	next_op_type = OP_NONE;
	if (pending.optype) {
		gen_mode = GEN_BODY;
		output_op (&pending, NULL);
		gen_mode = GEN_CALL;
	}

	fclose (c_out);
	fclose (c_out2);

	snprintf (buf, sizeof (buf), "%s.c", src_filename);
	if ((c_out = fopen (buf, "w"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}

	/* _host.c contains much necessary boilerplate code. include it */
	cout ("#include \"_host.c\"\n");
	
	/* call prototype turds */
	cout ("#ifdef PART1\n");
	fix = fix_first;
	for (; fix != NULL; fix = fix->next) {
		if (fix->size == C_FUNC) {
			cout ("extern void %s ();\n", fix->label);
			continue;
		}
	}
	cout ("#endif /* PART1 */\n");
	/* address 'fixups' */
	fix = fix_first;
	for (; fix != NULL; fix = fix->next) {
		if (fix->size != C_ADDR) continue;

		lab = get_label (fix->label);
		if (!lab) {
			/* ignore. let pass2 throw the error. */
			continue;
		}
		cout ("#define __D%s (0x%x)\n", lab->name, lab->val+BASE);
	}

	/* the code we made */
	snprintf (buf, sizeof (buf), ".%s.fn.c", src_filename);
	if ((f = fopen (buf, "r"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}
	cout ("#ifdef PART2\n");
	while ((c = fgetc (f)) != EOF) fputc (c, c_out);
	fclose (f);
	remove (buf);
	cout ("#endif /* PART2 */\n");

	snprintf (buf, sizeof (buf), ".%s.c", src_filename);
	if ((f = fopen (buf, "r"))==NULL) {
		fprintf (stderr, "Error: Cannot open %s for writing.\n", buf);
		exit (-1);
	}
	cout ("#ifdef PART1\n");
	while ((c = fgetc (f)) != EOF) fputc (c, c_out);
	fclose (f);
	remove (buf);

	/* and then computed jump table */
	cln ("goto end_;");
	cout ("\t\tdefault: \n");
	cln ("#ifdef M68K_DEBUG");
	cln ("	printf (\"Bad jump target at line %%d: $%%x (rdest=$%%x).\\n\", line_no, jdest, rdest);");
	cln ("#else");
	cln ("	printf (\"Bad jump target: $%%x (rdest=$%%x).\\n\", jdest, rdest);");
	cln ("#endif");
	cln ("abort ();");
	cout ("	}\n\n");
	
	cout ("handle_exception:\n");
	cln ("for (i=0; i<32; i++) {");
	cln ("	if (exceptions_pending & ((1<<i))) {");
	cln ("		jdest = exception_handlers[i];");
	cln ("		if (!--exceptions_pending_nums[i]) {");
	cln ("			exceptions_pending ^= 1<<i;");
	cln ("		}");
	/* save flags */
	cln ("		bN=N; bnZ=nZ; bV=V; bC=C; bX=X;");
	cln ("		goto jumptable;");
	cln ("	}");
	cln ("}");
	cln ("/* shouldn't get here... */");
	cln ("jdest = rdest;");
	cln ("goto jumptable;\n");
	
	cout ("end_:\treturn;\n}\n");
	cout ("#endif /* PART1 */\n");

	fclose (c_out);
	
	printf ("%d functions generated.\n", num_funcs);
}

static int is_immediate (ea_t *ea)
{
	return ((ea->mode == 7) && (ea->reg == 4));
}

static void c_postea (ea_t *ea)
{
	int inc;

	inc = 1<<ea->op_size;
	if ((ea->reg == 15) && (inc == 1)) inc = 2;
	
	switch (ea->mode) {
		/* areg postinc */
		case 3: cln ("Regs[%d]._s32 += %d;", ea->reg, inc); break;
		/* areg predec */
		case 4: cln ("Regs[%d]._s32 -= %d;", ea->reg, inc); break;
		default: break;
	}
}
static void c_ea_get_address (ea_t *ea, char *buf)
{
	int inc;
	switch (ea->mode) {
		/* dreg, areg */
		case 0: case 1: assert (0);
		/* areg indirect, postinc */
		case 2: case 3: 
			sprintf (buf, "(Regs[%d]._s32)", ea->reg); break;
		/* areg predec */
		case 4: 
			inc = 1<<ea->op_size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			sprintf (buf, "(Regs[%d]._s32-%d)", ea->reg, inc);
			break;
		/* areg offset */
		case 5:
			sprintf (buf, "(Regs[%d]._s32%+d)", ea->reg, ea->imm.val);
			break;
		/* areg offset + reg */
		case 6:
			sprintf (buf, "(Regs[%d]._s32+((%s)Regs[%d]._s32)%+d)", ea->reg, (ea->ext._.size ? "s32" : "s16"), ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), ea->ext._.displacement);
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				sprintf (buf, "(%d)", ea->imm.val);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					sprintf (buf, "(__D%s)", ea->imm.label);
				} else {
					sprintf (buf, "(%d)", ea->imm.val);
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				assert (0);
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				if (ea->imm.has_label)
					sprintf (buf, "__D%s", ea->imm.label);
				else
					error ("Absolute value not allowed.");
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				if (!ea->imm.has_label) error ("Absolute value not allowed.");
				sprintf (buf, "(__D%s + Regs[%d]._%s)",
						ea->imm.label,
						ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0),
						(ea->ext._.size ? "s32" : "s16"));
			}
			break;
		default:
			assert (0);
	}
}
static void c_readea (ea_t *ea, char *buf)
{
	int inc;
	switch (ea->mode) {
		/* dreg, areg */
		case 0:
		case 1:
			sprintf (buf, "Regs[%d]._%s", ea->reg, c_ssizes[ea->op_size]);
			break;
		/* areg indirect */
		case 2:
			sprintf (buf, "%s(Regs[%d]._s32)", mem_read_funcs[ea->op_size], ea->reg);
			break;
		/* areg postinc */
		case 3:
			sprintf (buf, "%s(Regs[%d]._s32)", mem_read_funcs[ea->op_size], ea->reg);
			break;
		/* areg predec */
		case 4:
			inc = 1<<ea->op_size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			sprintf (buf, "%s(Regs[%d]._s32-%d)", mem_read_funcs[ea->op_size], ea->reg, inc);
			break;
		/* areg offset */
		case 5:
			sprintf (buf, "%s(Regs[%d]._s32%+d)", mem_read_funcs[ea->op_size], ea->reg, ea->imm.val);
			break;
		/* areg offset + reg */
		case 6:
			sprintf (buf, "%s(Regs[%d]._s32+((%s)Regs[%d]._s32)%+d)", mem_read_funcs[ea->op_size], ea->reg, (ea->ext._.size ? "s32" : "s16"), ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), ea->ext._.displacement);
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				sprintf (buf, "%s(%d)", mem_read_funcs[ea->op_size], ea->imm.val);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					sprintf (buf, "%s(__D%s)", mem_read_funcs[ea->op_size], ea->imm.label);
				} else {
					sprintf (buf, "%s(%d)", mem_read_funcs[ea->op_size], ea->imm.val);
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				if (ea->imm.has_label) {
					sprintf (buf, "__D%s", ea->imm.label);
				} else {
					sprintf (buf, "%d", ea->imm.val);
				}
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				if (ea->imm.has_label)
					sprintf (buf, "%s(__D%s)", mem_read_funcs[ea->op_size], ea->imm.label);
				else
					error ("Absolute value not allowed.");
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				if (!ea->imm.has_label) error ("Absolute value not allowed.");
				sprintf (buf, "%s(__D%s + Regs[%d]._%s)",
						mem_read_funcs[ea->op_size],
						ea->imm.label,
						ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0),
						(ea->ext._.size ? "s32" : "s16"));
			}
			break;
		default:
			error ("nasty error in c_readea()");
	}
}
static void c_writeea (ea_t *ea, int size, const char *val)
{
	int inc;
	switch (ea->mode) {
		/* dreg, areg */
		case 0: case 1:
			cln ("Regs[%d]._%s = %s;", ea->reg, (size==BYTE ? "s8" : (size==WORD ? "s16" : "s32")), val);
			break;
		/* areg indirect */
		case 2:
			cln ("%s(Regs[%d]._s32, %s);", mem_write_funcs[size], ea->reg, val);
			break;
		/* areg postinc */
		case 3:
			cln ("%s(Regs[%d]._s32, %s);", mem_write_funcs[size], ea->reg, val);
			break;
		/* areg predec */
		case 4:
			inc = 1<<size;
			/* stack pointer always by 2 */
			if ((ea->reg == 15) && (inc == 1)) inc = 2;
			
			cln ("%s(Regs[%d]._s32-%d, %s);", mem_write_funcs[size], ea->reg, inc, val);
			break;
		/* areg offset */
		case 5:
			cln ("%s(Regs[%d]._s32%+d, %s);", mem_write_funcs[size], ea->reg, ea->imm.val, val);
			break;
		/* areg offset + reg */
		case 6:
			cln ("%s(Regs[%d]._s32+(Regs[%d]._%s)%+d, %s);", mem_write_funcs[size], ea->reg, ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), (ea->ext._.size ? "s32" : "s16"), ea->ext._.displacement, val);
			break;
		/* yes */
		case 7:
			/* $xxx.w */
			if (ea->reg == 0) {
				cln ("%s(%d, %s);", mem_write_funcs[size], ea->imm.val, val);
			}
			/* $xxx.l */
			else if (ea->reg == 1) {
				if (ea->imm.has_label) {
					cln ("%s(__D%s, %s);", mem_write_funcs[size], ea->imm.label, val);
				} else {
					cln ("%s(%d, %s);", mem_write_funcs[size], ea->imm.val, val);
				}
			}
			/* immediate */
			else if (ea->reg == 4) {
				if (ea->imm.has_label) {
					cln ("%s(__D%s, %s);", mem_write_funcs[size], ea->imm.label, val);
				} else {
					cln ("%s(%d, %s);", mem_write_funcs[size], ea->imm.val, val);
				}
			}
			/* PC + offset */
			else if (ea->reg == 2) {
				if (ea->imm.has_label)
					cln ("%s(__D%s, %s);", mem_write_funcs[size], ea->imm.label, val);
				else
					error ("Absolute value not allowed.");
			}
			/* PC + INDEX + OFFSET */
			else if (ea->reg == 3) {
				if (!ea->imm.has_label) error ("Absolute value not allowed.");
				cln ("%s(__D%s + Regs[%d]._%s, %s);", mem_write_funcs[size], ea->imm.label, ea->ext._.reg + (ea->ext._.d_or_a ? 8 : 0), (ea->ext._.size ? "s32" : "s16"), val);
			}
			else {
				error ("wtf in c_writeea ()");
			}
			break;
		default:
			error ("nasty error in c_writeea()");
	}
}

static void c_eval_cond (int cond)
{
	switch (cond) {
		/* ra */
		case 0: cout ("1"); break;
		/* f */
		case 1: cout ("0"); break;
		/* hi */
		case 2: cout ("((!C) && nZ)"); break;
		/* ls */
		case 3: cout ("(C || (!nZ))"); break;
		/* cc */
		case 4: cout ("(!C)"); break;
		/* cs */
		case 5: cout ("C"); break;
		/* ne */
		case 6: cout ("nZ"); break;
		/* eq */
		case 7: cout ("(!nZ)"); break;
		/* vc */
		case 8: cout ("(!V)"); break;
		/* vs */
		case 9: cout ("V"); break;
		/* pl */
		case 10: cout ("(!N)"); break;
		/* mi */
		case 11: cout ("N"); break;
		/* ge */
		case 12: cout ("(!(N ^ V))"); break;
		/* lt */
		case 13: cout ("(!(N ^ (!V)))"); break;
		/* gt */
		case 14: cout ("(nZ && (!(N ^ V)))"); break;
		/* le */
		case 15: cout ("((!nZ) || (!(N ^ (!V))))"); break;
	}
}

static void make_funcname (char *buf, int len)
{
	snprintf (buf, len, "__F%x", get_bitpos ());
}

static void c_fnbegin ()
{
	add_fixup (0, C_FUNC, pending_func_name);
	SWAP_COUT;
	num_funcs++;
	cout ("void %s ()\n", pending_func_name);
	cout ("{\n");
}

static void c_fnend ()
{
	cout ("}\n");
	SWAP_COUT;
}

#define c_fncall()	\
	{if (gen_mode == GEN_CALL) {		\
		make_funcname (pending_func_name, sizeof (pending_func_name));	\
		cln ("%s ();", pending_func_name);	\
		return;				\
	}}

/* Check for pending exceptions also */
void c_jump_e (ea_t *ea)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	c_ea_get_address (ea, buf);
	cln ("{");
	cln ("if ((exceptions_pending) && (rdest == 0)) {");
	
	if (((ea->mode == 7) && (ea->reg == 1)) ||
	    ((ea->mode == 7) && (ea->reg == 2))) {
		cln ("	rdest = __D%s;", ea->imm.label);
	} else {
		cln ("	rdest = %s;", buf);
	}
	c_postea (ea);
	cln ("	goto handle_exception;", buf);
	cln ("} else {");

	if (((ea->mode == 7) && (ea->reg == 1)) ||
	    ((ea->mode == 7) && (ea->reg == 2))) {
		cln ("	goto __N%s;", ea->imm.label);
	} else {
		cln ("	jdest = %s;", buf);
		c_postea (ea);
		cln ("	goto jumptable;");
	}
	cln ("}}");
}

void c_jump (ea_t *ea)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	c_ea_get_address (ea, buf);
	
	cln ("{");
	if (((ea->mode == 7) && (ea->reg == 1)) ||
	    ((ea->mode == 7) && (ea->reg == 2))) {
		cln ("	goto __N%s;", ea->imm.label);
	} else {
		cln ("	jdest = %s;", buf);
		c_postea (ea);
		cln ("	goto jumptable;");
	}
	cln ("}");
}

void c_func_bchg (ea_t *src, ea_t *dest, int size)
{
	int len;
	const char *sz;
	char buf[256];
	c_fncall ();
	
	c_fnbegin ();
	sz = c_usizes[size];
	if (size == BYTE) len = 8;
	else if (size == WORD) len = 16;
	else len = 32;

	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);
	c_readea (dest, buf);
	cln ("%s dest = %s;", sz, buf);
	cln ("src &= %d;", len-1);
	cln ("dest ^= (1 << src);");
	ifZ { cln ("nZ = !(((%s)dest & (1 << src)) >> src);", sz); }
	c_writeea (dest, size, "dest");
	c_postea (src);
	c_postea (dest);
	c_fnend ();
}

void c_func_bclr (ea_t *src, ea_t *dest, int size)
{
	int len;
	const char *sz;
	char buf[256];

	c_fncall ();
	
	c_fnbegin ();
	sz = c_usizes[size];
	if (size == BYTE) len = 8;
	else if (size == WORD) len = 16;
	else len = 32;

	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);
	c_readea (dest, buf);
	cln ("%s dest = %s;", sz, buf);
	cln ("src &= %d;", len-1);
	ifZ { cln ("nZ = !(1 ^ ((dest >> src) & 1));"); }
	cln ("dest &= ~(1 << src);");
	c_writeea (dest, size, "dest");
	c_postea (src);
	c_postea (dest);
	c_fnend ();
}

void c_func_bset (ea_t *src, ea_t *dest, int size)
{
	int len;
	const char *sz;
	char buf[256];

	c_fncall ();
	
	c_fnbegin ();
	sz = c_usizes[size];
	if (size == BYTE) len = 8;
	else if (size == WORD) len = 16;
	else len = 32;

	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);
	c_readea (dest, buf);
	cln ("%s dest = %s;", sz, buf);
	cln ("src &= %d;", len-1);
	ifZ { cln ("nZ = !(1 ^ ((dest >> src) & 1));"); }
	cln ("dest |= (1 << src);");
	c_writeea (dest, size, "dest");
	c_postea (src);
	c_postea (dest);
	c_fnend ();
}

void c_func_btst (ea_t *src, ea_t *dest, int size)
{
	int len;
	const char *sz;
	char buf[256];

	c_fncall ();
	
	c_fnbegin ();
	sz = c_usizes[size];
	if (size == BYTE) len = 8;
	else if (size == WORD) len = 16;
	else len = 32;

	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);
	c_readea (dest, buf);
	cln ("%s dest = %s;", sz, buf);
	cln ("src &= %d;", len-1);
	ifZ { cln ("nZ = !(1 ^ ((dest >> src) & 1));"); }
	c_postea (src);
	c_postea (dest);
	c_fnend ();
}

void c_func_muls (ea_t *src, ea_t *dest)
{
	char buf[256];
	c_fncall ();
	
	c_fnbegin ();
	c_readea (src, buf);
	cln ("s16 src = %s;", buf);
	c_readea (dest, buf);
	cln ("s32 dest = (s16)%s;", buf);
	cln ("dest *= (s32)src;");
	ifZ { cln ("nZ = dest;"); }
	ifN { cln ("N = ((s32)dest) < 0;"); }
	ifC { cln ("C = 0;"); }
	ifV { cln ("V = 0;"); }
	c_writeea (dest, LONG, "dest");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}
void c_func_mulu (ea_t *src, ea_t *dest)
{
	char buf[256];
	c_fncall ();
	
	c_fnbegin ();
	c_readea (src, buf);
	cln ("u16 src = %s;", buf);
	c_readea (dest, buf);
	cln ("u32 dest = (u16)%s;", buf);
	cln ("dest *= (u32)src;");
	ifZ { cln ("nZ = dest;"); }
	ifN { cln ("N = ((s32)dest) < 0;"); }
	ifC { cln ("C = 0;"); }
	ifV { cln ("V = 0;"); }
	c_writeea (dest, LONG, "dest");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_logop (ea_t *src, ea_t *dest, int size, char op)
{
	const char *sz;
	char buf[256];
	sz = c_ssizes[size];
	
	c_fncall ();
	
	c_fnbegin ();
	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);
	c_readea (dest, buf);
	cln ("%s dest = %s;", sz, buf);
	cln ("dest %c= src;", op);
	ifC { cln ("C = 0;"); }
	ifV { cln ("V = 0;"); }
	ifZ { cln ("nZ = dest;"); }
	ifN { cln ("N = (dest < 0);"); }
	c_writeea (dest, size, "dest");
	c_postea (src);
	c_postea (dest);
	c_fnend ();
}

void c_func_not (ea_t *ea, int size)
{
	char buf[256];
	c_fncall ();
	
	c_fnbegin ();
	c_readea (ea, buf);
	cln ("%s val = %s;", c_usizes[size], buf);
	cln ("val = ~val;");
	ifN { cln ("N = ((%s)val) < 0;", c_ssizes[size]); }
	ifZ { cln ("nZ = val;"); }
	ifV { cln ("V = 0;"); }
	ifC { cln ("C = 0;"); }
	c_writeea (ea, size, "val");
	c_postea (ea);
	c_fnend ();
}

void c_func_negx (ea_t *ea, int size)
{
	char buf[256];
	c_fncall ();

	c_fnbegin ();
	c_readea (ea, buf);
/* XXX is nZ flag correct here? */
	cln ("%s src = %s;", c_ssizes[size], buf);
	cln ("u32 dest = (0 - src) - (X ? 1 : 0);");
	cln ("int flgs, flgn;");
	cln ("flgs = ((%s)src) < 0;", c_ssizes[size]);
	cln ("flgn = ((%s)dest) < 0;", c_ssizes[size]);
	ifZ { cln ("nZ = !((!nZ) & (((%s)(dest)) == 0));", c_ssizes[size]); }
	ifV { cln ("V = (flgs & flgn);"); }
	cln ("X = flgs ^ ((flgs ^ flgn) & (flgn));");
	ifC { cln ("C = X;"); }
	ifN { cln ("N = ((%s)dest) < 0;", c_ssizes[size]); }
	c_writeea (ea, size, "dest");
	c_postea (ea);
	c_fnend ();
}
void c_func_neg (ea_t *ea, int size)
{
	char buf[256];
	c_fncall ();

	c_fnbegin ();
	c_readea (ea, buf);
	cln ("%s src = %s;", c_ssizes[size], buf);
	cln ("%s dest = ((%s)0) - ((%s)src);", c_usizes[size], c_ssizes[size], c_ssizes[size]);
	cln ("int flgs, flgn;");
	cln ("flgs = ((%s)src) < 0;", c_ssizes[size]);
	cln ("flgn = ((%s)dest) < 0;", c_ssizes[size]);
	ifZ { cln ("nZ = dest;"); }
	ifV { cln ("V = (flgs & flgn);"); }
	cln ("X = (dest != 0);");
	ifC { cln ("C = X;"); }
	ifN { cln ("N = (flgn != 0);"); }
	c_writeea (ea, size, "dest");
	c_postea (ea);
	c_fnend ();
}

void c_func_divu (ea_t *ea, int reg)
{
	char buf[256];
	c_fncall ();
	
	c_fnbegin ();
	c_readea (ea, buf);
	cln ("s16 src = %s;\n", buf);
	cln ("s32 dest = Regs[%d]._s32;", reg);
	cln ("if (src == 0) {");
	ifV { cln ("	V = 0;"); }
	cln ("	assert (src != 0);");
	cln ("} else {");
	cln ("	u32 newv = (u32)dest / (u32)(u16)src;");
	cln ("	u32 rem = (u32)dest %% (u32)(u16)src;");
	cln ("	if (newv > 0xffff) {");
	ifV { cln ("		V = 1;"); }
	ifN { cln ("		N = 1;"); }
	ifC { cln ("		C = 0;"); }
	cln ("	} else {");
	ifC { cln ("		C = 0;"); }
	ifV { cln ("		V = 0;"); }
	ifZ { cln ("		nZ = newv;"); }
	ifN { cln ("		N = (newv < 0);"); }
	cln ("		Regs[%d]._u32 = (newv & 0xffff) | ((u32)rem << 16);", reg);
	cln ("}}");
	c_postea (ea);
	c_fnend ();
}

void c_func_divs (ea_t *ea, int reg)
{
	char buf[256];
	c_fncall ();
	
	c_fnbegin ();
	c_readea (ea, buf);
	cln ("s16 src = %s;\n", buf);
	cln ("s32 dest = Regs[%d]._s32;", reg);
	cln ("if (src == 0) {");
	ifV { cln ("	V = 0;"); }
	cln ("	assert (src != 0);");
	cln ("} else {");
	cln ("	s32 newv = (s32)dest / (s32)(s16)src;");
	cln ("	u16 rem = (s32)dest %% (s32)(s16)src;");
	cln ("	if ((newv & 0xffff8000) != 0 && (newv & 0xffff8000) != 0xffff8000) {");
	ifV { cln ("		V = 1;"); }
	ifN { cln ("		N = 1;"); }
	ifC { cln ("		C = 0;"); }
	cln ("	} else {");
	ifC { cln ("		C = 0;"); }
	ifV { cln ("		V = 0;"); }
	ifZ { cln ("		nZ = newv;"); }
	ifN { cln ("		N = (newv < 0);"); }
	cln ("		Regs[%d]._u32 = (newv & 0xffff) | ((u32)rem << 16);", reg);
	cln ("}}");
	c_postea (ea);
	c_fnend ();
}

/* src is the shift */
void c_func_asl (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int shift, len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = %s;\n", buf);

	if (is_immediate (src)) {
		/* mostly for the sake of -O0 compiles... */
		shift = src->imm.val;
		
		if (shift >= len) {
			ifV cln ("V = (val != 0);");
			cln ("X = %s;", (shift == len ? "val & 1" : "0"));
			ifC cln ("C = X;");
			cln ("val = 0;");
		} else {
			ifV cln ("u32 mask = (0x%x << (%d - %d)) & 0x%x;", mask, len-1, shift, mask);
			ifV cln ("V = (val & mask) != mask && (val & mask) != 0;");
			cln ("val <<= %d;", shift-1);
			cln ("X = (val & 0x%x);", 1<<(len-1));
			ifC cln ("C = (val & 0x%x);", 1<<(len-1));
			cln ("val <<= 1;");
		}
	} else {
		c_readea (src, buf);
		cln ("s32 src = %s & 63;", buf);
		cln ("if (src >= %d) {", len);
		ifV cln ("	V = (val != 0);");
		cln ("	X = (src == %d ? val & 1 : 0);", len);
		ifC cln ("C = X;");
		cln ("	val = 0;");
		cln ("} else if (src == 0) {");
		ifC cln ("	C = 0;");
		ifV cln ("	V = 0;");
		cln ("} else {");
		ifV cln ("	u32 mask = (0x%x << (%d - src)) & 0x%x;", mask, len-1, mask);
		ifV cln ("	V = (val & mask) != mask && (val & mask) != 0;");
		cln ("	val <<= src-1;");
		cln ("	X = (val & 0x%x);", 1<<(len-1));
		ifC cln ("	C = (val & 0x%x);", 1<<(len-1));
		cln ("	val <<= 1;");
		//cln ("	val &= 0x%x;", mask);
		cln ("}");
	}
	ifZ cln ("nZ = ((%s)(val)) != 0;", c_ssizes[size]);
	ifN cln ("N = ((%s)(val)) < 0;", c_ssizes[size]);
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_lsl (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int shift, len;
	
	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);

	if (is_immediate (src)) {
		shift = src->imm.val;
		
		if (shift >= len) {
			cln ("X = %s;", (shift == len ? "val & 1" : "0"));
			ifC cln ("C = X;");
			cln ("val = 0;");
		} else {
			cln ("val <<= %d;", shift-1);
			cln ("X = (val & 0x%x);", 1<<(len-1));
			ifC cln ("C = X;");
			cln ("val <<= 1;");
			cln ("val &= 0x%x;", mask);
		}
	} else {
		c_readea (src, buf);
		cln ("s32 shift = %s & 63;", buf);
		cln ("if (shift >= %d) {", len);
		cln ("	X = (shift == %d ? val & 1 : 0);", len);
		ifC cln ("	C = X;");
		cln ("	val = 0;");
		cln ("} else if (shift == 0) {");
		ifC cln ("	C = 0;");
		cln ("} else {");
		cln ("	val <<= shift-1;");
		cln ("	X = (val & 0x%x);", 1<<(len-1));
		ifC cln ("	C = X;");
		cln ("	val <<= 1;");
		cln ("	val &= 0x%x;", mask);
		cln ("}");
	}

	ifZ cln ("nZ = ((%s)(val)) != 0;", c_ssizes[size]);
	ifN cln ("N = ((%s)(val)) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_roxr (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 carry;");
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);

	c_readea (src, buf);
	cln ("s32 cnt = (%s & 63) - 1;", buf);

	cln ("u32 hival = (val << 1) | (X ? 1 : 0);");
	cln ("hival <<= (%d - cnt);", len-1);
	cln ("val >>= cnt;");
	cln ("carry = val & 1;");
	cln ("val >>= 1;");
	cln ("val |= hival;");
	cln ("X = carry;");
	cln ("val &= 0x%x;", mask);
	ifC cln ("C = X;");
	ifZ cln ("nZ = val;");
	ifN cln ("N = ((%s)val) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}
void c_func_roxl (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);

	c_readea (src, buf);
	cln ("s32 cnt = (%s & 63) - 1;", buf);

	cln ("u32 loval = val >> (%d - cnt);", len-1);
	cln ("u32 carry = loval & 1;");
	cln ("val = (((val << 1) | (X ? 1 : 0)) << cnt) | (loval >> 1);");
	cln ("X = carry;");
	cln ("val &= 0x%x;", mask);
	ifC cln ("C = X;");
	ifZ cln ("nZ = val;");
	ifN cln ("N = ((%s)val) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}
void c_func_rol (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);

	c_readea (src, buf);
	cln ("s32 cnt = %s & 63;", buf);

	cln ("u32 loval;");
	cln ("cnt &= %d;", len-1);
	cln ("loval = val >> (%d - cnt);", len);
	cln ("val <<= cnt;");
	cln ("val |= loval;");
	cln ("val &= 0x%x;", mask);
	ifC cln ("C = val & 1;");
	ifZ cln ("nZ = val;");
	ifN cln ("N = ((%s)val) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_ror (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);

	c_readea (src, buf);
	cln ("s32 cnt = %s & 63;", buf);

	cln ("u32 hival;");
	cln ("cnt &= %d;", len-1);
	cln ("hival = val << (%d - cnt);", len);
	cln ("val >>= cnt;");
	cln ("val |= hival;");
	cln ("val &= 0x%x;", mask);
	ifC cln ("C = (val & 0x%x) >> %d;", 1<<(len-1), len-1);
	ifZ cln ("nZ = val;");
	ifN cln ("N = ((%s)val) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_asr (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int shift, len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);
	cln ("u32 sign = (0x%x & val) >> %d;", 1<<(len-1), len-1);

	if (is_immediate (src)) {
		shift = src->imm.val;

		if (shift >= len) {
			cln ("val = 0x%x & (u32)-sign;", mask);
			cln ("X = sign;");
			ifC cln ("C = X;");
		} else {
			cln ("val >>= %d;", shift-1);
			cln ("X = val & 1;");
			ifC cln ("C = X;");
			cln ("val >>= 1;");
			cln ("val |= (0x%x << %d) & (u32)-sign;", mask, len - shift);
		}
	} else {
		c_readea (src, buf);
		cln ("s32 src = %s & 63;", buf);
	
		cln ("if (src >= %d) {", len);
		cln ("	val = 0x%x & (u32)-sign;", mask);
		cln ("	X = sign;");
		ifC cln ("	C = X;");
		cln ("} else if (src == 0) {");
		ifC cln ("	C = 0;");
		cln ("} else {");
		cln ("	val >>= src - 1;");
		cln ("	X = val & 1;");
		ifC cln ("	C = X;");
		cln ("	val >>= 1;");
		cln ("	val |= (0x%x << (%d - src)) & (u32)-sign;", mask, len);
		//cln ("	val &= 0x%x;", mask);
		cln ("}");
	}
	ifZ cln ("nZ = ((%s)(val)) != 0;", c_ssizes[size]);
	ifN cln ("N = ((%s)(val)) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}
void c_func_lsr (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	unsigned int mask;
	int shift, len;

	c_fncall ();

	c_fnbegin ();
	if (size == BYTE) {
		mask = 0xff;
		len = 8;
	} else if (size == WORD) {
		mask = 0xffff;
		len = 16;
	} else {
		mask = 0xffffffff;
		len = 32;
	}

	c_readea (dest, buf);
	cln ("u32 val = (%s)%s;\n", c_usizes[size], buf);

	if (is_immediate (src)) {
		shift = src->imm.val;

		if (shift >= len) {
			cln ("val = 0;");
			cln ("X = %d && (val >> %d);", (shift == len), len-1);
			ifC cln ("C = X;");
		} else {
			cln ("val >>= %d;", shift-1);
			cln ("X = val & 1;");
			ifC cln ("C = X;");
			cln ("val >>= 1;");
		}
	} else {
		c_readea (src, buf);
		cln ("u32 shift = %s & 63;", buf);

		cln ("if (shift >= %d) {", len);
		cln ("	val = 0;");
		cln ("	X = (shift == %d) && (val >> %d);", len, len-1);
		ifC cln ("	C = X;");
		cln ("} else if (shift == 0) {");
		ifC cln ("	C = 0;");
		cln ("} else {");
		cln ("	val >>= shift - 1;");
		cln ("	X = val & 1;");
		ifC cln ("	C = X;");
		cln ("	val >>= 1;");
		cln ("}");
	}

	ifZ cln ("nZ = ((%s)(val)) != 0;", c_ssizes[size]);
	ifN cln ("N = ((%s)(val)) < 0;", c_ssizes[size]);
	ifV cln ("V = 0;");
	
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_movem (ea_t *ea, int sz, int dr, int reg_mask)
{
	int i, offset;
	char buf[256];
	const char *read_func;
	const char *write_func;
	
	c_fncall ();
	
	c_fnbegin ();
	
	sz++;
	read_func = mem_read_funcs[sz];
	write_func = mem_write_funcs[sz];

	c_ea_get_address (ea, buf);

	if (dr == 0) {
		/* reg to memory */
		cln ("s32 dest = %s;", buf);

		if (ea->mode == 4) {
			/* pre-decrement mode */
			offset = 0;
			for (i=0; i<16; i++) {
				if (reg_mask & (1<<i)) {
					cln ("%s (dest, Regs[%d]._%s);", write_func, 15-i, c_usizes[sz]);
					cln ("dest -= %d;", 1<<sz);
					offset += 1<<sz;
				}
			}
			cln ("Regs[%d]._s32 -= %d;", ea->reg, offset);
		} else {
			for (i=0; i<16; i++) {
				if (reg_mask & (1<<i)) {
					cln ("%s (dest, Regs[%d]._%s);", write_func, i, c_usizes[sz]);
					cln ("dest += %d;", 1<<sz);
				}
			}
		}
	} else {
		/* mem to reg */
		cln ("s32 src = %s;", buf);

		offset = 0;
		for (i=0; i<16; i++) {
			if (reg_mask & (1<<i)) {
				cln ("Regs[%d]._s32 = %s (src);", i, read_func);
				cln ("src += %d;", 1<<sz);
				offset += 1<<sz;
			}
		}
		/* post-inc mode */
		if (ea->mode == 3)
			cln ("Regs[%d]._s32 += %d;", ea->reg, offset);
	}
	c_fnend ();
}

void c_func_subx (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	const char *sz = c_ssizes[size];
	c_fncall ();
	
	c_fnbegin ();
/* XXX nZ correct? */
	c_readea (dest, buf);
	cln ("int flgs, flgo, flgn;");
	cln ("%s dest = %s;", sz, buf);

	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);

	cln ("s32 newv = dest - src - (X ? 1 : 0);");

	cln ("flgs = src < 0;", sz);
	cln ("flgo = dest < 0;", sz);
	cln ("flgn = ((%s)newv) < 0;", sz);
	ifV cln ("V = (flgs ^ flgo) & (flgo ^ flgn);");
	cln ("X = (flgs ^ ((flgs ^ flgn) & (flgo ^ flgn)));");
	ifC cln ("C = X;");
	ifZ cln ("nZ = !((!nZ) & (((%s)(newv)) == 0));", sz);
	ifN cln ("N = ((%s)newv) < 0;", sz);
	c_writeea (dest, size, "newv");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_addx (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	const char *sz = c_ssizes[size];
	c_fncall ();
	
	c_fnbegin ();
/* XXX nZ correct? */
	c_readea (dest, buf);
	cln ("int flgs, flgo, flgn;");
	cln ("%s dest = %s;", sz, buf);

	c_readea (src, buf);
	cln ("%s src = %s;", sz, buf);

	cln ("s32 newv = dest + src + (X ? 1 : 0);");

	cln ("flgs = src < 0;", sz);
	cln ("flgo = dest < 0;", sz);
	cln ("flgn = ((%s)newv) < 0;", sz);
	ifV cln ("V = (flgs ^ flgn) & (flgo ^ flgn);");
	cln ("X = (flgs ^ ((flgs ^ flgo) & (flgo ^ flgn)));");
	ifC cln ("C = X;");
	ifZ cln ("nZ = !((!nZ) & (((%s)(newv)) == 0));", sz);
	ifN cln ("N = ((%s)newv) < 0;", sz);
	c_writeea (dest, size, "newv");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_add (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	const char *sz = c_ssizes[size];
	const char *usz = c_usizes[size];

	c_fncall ();
	
	c_fnbegin ();
	c_readea (dest, buf);
	cln ("s32 dest = %s;\n", buf);

	c_readea (src, buf);
	cln ("s32 src = %s;\n", buf);

	cln ("s32 val = (%s)dest + (%s)src;", sz, sz);

	ifV cln ("s32 flgs = ((%s)src) < 0;", sz);
	ifV cln ("s32 flgo = ((%s)dest) < 0;", sz);
	ifVoN cln ("s32 flgn = ((%s)val) < 0;", sz);
	ifZ cln ("nZ = val;");
	ifV cln ("V = (flgs ^ flgn) & (flgo ^ flgn);");
	cln ("X = ((%s)(~src)) < ((%s)dest);", usz, usz);
	ifC cln ("C = X;");
	ifN cln ("N = (flgn != 0);");
	c_writeea (dest, size, "val");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_cmpa (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	const char *sz = c_ssizes[size];
	c_fncall ();
	
	c_fnbegin ();

	cln ("s32 dest = Regs[%d]._s32;\n", dest->reg);

	c_readea (src, buf);
	cln ("%s src = %s;\n", sz, buf);

	cln ("s32 val = (s32)dest - (s32)src;");
	ifV cln ("s32 flgs = ((s32)src) < 0;");
	ifV cln ("s32 flgo = ((s32)dest) < 0;");
	ifVoN cln ("s32 flgn = ((s32)val) < 0;");
	ifZ cln ("nZ = val;");
	ifV cln ("V = ((flgs != flgo) && (flgn != flgo));");
	ifC cln ("C = ((u32)src) > ((u32)dest);");
	ifN cln ("N = (flgn != 0);");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_cmp (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	const char *sz = c_ssizes[size];
	const char *usz = c_usizes[size];
	c_fncall ();
	
	c_fnbegin ();

	c_readea (dest, buf);
	cln ("s32 dest = %s;\n", buf);

	c_readea (src, buf);
	cln ("s32 src = %s;\n", buf);

	cln ("s32 val = (%s)dest - (%s)src;", sz, sz);

	ifV cln ("s32 flgs = ((%s)src) < 0;", sz);
	ifV cln ("s32 flgo = ((%s)dest) < 0;", sz);
	ifVoN cln ("s32 flgn = ((%s)val) < 0;", sz);
	ifV cln ("V = ((flgs != flgo) && (flgn != flgo));");
	ifC cln ("C = ((%s)src) > ((%s)dest);", usz, usz);
	ifN cln ("N = (flgn != 0);");
	ifZ cln ("nZ = val;");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_sub (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	const char *sz = c_ssizes[size];
	const char *usz = c_usizes[size];

	c_fncall ();
	
	c_fnbegin ();
	c_readea (dest, buf);
	cln ("s32 dest = %s;\n", buf);

	c_readea (src, buf);
	cln ("s32 src = %s;\n", buf);

	cln ("u32 newv = (%s)dest - (%s)src;", sz, sz);

	ifV cln ("s32 flgs = ((%s)src) < 0;", sz);
	ifV cln ("s32 flgo = ((%s)dest) < 0;", sz);
	ifVoN cln ("s32 flgn = ((%s)newv) < 0;", sz);
	ifZ cln ("nZ = newv;");
	ifV cln ("V = (flgs ^ flgo) & (flgn ^ flgo);");
	cln ("X = ((%s)src) > ((%s)dest);", usz, usz);
	ifC cln ("C = X;");
	ifN cln ("N = (flgn != 0);");
	c_writeea (dest, size, "newv");
	c_postea (dest);
	c_postea (src);
	c_fnend ();
}

void c_func_adda (ea_t *src, int dest_reg, int size)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	c_readea (src, buf);
	cln ("Regs[%d]._u32 = Regs[%d]._s32 + (%s)%s;", dest_reg, dest_reg, c_ssizes[size], buf);
	c_postea (src);
}

void c_func_suba (ea_t *src, int dest_reg, int size)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	c_readea (src, buf);
	cln ("Regs[%d]._u32 = Regs[%d]._s32 - (%s)%s;", dest_reg, dest_reg, c_ssizes[size], buf);
	c_postea (src);
}

void c_func_swap (int reg)
{
	c_fncall ();

	c_fnbegin ();
	cln ("s32 temp = Regs[%d].word[0];", reg);
	cln ("Regs[%d].word[0] = Regs[%d].word[1];", reg, reg);
	cln ("Regs[%d].word[1] = temp;", reg);
	ifZ cln ("nZ = Regs[%d]._s32;", reg);
	ifN cln ("N = (Regs[%d]._s32 < 0);", reg);
	ifV cln ("V = 0;");
	ifC cln ("C = 0;");
	c_fnend ();
}

void c_func_jsr (ea_t *dest)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	snprintf (buf, sizeof (buf), "%x", get_bitpos ());
	add_label (buf, C_ADDR, get_bitpos () - BASE);
	add_fixup (0, C_ADDR, buf);

	snprintf (buf, sizeof (buf), "__D%x", get_bitpos ());
	cln ("Regs[15]._s32 -= 4;");
	cln ("wrlong (Regs[15]._s32, %s);", buf);
				
	c_jump (dest);
}

void c_func_bcc (ea_t *dest, int cond)
{
	if (gen_mode != GEN_CALL) return;
	if (cond == 0) {
		c_jump (dest);
	} else {
		cout ("\tif (");
		c_eval_cond (cond);
		cout (")\n");
		c_jump_e (dest);
	}
}

/* XXX note: this does not call c_jump and therefore does not
 * check for pending exceptions. an infinite DBcc loop will not
 * allow exceptions to occur.
 * does it matter? */
void c_func_dbcc (const char *label, int cond, int reg)
{
	if (gen_mode != GEN_CALL) return;
	if (cond == 0) {
		cln ("if (--Regs[%d]._s16 != -1) goto __N%s;\n", reg, label);
	} else {
		cout ("\tif ((!");
		c_eval_cond (cond);
		cout (") && (--Regs[%d]._s16 != -1)) goto __N%s;\n", reg, label);
	}
}

void c_func_clr (ea_t *dest)
{
	c_fncall ();
	
	c_fnbegin ();
	c_writeea (dest, dest->op_size, "0");
	c_postea (dest);
	ifN cln ("N = 0;");
	ifZ cln ("nZ = 0;");
	ifV cln ("V = 0;");
	ifC cln ("C = 0;");
	c_fnend ();
}

void c_func_exg (int reg1, int reg2)
{
	c_fncall ();

	c_fnbegin ();
	cln ("u32 temp = Regs[%d]._u32;", reg1);
	cln ("Regs[%d]._u32 = Regs[%d]._u32;", reg1, reg2);
	cln ("Regs[%d]._u32 = temp;", reg2);
	c_fnend ();
}

void c_func_ext (int reg, int size)
{
	c_fncall ();

	c_fnbegin ();
	cln ("%s val = Regs[%d]._%s;", c_ssizes[size], reg, c_ssizes[size-1]);
	cln ("Regs[%d]._%s = val;", reg, c_ssizes[size]);
	ifZ cln ("nZ = val;");
	ifN cln ("N = (val < 0);");
	ifV cln ("V = 0;");
	ifC cln ("C = 0;");
	c_fnend ();
}

void c_func_rte ()
{
	if (gen_mode != GEN_CALL) return;
	cln ("N=bN; nZ=bnZ; V=bV; C=bC; X=bX;");
	cln ("jdest = rdest;");
	cln ("rdest = 0;");
	cln ("goto jumptable;");
}

void c_func_rts ()
{
	if (gen_mode != GEN_CALL) return;
	cln ("jdest = rdlong (Regs[15]._u32);");
	cln ("Regs[15]._s32 += 4;");
	cln ("goto jumptable;");
}

void c_func_illegal ()
{
	if (gen_mode != GEN_CALL) return;
	cln ("printf (\"Illegal instruction at line %d.\\n\");", line_no);
	cln ("abort ();");
}

void c_func_hcall (int val)
{
	if (gen_mode != GEN_CALL) return;
	cln ("(* hcalls[%d]) ();", val);
}

void c_func_link (int reg, int val)
{
	c_fncall ();

	c_fnbegin ();
	cln ("Regs[15]._u32 -= 4;");
	cln ("wrlong (Regs[15]._u32, Regs[%d]._u32);", reg);
	cln ("Regs[%d]._u32 = Regs[15]._u32;", reg);
	cln ("Regs[15]._u32 += 0x%x;", val);
	c_fnend ();
}

void c_func_unlk (int reg)
{
	c_fncall ();

	c_fnbegin ();
	cln ("Regs[15]._u32 = Regs[%d]._u32;", reg);
	cln ("Regs[%d]._u32 = rdlong (Regs[15]._u32);", reg);
	cln ("Regs[15]._u32 += 4;");
	c_fnend ();
}

void c_func_move (ea_t *src, ea_t *dest, int size)
{
	char buf[256];
	c_fncall ();

	c_fnbegin ();
	c_readea (src, buf);
	cln ("s32 val = %s;", buf);
	c_writeea (dest, size, "val");
	c_postea (src);
	c_postea (dest);
	ifZ cln ("nZ = val;");
	ifN cln ("N = val < 0;");
	ifV cln ("V = 0;");
	ifC cln ("C = 0;"); 
	c_fnend ();
}

void c_func_movea (ea_t *src, int reg_dest)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	assert (reg_dest > 7);
	c_readea (src, buf);
	cln ("Regs[%d]._s32 = %s;", reg_dest, buf);
	c_postea (src);
}

void c_func_tst (ea_t *ea)
{
	char buf[256];
	c_fncall ();

	c_fnbegin ();
	c_readea (ea, buf);
	cln ("nZ = %s;", buf);
	c_postea (ea);
	ifN cln ("N = nZ < 0;");
	ifV cln ("V = 0;");
	ifC cln ("C = 0;");
	c_fnend ();
}

void c_func_pea (ea_t *ea)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	c_ea_get_address (ea, buf);

	cln ("Regs[15]._u32 -= 4;");
	cln ("wrlong (Regs[15]._u32, %s);", buf);
}

void c_func_lea (ea_t *ea, int reg)
{
	char buf[256];
	if (gen_mode != GEN_CALL) return;
	c_ea_get_address (ea, buf);

	cln ("Regs[%d]._u32 = %s;", reg, buf);
}

void c_func_scc (ea_t *dest, int cond)
{
	c_fncall ();

	c_fnbegin ();
	cout ("\tif (");
	c_eval_cond (cond);
	cout (") {\n");
	c_writeea (dest, BYTE, "0xff");
	cln ("} else {");
	c_writeea (dest, BYTE, "0");
	cln ("}");
	c_postea (dest);
	c_fnend ();
}

static void output_op (op_t *op, op_t *next)
{
	switch (op->optype) {
		case OP_MULS: c_func_muls (&op->src, &op->dest); break;
		case OP_MULU: c_func_mulu (&op->src, &op->dest); break;
		case OP_ADDA: c_func_adda (&op->src, op->dest.reg, op->size); break;
		case OP_SUBA: c_func_suba (&op->src, op->dest.reg, op->size); break;
		case OP_ADD: c_func_add (&op->src, &op->dest, op->size); break;
		case OP_SUB: c_func_sub (&op->src, &op->dest, op->size); break;
		case OP_NOT: c_func_not (&op->src, op->size); break;
		case OP_NEGX: c_func_negx (&op->src, op->size); break;
		case OP_NEG: c_func_neg (&op->src, op->size); break;
		case OP_DIVS: c_func_divs (&op->src, op->dest.reg); break;
		case OP_DIVU: c_func_divu (&op->src, op->dest.reg); break;
		case OP_ADDX: c_func_addx (&op->src, &op->dest, op->size); break;
		case OP_SUBX: c_func_subx (&op->src, &op->dest, op->size); break;
		case OP_BCHG: c_func_bchg (&op->src, &op->dest, op->size); break;
		case OP_BCLR: c_func_bclr (&op->src, &op->dest, op->size); break;
		case OP_BSET: c_func_bset (&op->src, &op->dest, op->size); break;
		case OP_BTST: c_func_btst (&op->src, &op->dest, op->size); break;
		case OP_ASL: c_func_asl (&op->src, &op->dest, op->size); break;
		case OP_ASR: c_func_asr (&op->src, &op->dest, op->size); break;
		case OP_LSL: c_func_lsl (&op->src, &op->dest, op->size); break;
		case OP_LSR: c_func_lsr (&op->src, &op->dest, op->size); break;
		case OP_ROL: c_func_rol (&op->src, &op->dest, op->size); break;
		case OP_ROR: c_func_ror (&op->src, &op->dest, op->size); break;
		case OP_ROXL: c_func_roxl (&op->src, &op->dest, op->size); break;
		case OP_ROXR: c_func_roxr (&op->src, &op->dest, op->size); break;
		case OP_OR: c_func_logop (&op->src, &op->dest, op->size, '|'); break;
		case OP_AND: c_func_logop (&op->src, &op->dest, op->size, '&'); break;
		case OP_XOR: c_func_logop (&op->src, &op->dest, op->size, '^'); break;
		case OP_SWAP: c_func_swap (op->src.reg); break;
		case OP_JSR: c_func_jsr (&op->src); break;
		case OP_BCC: c_func_bcc (&op->src, op->opcode.Bcc.cond); break;
		case OP_DBCC: c_func_dbcc (op->dest.imm.label, op->opcode.Bcc.cond, op->src.reg); break;
		case OP_CLR: c_func_clr (&op->src); break;
		case OP_CMP: c_func_cmp (&op->src, &op->dest, op->size); break;
		case OP_CMPA: c_func_cmpa (&op->src, &op->dest, op->size); break;
		case OP_EXG: c_func_exg (op->src.reg, op->dest.reg); break;
		case OP_EXT: c_func_ext (op->src.reg, op->size); break;
		case OP_MOVEM: c_func_movem (&op->src, op->opcode.movem.sz, op->opcode.movem.dr, op->dest.reg); break;
		case OP_RTE: c_func_rte (); break;
		case OP_RTS: c_func_rts (); break;
		case OP_ILLEGAL: c_func_illegal (); break;
		case OP_HCALL: c_func_hcall (op->src.imm.val); break;
		case OP_LINK: c_func_link (op->src.reg, op->dest.imm.val); break;
		case OP_UNLK: c_func_unlk (op->src.reg); break;
		case OP_MOVE: c_func_move (&op->src, &op->dest, op->size); break;
		case OP_MOVEA: c_func_movea (&op->src, op->dest.reg); break;
		case OP_TST: c_func_tst (&op->src); break;
		case OP_PEA: c_func_pea (&op->src); break;
		case OP_LEA: c_func_lea (&op->src, op->dest.reg); break;
		case OP_JMP: c_jump (&op->src); break;
		case OP_SCC: c_func_scc (&op->src, op->opcode.DBcc.cond); break;

		default: 
			     printf ("type %d\n", op->optype);
			     assert (0);
	}
}

void c_push_op (enum M68K_OPS type, ea_t *src, ea_t *dest, int size)
{
	c_push_op2 (type, src, dest, size, 0);
}
void c_push_op2 (enum M68K_OPS type, ea_t *src, ea_t *dest, int size, int opcode)
{
	next_op_type = type;
	
	if (pending.optype) {
		gen_mode = GEN_BODY;
		output_op (&pending, NULL);
		gen_mode = GEN_CALL;
	}
	
	pending.size = size;
	pending.opcode.code = opcode;
	pending.optype = type;
	
	if (type == OP_JSR)
		return_target = 1;
	else
		return_target = 0;
	
	if (src) {
		pending.src = *src;
	}
	if (dest) {
		pending.dest = *dest;
	}
	output_op (&pending, NULL);
}
void c_push_op_basic (enum M68K_OPS type)
{
	c_push_op2 (type, NULL, NULL, 0, 0);
}

