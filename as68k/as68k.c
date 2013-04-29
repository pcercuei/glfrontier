
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "dict.h"
#include "as68k.h"
#include "output.h"

struct Compiler *comp;

FILE *fin;
FILE *fout;

char buf[4096];
Dict labels;
int dump_labels = 0;
int line_no = 0;
int last_op_addr;
int output_c = 0;

const char *src_filename;

/* to convert weird move sizes to standard sizes */
const int move_size[4] = { 1, 3, 2, 0 };
const char *Bcc_str[16] = {
	"ra",NULL,"hi","ls","cc","cs","ne","eq",
	"vc","vs","pl","mi","ge","lt","gt","le"
};
const char *DBcc_str[16] = {
	"t","f","hi","ls","cc","cs","ne","eq",
	"vc","vs","pl","mi","ge","lt","gt","le"
};

/* enum SIZE */
const int size2len[] = { 8, 16, 32 };

#define check(ch)	do {  \
	if (*pos != (ch)) error ("Expected '%c'", ch); \
	pos++; \
	} while (0);
#define check_whitespace()	do { \
	if (!isspace (*pos)) error ("Line malformed."); \
	while (isspace (*pos)) pos++; \
	} while (0);

int get_bitpos () { return (int)ftell (fout); }
#define set_bitpos(pos)	(fseek (fout,pos,SEEK_SET))

struct Fixup *fix_first = NULL;
struct Fixup *fix_last = NULL;
void add_fixup (int adr, int size, const char *label)
{
	struct Fixup *fix = malloc (sizeof (struct Fixup));
	fix->adr = adr;
	fix->size = size;
	fix->next = NULL;
	fix->rel_to = (last_op_addr + 2) - BASE;
	fix->line_no = line_no;
	strcpy (fix->label, label);
	if (fix_first == NULL)  fix_first = fix;
	if (fix_last == NULL) fix_last = fix;
	else {
		fix_last->next = fix;
		fix_last = fix;
	}
}
void add_fixup_rel_to (int adr, int size, const char *label, int rel_to)
{
	struct Fixup *fix = malloc (sizeof (struct Fixup));
	fix->adr = adr;
	fix->size = size;
	fix->next = NULL;
	fix->rel_to = rel_to;
	fix->line_no = line_no;
	strcpy (fix->label, label);
	if (fix_first == NULL)  fix_first = fix;
	if (fix_last == NULL) fix_last = fix;
	else {
		fix_last->next = fix;
		fix_last = fix;
	}
}

void error (const char *format, ...)
{
	va_list argptr;

	fprintf (stderr, "Error at line %d: ", line_no);
	va_start (argptr, format);
	vfprintf (stderr, format, argptr);
	va_end (argptr);
	fprintf (stderr, "\n");
	
	fclose (fout);
	remove ("aout.prg");
	exit (0);
}

void bug_error (const char *format, ...)
{
	va_list argptr;

	fprintf (stderr, "Internal compiler error (line %d of input): ", line_no);
	va_start (argptr, format);
	vfprintf (stderr, format, argptr);
	va_end (argptr);
	fprintf (stderr, "\n");
	
	fclose (fout);
	remove ("aout.prg");
	abort ();
}

static void ea_set_immediate (ea_t *ea, int imm_val)
{
	ea->mode = 7;
	ea->reg = 4;
	ea->imm.has_label = 0;
	ea->imm.val = imm_val;
}
	
void add_label (const char *name, int type, int val)
{
	struct Label *lab = malloc (sizeof (struct Label));
	if (dict_get (&labels, name)) error ("Label %s redefined.", name);
	lab->val = val;
	lab->type = type;
	dict_set (&labels, name, lab);
}

struct Label *get_label (const char *name)
{
	struct Label *lab;
	struct Node *n = dict_get (&labels, name);
	
	if (n == NULL) return NULL;
	lab = n->obj;
	lab->name = n->key;
	return lab;
}

void wr_byte(unsigned char x)
{
	fputc (x, fout);
}

void wr_short(short x)
{
	wr_byte((unsigned char)((x>>8) & 0xff));
	wr_byte((unsigned char)(x & 0xff));
}

void wr_int(int x)
{
	wr_byte((unsigned char)((x>>24) & 0xff));
	wr_byte((unsigned char)((x>>16) & 0xff));
	wr_byte((unsigned char)((x>>8) & 0xff));
	wr_byte((unsigned char)(x & 0xff));
}


static inline int get_size (int ch)
{
	switch (ch) {
		case 'b': return BYTE;
		case 'w': return WORD;
		case 'l': return LONG;
		default: error ("Invalid size '.%c'.", ch); return 0;
	}
}

char *rd_label (char *buf, char *lab_buf)
{
	char snipped;
	char *pos = buf;
	
	if (!isalpha (*buf)) error ("Label expected.");
	/* label */
	do {
		pos++;
	} while (isalnum (*pos) || (*pos == '_'));
	/* snip */
	snipped = *pos;
	*pos = '\0';

	if (strlen (buf) > LAB_LEN-1) error ("Label too long.");
	strncpy (lab_buf, buf, LAB_LEN);
	lab_buf[LAB_LEN-1] = '\0';
	*pos = snipped;
	return pos;
}


void check_range (int *val, enum SIZE size)
{
	switch (size) {
		case BYTE: if ((*val < -128) || (*val > 255)) goto err;
			if (*val > 127) *val -= 256;
			break;
		case WORD: if ((*val < -32768) || (*val > 65535)) goto err;
			if (*val > 32767) *val -= 65536;
			break;
		default: break;
	}
	return;
err:
	error ("Data too large.");
}
			

char *get_imm (char *buf, struct ImmVal *imm, int flags)
{
	struct Label *lab;

	imm->has_label = 0;
	while (isspace (*buf)) buf++;
	if (flags & F_IMM) {
		if (*buf != '#') goto err;
		buf++;
	}
	if (*buf == '$') {
		/* hex value */
		buf++;
		if (sscanf (&buf[0], "%x", &imm->val) != 1) goto err;
		while (isxdigit (*buf)) buf++;
	} else if (isdigit (*buf) || (*buf == '-')) {
		/* dec value */
		if (sscanf (&buf[0], "%d", &imm->val) != 1) goto err;
		if (*buf == '-') buf++;
		while (isdigit (*buf)) buf++;
	} else if (isalpha (*buf)) {
		/* label */
		buf = rd_label (buf, imm->label);
		lab = get_label (imm->label);
		imm->val = 0;
		if (lab) {
			if (lab->type == L_CONST) imm->val = lab->val;
			else imm->has_label = 1;
		} else {
			imm->has_label = 1;
		}
		if ((flags & F_NOLABELS) && (imm->has_label)) {
			error ("Label not allowed.");
		}
	} else {
		goto err;
	}
	return buf;
err:
	error ("Malformed immediate value.");
	return NULL;
}

void check_end (const char *buf)
{
	while (isspace (*buf)) buf++;
	if (*buf) error ("Garbage at end of line.");
}

void wr_ea (ea_t *ea)
{
	if (ea->mode == 5) {
		if (ea->imm.has_label) error ("Relative not allowed.");
		wr_short (ea->imm.val);
		return;
	}
	if (ea->mode == 6) {
		if (ea->imm.has_label) error ("Relative not allowed.");
		wr_short (ea->ext.ext);
		return;
	}
	if (ea->mode == 7) {
		/* $xxx.w */
		if (ea->reg == 0) {
			wr_short (ea->imm.val);
			return;
		}
		/* $xxx.l */
		else if (ea->reg == 1) {
			if (ea->imm.has_label) goto abs_lab32;
			wr_int (ea->imm.val);
			return;
		}
		/* immediate */
		else if (ea->reg == 4) {
			if (ea->imm.has_label) {
				if (ea->op_size != LONG)
					error ("Relative not allowed.");
				goto abs_lab32;
			}
			if (ea->op_size == LONG) {
				wr_int (ea->imm.val);
			} else if (ea->op_size == WORD) {
				wr_short (ea->imm.val);
			} else {
				wr_short (ea->imm.val & 0xff);
			}
			return;
		}
		/* PC + offset */
		else if (ea->reg == 2) {
			if (ea->imm.has_label) goto rel_lab16;
			error ("Absolute value not allowed.");
			return;
		}
		/* PC + INDEX + OFFSET */
		else if (ea->reg == 3) {
			if (!ea->imm.has_label) error ("Absolute value not allowed.");
			wr_short (ea->ext.ext);
			add_fixup (get_bitpos()-1, BYTE, ea->imm.label);
			return;
		}
	}
	return;
rel_lab16:
	/* resolve a relative 16-bit label (offset) */
	wr_short (0);
	add_fixup (get_bitpos()-2, WORD, ea->imm.label);
	return;
abs_lab32:
	/* resolve an absolute 32-bit label */
	wr_int (0);
	add_fixup (get_bitpos ()-4, LONG_FIXUP, ea->imm.label);
}

char *get_reg (char *buf, int *reg_num, int flags)
{
	int type;
	while (isspace (*buf)) buf++;
	if ((flags & F_AREG) && (buf[0] == 'a')) type = 8;
	else if ((flags & F_DREG) && (buf[0] == 'd')) type = 0;
	else goto err;
	buf++;
	if (sscanf (&buf[0], "%d", reg_num) != 1) goto err;
	
	*reg_num += type;
	while (isdigit (*buf)) buf++;
	return buf;
err:
	error ("Expected a register.");
	return NULL;
}

char *get_ea (char *pos, ea_t *ea, int op_size, int flags)
{
	int reg;
	int size;
	char *orig;
	ea->imm.has_label = 0;	
	ea->op_size = op_size;
	while (isspace (*pos)) pos++;
	orig = pos;
	if ((pos[0] == 'd') && isdigit (pos[1])) {
		pos++;
		if (sscanf (&pos[0], "%d", &ea->reg) != 1) goto poopdog;
		ea->mode = 0;
		if ((ea->reg < 0) || (ea->reg > 7)) goto poopdog;
		while (isdigit (*pos)) pos++;
		if (!(flags & F_DREG)) goto err;
		return pos;
	}
	if ((pos[0] == 'a') && isdigit (pos[1])) {
		pos++;
		if (sscanf (&pos[0], "%d", &ea->reg) != 1) goto poopdog;
		ea->mode = 1;
		if ((ea->reg < 0) || (ea->reg > 7)) goto poopdog;
		ea->reg += 8;
		while (isdigit (*pos)) pos++;
		if (!(flags & F_AREG)) goto err;
		return pos;
	}
poopdog:
	pos = orig;
	if ((*pos == '#') && (flags & F_IMM)) {
		pos = get_imm (pos, &ea->imm, F_IMM);
		check_range (&ea->imm.val, op_size);
		ea->mode = 7;
		ea->reg = 4;
		return pos;
	}
	if ((flags & F_PRE) && (pos[0] == '-') && (pos[1] == '(')) {
		pos += 2;
		pos = get_reg (pos, &ea->reg, F_AREG);
		check (')');
		ea->mode = 0x4;
		return pos;
	}
	if ((flags & (F_IND | F_POST)) && (pos[0] == '(')) {
		check ('(');
		pos = get_reg (pos, &ea->reg, F_AREG);
		check (')');
		if ((flags & F_POST) && (pos[0] == '+')) {
			ea->mode = 3;
			return ++pos;
		} else if (flags & F_IND) {
			ea->mode = 2;
			return pos;
		} else goto err;
	}
	if ((*pos == '$') || isdigit (*pos) || (*pos == '-') || isalpha (*pos)) {
		if (isalpha (*pos)) {
			pos = get_imm (pos, &ea->imm, 0);
		} else if (*pos == '$') {
			pos++;
			if (sscanf (pos, "%x", &ea->imm.val) != 1) goto err;
			while (isxdigit (*pos)) pos++;
		} else {
			if (sscanf (pos, "%d", &ea->imm.val) != 1) goto err;
			if (*pos == '-') pos++;
			while (isdigit (*pos)) pos++;
		}
		/* Immediate memory (not label) */
		if ((*pos == '.') && (flags & (F_MEM_W | F_MEM_L))) {
			pos++;
			size = get_size (*pos);
			pos++;
			ea->mode = 0x7;
			if (size == WORD) {
				ea->reg = 0;
				if ((ea->imm.val < 0) || (ea->imm.val > 65535)) error ("Immediate value too large.");
			}
			else if (size == LONG) ea->reg = 1;
			else error ("Bad size.");
			return pos;
		}
		while (isspace (*pos)) pos++;
		if (((*pos == ',') || (*pos == '\0')) && (flags & F_MEM_L)) {
			/* Immed memory with label */
			ea->mode = 0x7;
			ea->reg = 1;
			return pos;
		}
		if (*pos != '(') goto err;
		/* adr reg or pc indexed or offset */
		pos++;
		if ((pos[0] == 'p') && (pos[1] == 'c')) {
			/* pc relative */
			pos += 2;
			if ((pos [0] == ')') && (flags & F_PC_OFFSET)) {
				if ((ea->imm.val < -32768) || (ea->imm.val > 32767)) error ("Immediate value too large.");
				ea->mode = 7;
				ea->reg = 2;
				return ++pos;
			}
			if (!(flags & F_PC_INDEX)) goto err;
			check (',');
			pos = get_reg (pos, &reg, F_AREG | F_DREG);
			ea->ext.ext = 0;
			if (reg > 7) {
				ea->ext._.reg = reg-8;
				ea->ext._.d_or_a = 1;
			} else {
				ea->ext._.reg = reg;
				ea->ext._.d_or_a = 0;
			}
			check ('.');
			if (pos[0] == 'l') {
				ea->ext._.size = 1;
			} else if (pos[0] == 'w') {
				ea->ext._.size = 0;
			} else error ("Index register must have size.");
			pos++;
			check (')');
			ea->mode = 7;
			ea->reg = 3;
			return pos;
		} else {
			/* adr with index/offset */
			pos = get_reg (pos, &ea->reg, F_AREG);
			if ((flags & F_OFFSET) && (pos[0] == ')')) {
				/* with offset only */
				if ((ea->imm.val < -32768) || (ea->imm.val > 32767)) error ("Immediate value too large.");
				ea->mode = 5;
				return ++pos;
			}
			if (!(flags & F_INDEX)) goto err;
			check (',');
			pos = get_reg (pos, &reg, F_AREG | F_DREG);
			if ((ea->imm.val < -128) || (ea->imm.val > 127)) error ("Immediate value too large.");
			ea->ext.ext = 0;
			ea->ext._.displacement = ea->imm.val;
			if (reg > 7) {
				ea->ext._.reg = reg-8;
				ea->ext._.d_or_a = 1;
			} else {
				ea->ext._.reg = reg;
				ea->ext._.d_or_a = 0;
			}
			check ('.');
			if (pos[0] == 'l') {
				ea->ext._.size = 1;
			} else if (pos[0] == 'w') {
				ea->ext._.size = 0;
			} else error ("Index register must have size.");
			pos++;
			check (')');
			ea->mode = 6;
			return pos;
		}	
	}
	error ("Illegal effective address.");
err:
	error ("Malformed effective address. Valid modes are: %s%s%s%s%s%s%s%s%s%s%s%s",
			(flags & F_DREG ? "dreg " : ""),
			(flags & F_AREG ? "areg " : ""),
			(flags & F_IND ? "indirect " : ""),
			(flags & F_POST ? "postincrement ": ""),
			(flags & F_PRE ? "predecrement ": ""),
			(flags & F_OFFSET ? "areg-offset ": ""),
			(flags & F_OFFSET ? "areg-offset-index ": ""),
			(flags & F_MEM_W ? "absolute.w ": ""),
			(flags & F_MEM_L ? "absolute.l ": ""),
			(flags & F_IMM ? "immediate ": ""),
			(flags & F_PC_OFFSET ? "pc-relative ": ""),
			(flags & F_PC_INDEX ? "pc-rel-index ": ""));
	return NULL;
}

int _reg_num (char *cunt)
{
	int is_areg;
	int num;
	
	if (cunt[0] == 'a') is_areg = 8;
	else is_areg = 0;
	cunt++;

	if (sscanf (cunt, "%d", &num) != 1) return -1;
	num += is_areg;

	while (isdigit (*cunt)) cunt++;
	if (isalpha (*cunt)) return -1;
	return num;
}

/* movem is a wanking cunt of doom */
/* this is such a sinful mess :-) */
char *parse_movem (char *pos, int size)
{
	union Opcode op;
	char *start = pos;
	char lab1[LAB_LEN];
	int lo, hi;
	/* d0-7,a0-7 */
	int Regs[16];
	int dr = 0; /* reg to mem */
	ea_t ea, ea2;
	memset (Regs, 0, 16*sizeof(int));
try_again:
	if ((pos[0] == 'a') || (pos[0] == 'd')) {
more_to_come:
		pos = rd_label (pos, lab1);
		lo = _reg_num (lab1);
		if (lo == -1) {
mess:
			/* mem to reg */
			dr = 1; 
			pos = start;
			pos = get_ea (pos, &ea, size, F_IND | F_POST | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			if (pos[0] != ',') goto err;
			pos++;
			goto try_again;
		}
		
		if (*pos == '-') {
			/* range of fucking registers */
			pos++;
			if (sscanf (pos, "%d", &hi) != 1) goto err;
			pos++;
			if ((hi < 0) || (hi > 7)) goto err;
			if (lo > 7) hi += 8;
		} else {
			hi = lo;
		}
		for (; lo<=hi; lo++) Regs[lo] = 1;
		if (pos[0] == '/') {
			pos++;
			goto more_to_come;
		}
	} else {
		goto mess;
	}
	if (!dr) {
		/* reg to mem */
		if (pos[0] != ',') goto err;
		pos++;
		pos = get_ea (pos, &ea, size, F_IND | F_PRE | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
	}
	op.code = 0x4880;
	op.movem.sz = (size == WORD ? 0 : 1);
	op.movem.dr = dr;
	op.movem.dest_reg = ea.reg;
	op.movem.dest_mode = ea.mode;
	wr_short (op.code);

	hi = 0;
	if (ea.mode == 0x4) {
		/* predecrement mode */
		for (lo=0; lo < 16; lo++) {
			if (Regs[15-lo]) hi |= (1<<lo);
		}
	} else {
		for (lo=0; lo < 16; lo++) {
			if (Regs[lo]) hi |= (1<<lo);
		}
	}
	wr_short (hi);
	/* movem seems to count offsets relative to the
	 * register bitfield... */
	last_op_addr += 2;
	wr_ea (&ea);

	if (output_c) {
		/* register mask in ea2->reg */
		ea2.reg = hi;
		comp->push_op2 (OP_MOVEM, &ea, &ea2, size, op.code);
	}
	
	return pos;
err:
	error ("your movem is all wanked up.");
	return NULL;
}

static char *do_dcx_labelled (char *pos, int size, struct ImmVal *imm)
{
	struct Label *l;
	char lab2[LAB_LEN];
	int rel_to = 0;
	
	if (*pos == '-') {
		/* form: LABEL1-LABEL2. LABEL2 must be resolvable NOW */
		pos = rd_label (++pos, lab2);

		l = get_label (lab2);
		if (l == NULL) error ("Error. dc.x LABEL1-LABEL2 must have resolvable LABEL2 on first pass.");

		rel_to = l->val;
	}

	switch (size) {
		case BYTE:
			wr_byte (0);
			add_fixup_rel_to (get_bitpos()-1, BYTE, imm->label, rel_to);
			break;
		case WORD:
			wr_short (0);
			add_fixup_rel_to (get_bitpos()-2, WORD, imm->label, rel_to);
			break;
		case LONG: default:
			wr_int (0);
			add_fixup_rel_to (get_bitpos()-4, LONG_FIXUP, imm->label, rel_to);
			break;
	}
	return pos;
}

int asm_pass1 (const char *bin_filename)
{
	int i, reg, size;
	union Opcode op;
	struct ImmVal imm;
	char lab1[LAB_LEN];
	char *equ_lab;
	char *pos;
	ea_t ea;
	ea_t ea2;
	int coutput_label;

	wr_short (0x601a);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_int (0);
	wr_short (0);

	dict_init (&labels);
	
	if (output_c) comp->begin (src_filename, bin_filename);
	
	while (fgets (buf, sizeof (buf), fin)) {
		last_op_addr = get_bitpos ();
		line_no++;
		pos = &buf[0];
		/* comments */
		equ_lab = NULL;
		while (isspace (*pos)) pos++;
		if (*pos == '\0') continue;
		if (*pos == '*') continue;
		if (*pos == ':') {
			/* empty label (":	moveq...") signifies a valid
			 * computed jump target without a defined label.
			 * Helpful for optimising generated asm output. */
			coutput_label = 1;
			pos++;
			check_whitespace ();
			if (*pos == '\0') continue;
		}
		else if (isalpha (*pos)) {
			equ_lab = pos;
			pos = rd_label (pos, lab1);
			if (*pos == ':') {
				/* add label */
				add_label (lab1, L_ADDR, get_bitpos () - BASE);
				if (dump_labels) {
					fprintf (stderr, "0x%x: %s\n", get_bitpos () - BASE, lab1);
				}
				if (output_c) {
					comp->label (lab1);
				}
				coutput_label = 1;
				equ_lab = NULL;
				pos++;
				check_whitespace ();
				if (*pos == '\0') continue;
			}
			while (isspace (*pos)) pos++;
		}

		/* EQU */
		if (strncmp (pos, "equ", 3) == 0) {
			pos += 3;
			check_whitespace();
			if (!equ_lab) error ("EQU without label.");
			pos = get_imm (pos, &imm, F_NOLABELS);
			add_label (lab1, L_CONST, imm.val);
			coutput_label = 0;
			continue;
		}
		if (equ_lab) {
			pos = equ_lab;
			equ_lab = NULL;
		}
			
		/* DC.X */
		if (strncmp (pos, "dc.", 3) == 0) {
			size = get_size (pos[3]);
			pos += 4;
			check_whitespace();
			coutput_label = 0;
			if ((size == BYTE) && (*pos == '"')) {
				thing:
				pos++;
				while (*pos != '"') {
					wr_byte (*pos);
					pos++;
				}
				/* double '""' escapes */
				pos++;
				if (*pos == '"') {
					wr_byte ('"');
					goto thing;
				}
				while (isspace (*pos)) pos++;
				if (*pos != ',') {
					check_end (pos);
					continue;
				}
				pos++;
			}
			while (isdigit (*pos) || isalpha (*pos) || (*pos == '$')) {
				pos = get_imm (pos, &imm, 0);
				if (imm.has_label) {
					pos = do_dcx_labelled (pos, size, &imm);
				} else {
					switch (size) {
						case BYTE: wr_byte (imm.val); break;
						case WORD: wr_short (imm.val); break;
						case LONG: default: wr_int (imm.val); break;
					}
				}
				while (isspace (*pos)) pos++;
				if (*pos != ',') break;
				pos++;
				while (isspace (*pos)) pos++;
			}
			check_end (pos);
			continue;
		}
		/* DS.X */
		if (strncmp (pos, "ds.", 3) == 0) {
			size = get_size (pos[3]);
			pos += 4;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS);
			check_end (pos);
			for (i=0; i<imm.val; i++) {
				if (size == BYTE) wr_byte (0);
				else if (size == WORD) wr_short (0);
				else wr_int (0);
			}
			continue;	
		}
		
		if (output_c) {
			comp->addr_label (coutput_label);
		}
		coutput_label = 0;

		/* odd address checking */
		if (last_op_addr & 0x1) error ("Odd address.");
		/* ADDQ/SUBQ */
		if ((strncmp (pos, "addq", 4)==0) ||
		    (strncmp (pos, "subq", 4)==0)) {
			op.code = 0x5000;
			op.addq.issub = (pos[0] == 's');
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			if ((imm.val < 1) || (imm.val > 8)) error ("Immediate value too large.");
			check (',');
			pos = get_ea (pos, &ea, LONG, F_DREG | F_AREG | F_IND | F_POST | F_PRE | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
			if ((size == BYTE) && (ea.mode == 1)) error ("Bad size for address reg direct.");
			check_end (pos);
			op.addq.dest_reg = ea.reg;
			op.addq.dest_mode = ea.mode;
			op.addq.size = size;
			op.addq.data = (imm.val == 8 ? 0 : imm.val);
			wr_short (op.code);
			wr_ea (&ea);

			if (output_c) {
				ea.op_size = size;
				ea2.op_size = size;
				ea_set_immediate (&ea2, imm.val);
				
				if (ea.mode == 1) {
					/* to areg is special: no condition codes set */
					if (op.addq.issub) {
						comp->push_op (OP_SUBA, &ea2, &ea, size);
					} else {
						comp->push_op (OP_ADDA, &ea2, &ea, size);
					}
				} else {
					if (op.addq.issub) {
						comp->push_op (OP_SUB, &ea2, &ea, size);
					} else {
						comp->push_op (OP_ADD, &ea2, &ea, size);
					}
				}
			}
			continue;
		}
		/* NOP */
		if (strncmp (pos, "nop", 3) == 0) {
			pos += 3;
			check_end (pos);
			wr_short (0x4e71);
			continue;
		}
		/* NOT/NEGX/NEG */
		if (strncmp (pos, "not", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4600;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				comp->push_op (OP_NOT, &ea, NULL, size);
			}
			continue;
		}
		if (strncmp (pos, "negx", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4000;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				comp->push_op (OP_NEGX, &ea, NULL, size);
			}
			continue;
		}
		if (strncmp (pos, "neg", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4400;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				comp->push_op (OP_NEG, &ea, NULL, size);
			}
			continue;
		}
		/* DIVS/DIVU */
		if ((strncmp (pos, "divs", 3) == 0) ||
		    (strncmp (pos, "divu", 3) == 0)) {
			int _signed = 0;
			pos += 3;
			if (pos[0] == 's') { op.code = 0x81c0; _signed = 1; }
			else op.code = 0x80c0;
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, WORD, F_ALL_NOAREG);
			check (',');
			pos = get_ea (pos, &ea2, WORD, F_DREG);
			check_end (pos);
			op.type2.reg = ea2.reg;
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				if (_signed) {
					comp->push_op (OP_DIVS, &ea, &ea2, WORD);
				} else {
					comp->push_op (OP_DIVU, &ea, &ea2, WORD);
				}
			}
			continue;
		}
		/* MULS/MULU */
		if (strncmp (pos, "mul", 3) == 0) {
			pos += 3;
			int _signed = 0;
			if (pos[0] == 's') { op.code = 0xc1c0; _signed = 1; }
			else op.code = 0xc0c0;
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, WORD, F_ALL_NOAREG);
			check (',');
			pos = get_ea (pos, &ea2, WORD, F_DREG);
			check_end (pos);
			op.type2.reg = ea2.reg;
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				if (_signed) {
					comp->push_op (OP_MULS, &ea, &ea2, WORD);
				} else {
					comp->push_op (OP_MULU, &ea, &ea2, WORD);
				}
			}
			continue;
		}
		/* ADDI/SUBI */
		if ((strncmp (pos, "addi", 4) == 0) ||
		    (strncmp (pos, "subi", 4) == 0)) {
			int sub = 0;
			if (pos[0] == 'a') {
				op.code = 0x0600;
			} else {
				op.code = 0x0400;
				sub = 1;
			}
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea2, size, F_IMM);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			if (size == BYTE) {
				wr_short (ea2.imm.val & 0xff);
			} else if (size == WORD) {
				wr_short (ea2.imm.val);
			} else {
				wr_int (ea2.imm.val);
			}
			wr_ea (&ea);
			
			if (output_c) {
				if (sub) {
					comp->push_op (OP_SUB, &ea2, &ea, size);
				} else {
					comp->push_op (OP_ADD, &ea2, &ea, size);
				}
			}
			
			continue;
		}
		/* ADDA/SUBA */
		if ((strncmp (pos, "adda", 4) == 0) ||
		    (strncmp (pos, "suba", 4) == 0)) {
			int is_sub = 0;
			if (pos[0] == 'a') op.code = 0xd000;
			else { op.code = 0x9000; is_sub = 1; }
			pos += 4;
			check ('.');
			size = get_size (pos[0]);
			if (size == BYTE) error ("Crap size for adda.");
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_ea (pos, &ea2, size, F_AREG);
			check_end (pos);
			op.type2.reg = ea2.reg;
			op.type2.op_mode = (size == WORD ? 3 : 7);
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				if (is_sub) {
					comp->push_op (OP_SUBA, &ea, &ea2, size);
				} else {
					comp->push_op (OP_ADDA, &ea, &ea2, size);
				}
			}
			continue;
		}
		/* ADDX/SUBX */
		if ((strncmp (pos, "addx", 4) == 0) ||
		    (strncmp (pos, "subx", 4) == 0)) {
			int _type = pos[0];
			if (pos[0] == 'a') op.code = 0xd100;
			else op.code = 0x9100;
			pos  += 4;
			check ('.');
			size = get_size (pos[0]);
			op.addx.size = size;
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_DREG | F_PRE);
			if (ea.mode == 0) {
				/* Dn,Dn mode */
				check (',');
				op.addx.src_reg = ea.reg;
				op.addx.rm = 0;
				pos = get_reg (pos, &i, F_DREG);
				check_end (pos);
				op.addx.dest_reg = i;
				wr_short (op.code);

				if (output_c) {
					ea2.mode = 0; ea2.reg = i; ea2.op_size = size;
				
					if (_type == 'a') comp->push_op (OP_ADDX, &ea, &ea2, size);
					else comp->push_op (OP_SUBX, &ea, &ea2, size);
				}
			} else {
				/* -(An),-(An) mode */
				check (',');
				op.addx.src_reg = ea.reg;
				op.addx.rm = 1;
				pos = get_ea (pos, &ea2, size, F_PRE);
				op.addx.dest_reg = ea2.reg;
				wr_short (op.code);

				if (output_c) {
					if (_type == 'a') comp->push_op (OP_ADDX, &ea, &ea2, size);
					else comp->push_op (OP_SUBX, &ea, &ea2, size);
				}
			}
			check_end (pos);
			continue;
		}
		/* ADD/SUB */
		if ((strncmp (pos, "add", 3) == 0) ||
		    (strncmp (pos, "sub", 3) == 0)) {
			int sub = 0;
			if (pos[0] == 'a') op.code = 0xd000;
			else {
				op.code = 0x9000;
				sub = 1;
			}
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_ea (pos, &ea2, size, F_DREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L);
			if (ea2.mode == 0) {
				/* data reg dest */
				op.type2.reg = ea2.reg;
				op.type2.op_mode = size;
				op.type2.ea_reg = ea.reg;
				op.type2.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
			} else {
				/* ea dest */
				if (ea.mode != 0) error ("One operand must be a data register.");
				op.type2.reg = ea.reg;
				op.type2.op_mode = size+4;
				op.type2.ea_reg = ea2.reg;
				op.type2.ea_mode = ea2.mode;
				wr_short (op.code);
				wr_ea (&ea2);
			}
			check_end (pos);
			
			if (output_c) {
				ea.op_size = size;
				ea2.op_size = size;
				
				if (sub) {
					comp->push_op (OP_SUB, &ea, &ea2, size);
				} else {
					comp->push_op (OP_ADD, &ea, &ea2, size);
				}
			}
			continue;
		}
		/* BCHG/BCLR/BSET/BTST */
		if ((strncmp (pos, "bchg", 4)==0) ||
		    (strncmp (pos, "bclr", 4)==0) ||
		    (strncmp (pos, "bset", 4)==0) ||
		    (strncmp (pos, "btst", 4)==0)) {
			int type;
			switch (pos[2]) {
				/* bchg */
				case 'h': type = 0; break;
				/* bclr */
				case 'l': type = 1; break;
				/* bset */
				case 'e': type = 2; break;
				/* btst */
				default: case 's': type = 3; break;
			}
			pos += 4;
			check_whitespace();
			pos = get_ea (pos, &ea, BYTE, F_IMM | F_DREG);
			if (ea.mode == 0) {
				/* Dn,<ea> mode */
				switch (type) {
					case 0: op.code = 0x0140; break;
					case 1: op.code = 0x0180; break;
					case 2: op.code = 0x01c0; break;
					case 3: op.code = 0x0100; break;
				}
				op.type2.reg = ea.reg;
				check (',');
				pos = get_ea (pos, &ea2, size, F_NOIMM_NOPC_NOAREG);
				op.type2.ea_reg = ea2.reg;
				op.type2.ea_mode = ea2.mode;
				wr_short (op.code);
				wr_ea (&ea2);
			} else {
				/* IMM,<ea> mode */
				check_range (&ea.imm.val, BYTE);
				imm.val = ea.imm.val;
				switch (type) {
					case 0: op.code = 0x0840; break;
					case 1: op.code = 0x0880; break;
					case 2: op.code = 0x08c0; break;
					case 3: op.code = 0x0800; break;
				}
				check (',');
				if (type == 3) {
					pos = get_ea (pos, &ea2, size, F_DREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
				} else {
					pos = get_ea (pos, &ea2, size, F_NOIMM_NOPC_NOAREG);
				}
				op.type2.ea_mode = ea2.mode;
				op.type2.ea_reg = ea2.reg;
				wr_short (op.code);
				wr_short (imm.val & 0xff);
				wr_ea (&ea2);
			}
			check_end (pos);

			if (output_c) {
				if (ea2.mode == 0) {
					size = LONG;
				} else {
					size = BYTE;
				}
				ea2.op_size = ea.op_size = size;
				switch (type) {
					/* bchg */
					case 0: comp->push_op (OP_BCHG, &ea, &ea2, size); break;
					/* bclr */
					case 1: comp->push_op (OP_BCLR, &ea, &ea2, size); break;
					/* bset */
					case 2: comp->push_op (OP_BSET, &ea, &ea2, size); break;
					/* btst */
					default: case 3:
						comp->push_op (OP_BTST, &ea, &ea2, size); break;
				}
			}
			continue;
		}
		/* ASx/LSx/ROx ROXx */
		if ((strncmp (pos, "ls", 2) == 0) ||
		    (strncmp (pos, "as", 2) == 0) ||
		    (strncmp (pos, "ro", 2) == 0)) {
			char dir;
			int type;
			dir = pos[2];
			if (pos[0] == 'a') type = 0;
			else if (pos[0] == 'l') type = 1;
			else if (pos[0] == 'r') {
				if (pos[2] == 'x') {
					type = 2;
					dir = pos[3];
				}
				else type = 3;
			} else {
				goto blork;
			}
			if ((dir != 'l') && (dir != 'r')) goto blork;
			dir = (dir == 'l' ? 1 : 0);
			while (isalpha (*pos)) pos++;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_DREG | F_IND | F_POST | F_PRE | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_IMM);
			
			if (ea.mode == 0) {
				/* data reg type */
				check (',');
				pos = get_reg (pos, &reg, F_DREG);
				check_end (pos);
				if (type == 1) op.code = 0xe008;
				else if (type == 0) op.code = 0xe000;
				else if (type == 2) op.code = 0xe010;
				else op.code = 0xe018;
				op.ASx.count_reg = ea.reg;
				op.ASx.dr = dir;
				op.ASx.size = size;
				op.ASx.ir = 1;
				op.ASx.reg = reg;
				wr_short (op.code);
			
				if (output_c) {
					ea.op_size = size;
					ea2.op_size = size;
					ea2.mode = 0; ea2.reg = reg;
					
					if (type == 0) {
						if (dir) comp->push_op (OP_ASL, &ea, &ea2, size);
						else comp->push_op (OP_ASR, &ea, &ea2, size);
					} else if (type == 1) {
						if (dir) comp->push_op (OP_LSL, &ea, &ea2, size);
						else comp->push_op (OP_LSR, &ea, &ea2, size);
					} else if (type == 2) {
						if (dir) comp->push_op (OP_ROXL, &ea, &ea2, size);
						else comp->push_op (OP_ROXR, &ea, &ea2, size);
					} else if (type == 3) {
						if (dir) comp->push_op (OP_ROL, &ea, &ea2, size);
						else comp->push_op (OP_ROR, &ea, &ea2, size);
					} else {
						error ("C code not generated (work unfinished).");
					}
				}
				continue;
			} else if ((ea.mode == 7) && (ea.reg == 4)) {
				/* imm,Dn type */
				check (',');
				pos = get_reg (pos, &reg, F_DREG);
				check_end (pos);
				if (type == 1) op.code = 0xe008;
				else if (type == 0) op.code = 0xe000;
				else if (type == 2) op.code = 0xe010;
				else op.code = 0xe018;
				if ((ea.imm.val < 1) || (ea.imm.val > 8)) error ("Bad immediate value.");
				op.ASx.count_reg = (ea.imm.val == 8 ? 0 : ea.imm.val);
				op.ASx.dr = dir;
				op.ASx.size = size;
				op.ASx.ir = 0;
				op.ASx.reg = reg;
				wr_short (op.code);
			
				if (output_c) {
					ea.op_size = size;
					ea2.op_size = size;
					ea2.mode = 0; ea2.reg = reg;
					
					if (type == 0) {
						if (dir) comp->push_op (OP_ASL, &ea, &ea2, size);
						else comp->push_op (OP_ASR, &ea, &ea2, size);
					} else if (type == 1) {
						if (dir) comp->push_op (OP_LSL, &ea, &ea2, size);
						else comp->push_op (OP_LSR, &ea, &ea2, size);
					} else if (type == 2) {
						if (dir) comp->push_op (OP_ROXL, &ea, &ea2, size);
						else comp->push_op (OP_ROXR, &ea, &ea2, size);
					} else if (type == 3) {
						if (dir) comp->push_op (OP_ROL, &ea, &ea2, size);
						else comp->push_op (OP_ROR, &ea, &ea2, size);
					} else {
						error ("C code not generated (work unfinished).");
					}
				}
				continue;
			} else {
				/* ea mode */
				if (size != WORD) error ("Illegal size.");
				check_end (pos);
				if (type == 1) op.code = 0xe2c0;
				else if (type == 0) op.code = 0xe0c0;
				else if (type == 2) op.code = 0xe4c0;
				else op.code = 0xe6c0;
				op.ASx.dr = dir;
				op.type1.ea_reg = ea.reg;
				op.type1.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
			
				if (output_c) {
					ea.op_size = size;
					ea2.op_size = size;
					ea_set_immediate (&ea2, 1);
					if (type == 0) {
						if (dir) comp->push_op (OP_ASL, &ea2, &ea, size);
						else comp->push_op (OP_ASR, &ea2, &ea, size);
					} else if (type == 1) {
						if (dir) comp->push_op (OP_LSL, &ea2, &ea, size);
						else comp->push_op (OP_LSR, &ea2, &ea, size);
					} else if (type == 2) {
						if (dir) comp->push_op (OP_ROXL, &ea2, &ea, size);
						else comp->push_op (OP_ROXR, &ea2, &ea, size);
					} else if (type == 3) {
						if (dir) comp->push_op (OP_ROL, &ea2, &ea, size);
						else comp->push_op (OP_ROR, &ea2, &ea, size);
					} else {
						error ("C code not generated (work unfinished).");
					}
				}
				continue;
			}
			continue;
		}
blork:
		/* ANDI/EORI/ORI */
		if ((strncmp (pos, "andi", 4) == 0) ||
		    (strncmp (pos, "eori", 4) == 0) ||
		    (strncmp (pos, "ori", 3) == 0)) {
			int _type = pos[0];
			if (pos[0] == 'a') {
				op.code = 0x0200;
			} else if (pos[0] == 'e') {
				op.code = 0x0a00;
			} else {
				op.code = 0x0000;
			}
			while (isalpha (*pos)) pos++;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			check_range (&imm.val, size);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			op.type1.size = size;
			wr_short (op.code);
			if (size == BYTE) {
				wr_short (imm.val & 0xff);
			} else if (size == WORD) {
				wr_short (imm.val);
			} else {
				wr_int (imm.val);
			}
			wr_ea (&ea);
			
			if (output_c) {
				ea2.op_size = size;
				ea_set_immediate (&ea2, imm.val);
				
				if (_type == 'e') {
					comp->push_op (OP_XOR, &ea2, &ea, size);
				} else if (_type == 'o') {
					comp->push_op (OP_OR, &ea2, &ea, size);
				} else if (_type == 'a') {
					comp->push_op (OP_AND, &ea2, &ea, size);
				}
			}
			continue;
		}
		/* AND/OR */
		if ((strncmp (pos, "and", 3) == 0) ||
		    (strncmp (pos, "or", 2) == 0)) {
			int _type = pos[0];
			if (pos[0] == 'a') op.code = 0xc000;
			else op.code = 0x8000;
			while (isalpha (*pos)) pos++;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL_NOAREG);
			check (',');
			pos = get_ea (pos, &ea2, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			if (ea2.mode == 0) {
				/* data reg dest */
				op.type2.reg = ea2.reg;
				op.type2.op_mode = size;
				op.type2.ea_reg = ea.reg;
				op.type2.ea_mode = ea.mode;
				wr_short (op.code);
				wr_ea (&ea);
			} else {
				/* ea dest */
				if (ea.mode != 0) error ("One operand must be a data register.");
				op.type2.reg = ea.reg;
				op.type2.op_mode = size+4;
				op.type2.ea_reg = ea2.reg;
				op.type2.ea_mode = ea2.mode;
				wr_short (op.code);
				wr_ea (&ea2);
			}
			
			if (output_c) {
				if (_type == 'a')
					comp->push_op (OP_AND, &ea, &ea2, size);
				else
					comp->push_op (OP_OR, &ea, &ea2, size);
			}
			continue;
		}
		/* EOR */
		if (strncmp (pos, "eor", 3) == 0) {
			op.code = 0xb000;
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_reg (pos, &reg, F_DREG);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.type2.reg = reg;
			op.type2.op_mode = size + 4;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea2.mode = 0; ea2.reg = reg;
				ea2.op_size = size;
				comp->push_op (OP_XOR, &ea2, &ea, size);
			}
			continue;
		}
		/* SWAP */
		if (strncmp (pos, "swap", 4) == 0) {
			pos += 4;
			pos = get_ea (pos, &ea, LONG, F_DREG);
			op.code = 0x4840;
			op.type2.ea_reg = ea.reg;
			check_end (pos);
			wr_short (op.code);
			
			if (output_c) {
				comp->push_op (OP_SWAP, &ea, NULL, 0);
			}
			continue;
		}
		/* BSR */
		if (strncmp (pos, "bsr", 3) == 0) {
			pos += 3;
			check ('.');
			size = pos[0];
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_MEM_L);
			if (ea.imm.has_label == 0) error ("BSR desires a label.");
			check_end (pos);
			op.code = 0x6100;
			wr_short (op.code);
			if (size == 's') {
				add_fixup (get_bitpos()-1, BYTE, ea.imm.label);
			} else if (size == 'w') {
				wr_short (0);
				add_fixup (get_bitpos()-2, WORD, ea.imm.label);
			} else {
				error ("Invalid size '.%c'.", size);
			}
			if (output_c) {
				comp->push_op (OP_JSR, &ea, NULL, 0);
			}
			continue;
		}
		/* Bcc.S */
		if (pos[0] == 'b') {
			for (i=0; i<16; i++) {
				if (Bcc_str [i] == NULL) continue;
				if (strncmp (&pos[1], Bcc_str[i], 2)==0) break;
			}
			if (i==16) goto fuckit;
			pos += 3;
			check ('.');
			size = pos[0];
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_MEM_L);
			if (ea.imm.has_label == 0) error ("BSR desires a label.");
			check_end (pos);
			op.code = 0x6000 | (i<<8);
			wr_short (op.code);
			if (size == 's') {
				add_fixup (get_bitpos()-1, BYTE, ea.imm.label);
			} else if (size == 'w') {
				wr_short (0);
				add_fixup (get_bitpos()-2, WORD, ea.imm.label);
			} else {
				error ("Invalid size '.%c'.", size);
			}
			if (output_c) {
				comp->push_op2 (OP_BCC, &ea, NULL, 0, op.code);
			}
			continue;
		}
		/* DBcc */
		if ((pos[0] == 'd') && (pos[1] == 'b')) {
			pos = rd_label (&pos[2], lab1);
			for (i=0; i<16; i++) {
				if (strncmp (lab1, DBcc_str[i], 2)==0) break;
			}
			if (i==16) {
				if (strncmp (lab1, "ra", 2)==0) {
					i = 1;
				} else {
					goto fuckit;
				}
			}
			while (isalpha (*pos)) pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, WORD, F_DREG);
			check (',');
			pos = get_ea (pos, &ea2, LONG, F_MEM_L);
			if (ea2.imm.has_label == 0) error ("DBcc desires a label.");
			check_end (pos);
			op.code = 0x50c8;
			op.DBcc.reg = ea.reg;
			op.DBcc.cond = i;
			wr_short (op.code);
			wr_short (0);
			add_fixup (get_bitpos()-2, WORD, ea2.imm.label);
			
			if (output_c) {
				comp->push_op2 (OP_DBCC, &ea, &ea2, 0, op.code);
			}
			continue;
		}
fuckit:
		/* CLR */
		if (strncmp (pos, "clr", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4200;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = size;
				comp->push_op (OP_CLR, &ea, NULL, size);
			}
			continue;
		}
		/* CMPI */
		if (strncmp (pos, "cmpi", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (*pos);
			pos++;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			check_range (&imm.val, size);
			check (',');
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x0c00;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			op.type1.size = size;
			wr_short (op.code);
			if (size == BYTE) {
				wr_short (imm.val & 0xff);
			} else if (size == WORD) {
				wr_short (imm.val);
			} else {
				wr_int (imm.val);
			}
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = size;
				ea2.op_size = size;
				ea_set_immediate (&ea2, imm.val);
				
				comp->push_op (OP_CMP, &ea2, &ea, size);
			}
			continue;
		}
		/* CMPA */
		if (strncmp (pos, "cmpa", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (*pos);
			pos++;
			if (size == BYTE) error ("Size must be word or long.");
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_reg (pos, &i, F_AREG);
			check_end (pos);

			op.code = 0xb000;
			op.type2.reg = i;
			op.type2.op_mode = (size == WORD ? 3 : 7);
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = size;
				ea2.op_size = size;
				ea2.mode = 1; ea2.reg = i;
				
				comp->push_op (OP_CMPA, &ea, &ea2, size);
			}
			continue;
		}
		/* CMPM */
		if (strncmp (pos, "cmpm", 4) == 0) {
			pos += 4;
			check ('.');
			size = get_size (*pos);
			pos++;
			check_whitespace ();
			pos = get_ea (pos, &ea, size, F_POST);
			op.code = 0xb108;
			op.cmpm.src_reg = ea.reg;
			op.cmpm.size = size;
			check (',');
			pos = get_ea (pos, &ea, size, F_POST);
			op.cmpm.dest_reg = ea.reg;
			wr_short (op.code);
			
			if (output_c) {
				error ("C code not generated (work unfinished).");
			}
			continue;
		}
		/* CMP */
		if (strncmp (pos, "cmp", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (*pos);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			if ((size == BYTE) && (ea.mode == 0x1))
				error ("Bad size for address register gropery.");
			pos = get_reg (pos, &i, F_DREG);
			check_end (pos);

			op.code = 0xb000;
			op.type2.reg = i;
			op.type2.op_mode = size;
			op.type2.ea_mode = ea.mode;
			op.type2.ea_reg = ea.reg;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = size;
				ea2.op_size = size;
				ea2.mode = 0; ea2.reg = i;
				
				comp->push_op (OP_CMP, &ea, &ea2, size);
			}
			continue;
		}
		/* EXG */
		if (strncmp (pos, "exg", 3) == 0) {
			pos += 3;
			op.code = 0xc100;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_DREG | F_AREG);
			check (',');
			op.exg.src_reg = ea.reg;
			if (ea.reg > 7) {
				/* Ax,Ay mode */
				op.exg.op_mode = 0x9;
				pos = get_ea (pos, &ea2, LONG, F_AREG);
				op.exg.dest_reg = ea2.reg;
			} else {
				/* Dx,Xn mode */
				pos = get_ea (pos, &ea2, LONG, F_AREG | F_DREG);
				if (ea2.reg > 7) {
					op.exg.op_mode = 0x11;
				} else {
					op.exg.op_mode = 0x8;
				}
				op.exg.dest_reg = ea2.reg;
			}
			check_end (pos);
			wr_short (op.code);
			
			if (output_c) {
				comp->push_op (OP_EXG, &ea, &ea2, LONG);
			}
			continue;
		}
		/* EXT */
		if (strncmp (pos, "ext", 3) == 0) {
			pos += 3;
			check ('.');
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			if (size == BYTE) error ("Ext dislikes bytes.");
			pos = get_ea (pos, &ea, size, F_DREG);
			check_end (pos);
			op.code = 0x4800;
			op.type2.ea_reg = ea.reg;
			op.type2.op_mode = size+1;
			wr_short (op.code);
			
			if (output_c) {
				comp->push_op (OP_EXT, &ea, NULL, size);
			}
			continue;
		}
			
			
		/* MOVEM */
		if (strncmp (pos, "movem.", 6)==0) {
			pos += 6;
			size = get_size (pos[0]);
			pos++;
			if (size == BYTE) error  ("Movem dislikes bytes.");
			check_whitespace();
			/* register to memory */
			pos = parse_movem (pos, size);
			check_end (pos);
			
			continue;
		}
		/* RTE */
		if (strncmp (pos, "rte", 3) == 0) {
			check_end (&pos[3]);
			wr_short (0x4e73);
			
			if (output_c) {
				comp->push_op_basic (OP_RTE);
			}
			continue;
		}
		/* RTS */
		if (strncmp (pos, "rts", 3) == 0) {
			check_end (&pos[3]);
			wr_short (0x4e75);
			
			if (output_c) {
				comp->push_op_basic (OP_RTS);
			}
			continue;
		}
		/* ILLEGAL */
		if (strncmp (pos, "illegal", 7) == 0) {
			check_end (&pos[7]);
			wr_short (0x4afc);
			
			if (output_c) {
				comp->push_op_basic (OP_ILLEGAL);
			}
			continue;
		}
		/* HOSTCALL -- Not 68000 */
		if (strncmp (pos, "hcall", 5) == 0) {
			pos += 5;
			check_whitespace();
			pos = get_imm (pos, &imm, F_IMM | F_NOLABELS);
			check_end (pos);
			check_range (&imm.val, WORD);
			wr_short (0x000b);
			wr_short (imm.val);
			
			if (output_c) {
				ea.imm.val = imm.val;
				comp->push_op (OP_HCALL, &ea, NULL, 0);
			}
			continue;
		}
		/* RESET */
		if (strncmp (pos, "reset", 5) == 0) {
			check_end (&pos[5]);
			wr_short (0x4e70);
			
			if (output_c) {
				error ("C code not generated (work unfinished).");
			}
			continue;
		}
		/* TRAP */
		if (strncmp (pos, "trap", 4) == 0) {
			/* TRAPV */
			if (pos[4] == 'v') {
				check_end (&pos[5]);
				wr_short (0x4e76);
			
				if (output_c) {
					error ("C code not generated (work unfinished).");
				}
				continue;
			}
			pos += 4;
			check_whitespace();
			pos = get_imm (pos, &imm, F_IMM | F_NOLABELS);
			check_end (pos);
			if ((imm.val < 0) || (imm.val > 15)) {
				error ("Trap vector out of range.");
			}
			wr_short (0x4e40 | imm.val);
			
			if (output_c) {
				error ("C code not generated (work unfinished).");
			}
			continue;
		}
		/* LINK */
		if (strncmp (pos, "link", 4) == 0) {
			pos += 4;
			check_whitespace();
			pos = get_reg (pos, &reg, F_AREG);
			check (',');
			pos = get_imm (pos, &imm, F_IMM | F_NOLABELS);
			check_end (pos);
			check_range (&imm.val, WORD);
			op.code = 0x4e50;
			op.type1.ea_reg = reg;
			wr_short (op.code);
			wr_short (imm.val);
			
			if (output_c) {
				ea.mode = 1; ea.reg = reg;
				ea2.imm.val = imm.val;
				comp->push_op (OP_LINK, &ea, &ea2, 0);
			}
			continue;
		}
		/* UNLK */
		if (strncmp (pos, "unlk", 4) == 0) {
			pos += 4;
			check_whitespace();
			pos = get_reg (pos, &reg, F_AREG);
			check_end (pos);
			op.code = 0x4e58;
			op.type1.ea_reg = reg;
			wr_short (op.code);
			
			if (output_c) {
				ea.mode = 1; ea.reg = reg;
				comp->push_op (OP_UNLK, &ea, NULL, 0);
			}
			continue;
		}
		/* MOVEQ */
		if (strncmp (pos, "moveq", 5) == 0){
			pos += 5;
			check_whitespace();
			pos = get_imm (pos, &imm, F_NOLABELS | F_IMM);
			check_range (&imm.val, BYTE);
			check (',');
			pos = get_ea (pos, &ea, LONG, F_DREG);
			check_end (pos);
			op.code = 0x7000;
			op.moveq.reg = ea.reg;
			op.moveq.data = imm.val;
			wr_short (op.code);

			if (output_c) {
				ea_set_immediate (&ea2, imm.val);
				comp->push_op (OP_MOVE, &ea2, &ea, LONG);
			}
			continue;
		}
		/* MOVEA */
		if (strncmp (pos, "movea.", 6) == 0) {
			pos += 6;
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_reg (pos, &i, F_AREG);
			check_end (pos);
is_movea:		
			if (size == BYTE) error ("Illegal size.");
			op.code = 0;
			op.move.size = move_size [size];
			op.move.dest_reg = i;
			op.move.dest_mode = 1;
			op.move.src_reg = ea.reg;
			op.move.src_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea2.mode = 1; ea2.reg = i;
				comp->push_op (OP_MOVEA, &ea, &ea2, size);
			}
			continue;
		}
		/* MOVE */
		if (strncmp (pos, "move.", 5) == 0) {
			pos += 5;
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_ALL);
			check (',');
			pos = get_ea (pos, &ea2, size, F_AREG | F_DREG | F_IND | F_PRE | F_POST | F_INDEX | F_OFFSET | F_MEM_W | F_MEM_L);
			check_end (pos);
			
			if ((ea.mode == 7) && (ea.reg == 1) && (ea.imm.has_label)) {
				if (strcmp (ea.imm.label, "sr")==0) {
					/* move from sr */
					if (size != WORD) error ("Illegal size.");
					op.code = 0x40c0;
					op.type1.ea_reg = ea2.reg;
					op.type1.ea_mode = ea2.mode;
					wr_short (op.code);
					wr_ea (&ea2);
			
					if (output_c) {
						error ("C code not generated (work unfinished).");
					}
					continue;
				}
			}
			if (ea2.mode == 1) {
				i = ea2.reg;
				goto is_movea;
			}
			if ((ea2.mode == 7) && (ea2.reg == 1) && (ea2.imm.has_label)) {
				if (strcmp (ea2.imm.label, "sr")==0) {
					/* move to status reg */
					if (size != WORD) error ("Illegal size.");
					op.code = 0x46c0;
					op.type1.ea_reg = ea.reg;
					op.type1.ea_mode = ea.mode;
					wr_short (op.code);
					wr_ea (&ea);
			
					if (output_c) {
						error ("C code not generated (work unfinished).");
					}
					continue;
				}
			}
			op.code = 0;
			op.move.size = move_size [size];
			op.move.dest_reg = ea2.reg;
			op.move.dest_mode = ea2.mode;
			op.move.src_reg = ea.reg;
			op.move.src_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			wr_ea (&ea2);

			if (output_c) {
				comp->push_op (OP_MOVE, &ea, &ea2, size);
			}
			continue;
		}
		/* TST */
		if (strncmp (pos, "tst.", 4) == 0) {
			pos += 4;
			size = get_size (pos[0]);
			pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, size, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4a00;
			op.type1.size = size;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = size;
				comp->push_op (OP_TST, &ea, NULL, size);
			}
			continue;
		}
		/* PEA */
		if (strncmp (pos, "pea", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check_end (pos);
			op.code = 0x4840;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = LONG;
				comp->push_op (OP_PEA, &ea, NULL, LONG);
			}
			continue;
		}
		/* LEA */
		if (strncmp (pos, "lea", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check (',');
			pos = get_ea (pos, &ea2, LONG, F_AREG);
			check_end (pos);
			op.code = 0x41e0;
			op.type2.ea_reg = ea.reg;
			op.type2.ea_mode = ea.mode;
			op.type2.reg = ea2.reg;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				ea.op_size = LONG;
				comp->push_op (OP_LEA, &ea, &ea2, LONG);
			}
			continue;
		}

		/* JSR */
		if (strncmp (pos, "jsr", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET |
					F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check_end (pos);
			op.code = 0x4e80;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				comp->push_op (OP_JSR, &ea, NULL, 0);
			}
			continue;
		}
		/* JMP */
		if (strncmp (pos, "jmp", 3) == 0) {
			pos += 3;
			check_whitespace();
			pos = get_ea (pos, &ea, LONG, F_IND | F_OFFSET |
					F_INDEX | F_MEM_W | F_MEM_L | F_PC_OFFSET | F_PC_INDEX);
			check_end (pos);
			op.code = 0x4ec0;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				comp->push_op (OP_JMP, &ea, NULL, 0);
			}
			continue;
		}
		/* TAS */
		if (strncmp (pos, "tas", 3) == 0) {
			pos += 3;
			check_whitespace ();
			pos = get_ea (pos, &ea, BYTE, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x4ac0;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				error ("C code not generated (work unfinished).");
			}
			continue;
		}
		/* Scc */
		if (pos[0] == 's') {
			pos = rd_label (&pos[1], lab1);
			for (i=0; i<16; i++) {
				if (strncmp (lab1, DBcc_str[i], 2)==0) break;
			}
			if (i==16) goto fuckit2;
			while (isalpha (*pos)) pos++;
			check_whitespace();
			pos = get_ea (pos, &ea, BYTE, F_NOIMM_NOPC_NOAREG);
			check_end (pos);
			op.code = 0x50c0;
			op.DBcc.cond = i;
			op.type1.ea_reg = ea.reg;
			op.type1.ea_mode = ea.mode;
			wr_short (op.code);
			wr_ea (&ea);
			
			if (output_c) {
				comp->push_op2 (OP_SCC, &ea, NULL, BYTE, op.code);
			}
			continue;
		}
fuckit2:
		error ("Unknown opcode %s.", pos);
	}
	/* write text section length */
	size = get_bitpos () - BASE;
	set_bitpos (2);
	wr_int (size);
	/* empty reloc table for the moment */
	set_bitpos (size + BASE);
	wr_int (0);
	
	if (output_c) comp->end (src_filename);

	return size + BASE;
}

int asm_pass2 (int fixup_pos)
{
	struct Label *lab;
	struct Fixup *fix;
	int last_adr = BASE;
	int _first = 1;
	int dist;
	int num_relocs = 0;

	fix = fix_first;

	for (; fix!=NULL; fix = fix->next) {
		if (fix->size == C_ADDR) continue;
		if (fix->size == C_FUNC) continue;

		line_no = fix->line_no;
		lab = get_label (fix->label);
		if (!lab) {
			error ("Undefined label '%s'.", fix->label);
		}
		if (lab->type == L_CONST) {
			error ("Illegal absolute value.");
		}
		set_bitpos (fix->adr);
		if (fix->size == BYTE) {
			dist = lab->val - fix->rel_to;
			if ((dist < -128) || (dist > 127)) {
				error ("Offset too big (%d).", dist);
			}
			wr_byte (dist);
		} else if (fix->size == WORD) {
			dist = lab->val - fix->rel_to;
			if ((dist < -32768) || (dist > 32767)) {
				error ("Offset too big (%d).", dist);
			}
			wr_short (dist);
		} else if (fix->size >= LONG) {
			num_relocs++;
			wr_int (lab->val);
			set_bitpos (fixup_pos);
			if (_first) {
				wr_int (fix->adr - BASE);
				last_adr = fix->adr;
				fixup_pos+=4;
				_first = 0;
			} else {
				dist = fix->adr - last_adr;
				while (dist > 254) {
					wr_byte (1);
					dist -= 254;
					fixup_pos++;
				}
				wr_byte (dist);
				last_adr = fix->adr;
				fixup_pos++;
			}
		}  else {error ("Unknown size in fixup tab.");}
	}
	set_bitpos (fixup_pos);
	if (_first) wr_int (0);
	else wr_byte (0);

	return num_relocs;
}

int main (int argc, char **argv)
{
	int arg, size, num;
	char bin_filename[128];
	if (argc == 1) {
		fprintf (stderr, "Usage: ./as68k [--dump-labels] [--output-c | --output-i386] file.s\n");
		exit (0);
	}
	
	for (arg=1; arg<argc-1; arg++) {
		
		if (strcmp (argv[arg], "--dump-labels") == 0) {
			dump_labels = 1;
		}
		else if (strcmp (argv[arg], "--output-c") == 0) {
			comp = &compiler[COMPILE_C];
			output_c = 1;
		}
		else if (strcmp (argv[arg], "--output-i386") == 0) {
			comp = &compiler[COMPILE_i386];
			output_c = 1;
		}
		else {
			fprintf (stderr, "Unknown option: '%s'\n", argv[arg]);
		}
	}
	
	src_filename = argv[arg];
	if ((fin = fopen (src_filename, "r"))==NULL) {
		printf ("Error. Cannot open %s.\n", src_filename);
		exit (0);
	}

	snprintf (bin_filename, sizeof (bin_filename), "%s.bin", src_filename);
	fout = fopen (bin_filename, "wb");

	fprintf (stderr, "Pass 1\n");
	size = asm_pass1 (bin_filename);
	fprintf (stderr, "Pass 2\n");
	num = asm_pass2 (size);
	fseek (fout, 0, SEEK_END);
	size = ftell (fout);

	/* on the ST 0x12 in executable is reserved. we put total
	 * size here because we are silly people */
	set_bitpos (0x12);
	wr_int (size);
	
	fprintf (stderr, "Done! %d bytes and %d relocations.\n", size, num);
		
	return 0;
}

