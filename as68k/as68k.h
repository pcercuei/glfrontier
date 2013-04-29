#ifndef _AS68K_H
#define _AS68K_H

typedef unsigned int uint;

#define LAB_LEN	64
/* atari .prg binaries have header then code at 0x1c */
#define BASE	0x1c

/* ea modes as encoded in the 68k instructions */
#define MODE_DREG	0

/* and ea modes as flags, used in various assembler funcs */
#define F_DREG		(1<<0)
#define F_AREG		(1<<1)
#define F_IND		(1<<2)
#define F_POST		(1<<3)
#define F_PRE		(1<<4)
#define F_OFFSET	(1<<5)
#define F_INDEX		(1<<6)
#define F_MEM_W		(1<<7)
#define F_MEM_L		(1<<8)
#define F_IMM		(1<<9)
#define F_PC_OFFSET	(1<<10)
#define F_PC_INDEX	(1<<11)

#define F_NOLABELS	(1<<12)

#define F_NOIMM_NOPC_NOAREG	(F_DREG | F_IND | F_POST | F_PRE | \
		F_OFFSET | F_INDEX | F_MEM_W | F_MEM_L)
#define F_ALL (0xffffffff & (~(F_NOLABELS)))
#define F_ALL_NOAREG	(F_ALL & (~F_AREG))

enum SIZE { BYTE, WORD, LONG, LONG_FIXUP, C_ADDR, C_FUNC };
extern const int size2len[];

struct ImmVal {
	int has_label;
	char label[LAB_LEN];
	int val;
};

typedef struct ea_t {
	int mode;
	int reg;
	int op_size;
	struct ImmVal imm;
	union Ext {
		short ext;
		struct {
			int displacement : 8;
			int ZERO : 3;
			uint size : 1;
			uint reg : 3;
			uint d_or_a : 1;
		} _;
	} ext;
	/* used by the ->i386 compiler. set by x_readea, and will
	 * be (for example) eax, or some immediate value in string
	 * form */
	char identifier[32];
	int x86_reg;
	/* used to keep the writeback address for complex addressing
	 * modes. reused at the end of the instruction... */
	int x86_reg_writeback;
	int do_writeback;
} ea_t;

typedef union Opcode {
	unsigned short code;
	struct move {
		uint src_reg : 3;
		uint src_mode : 3;
		uint dest_mode : 3;
		uint dest_reg : 3;
		uint size : 2;
		uint op : 2;
	} move;
	struct adr_index {
		int displacement : 8;
		uint zeros : 3;
		uint ind_size : 1;
		uint reg : 3;
		uint reg_type : 1;
	} adr_index;
	struct addq {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint size : 2;
		uint issub : 1;
		uint data : 3;
		uint op : 4;
	} addq;
	struct jmp {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint op : 10;
	} jmp;
	struct addi {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint size : 2;
		uint OP6 : 8;
	} addi;
	/* size and effective address */
	struct type1 {
		uint ea_reg : 3;
		uint ea_mode : 3;
		uint size : 2;
		uint op : 8;
	} type1;
	/* reg, opmode, ea */
	struct type2 {
		uint ea_reg : 3;
		uint ea_mode : 3;
		uint op_mode : 3;
		uint reg : 3;
		uint op : 4;
	} type2;
	struct MemShift {
		uint ea_reg : 3;
		uint ea_mode : 3;
		uint OP3 : 2;
		uint dr : 1;
		uint type : 2;
		uint OP0x1c : 5;
	} MemShift;
	struct ASx {
		uint reg : 3;
		uint OP0 : 2;
		uint ir : 1;
		uint size : 2;
		uint dr : 1;
		uint count_reg : 3;
		uint OP0xe : 4;
	} ASx;
	struct abcd {
		uint src_reg : 3;
		uint rm : 1;
		uint OP16 : 5;
		uint dest_reg : 3;
		uint OP12 : 4;
	} abcd;
	struct addx {
		uint src_reg : 3;
		uint rm : 1;
		uint OP0 : 2;
		uint size : 2;
		uint OP1 : 1;
		uint dest_reg : 3;
		uint op : 4;
	} addx;
	struct cmpm {
		uint src_reg : 3;
		uint OP1_1 : 3;
		uint size : 2;
		uint OP2_1 : 1;
		uint dest_reg : 3;
		uint OP0xb : 4;
	} cmpm;
	/* branches */
	struct DBcc {
		uint reg : 3;
		uint OP25 : 5;
		uint cond : 4;
		uint OP5 : 4;
	} DBcc;
	struct Bcc {
		int displacement : 8;
		uint cond : 4;
		uint op : 4;
	} Bcc;
	struct bra {
		int displacement : 8;
		uint op : 8;
	} bra;
	struct movem {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint sz : 1;
		uint ONE : 3;
		uint dr : 1;
		uint NINE : 5;
	} movem;
	struct lea {
		uint dest_reg : 3;
		uint dest_mode : 3;
		uint SEVEN : 3;
		uint reg : 3;
		uint FOUR : 4;
	} lea;
	struct moveq {
		int data : 8;
		uint ZERO : 1;
		uint reg : 3;
		uint SEVEN : 4;
	} moveq;
	struct exg {
		uint dest_reg : 3;
		uint op_mode : 5;
		uint OP1 : 1;
		uint src_reg : 3;
		uint OP0xc : 4;
	} exg;
} Opcode;

struct Fixup {
	int rel_to;
	int line_no;
	int size;
	int adr;
	char label[LAB_LEN];
	struct Fixup *next;
};

struct Label {
	const char *name;
	int val;
	enum LAB_TYPE {
		L_ADDR,
		L_CONST
	} type;
};

/* label lookup */
struct Label *get_label (const char *name);
void add_label (const char *name, int type, int val);
void add_fixup (int adr, int size, const char *label);
void error (const char *format, ...);
void bug_error (const char *format, ...);
int get_bitpos ();

/* fixups linked list */
extern struct Fixup *fix_first;
extern struct Fixup *fix_last;
extern int line_no;

#endif /* _AS68K_H */
