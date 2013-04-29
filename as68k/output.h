#ifndef _OUTPUT_H
#define _OUTPUT_H

/*
 * The C outputted 68k program behaves in a not entirely 68k compatible manner.
 * The exception table of the 68k does not exist and exceptions must be set by
 * host call. Pending exceptions are only tested on *conditional* branch instructions,
 * so a tight 'jmp' or 'bra' loop will not permit exceptions to be handled.
 *
 * There is no distinction between user and supervisor mode. No instructions to
 * access the condition codes have been implemented.
 * 
 * Not implemented:
 * 	abcd, andi to ccr, andi to sr, bkpt, chk, cmpm, eori to ccr, eori to sr,
 * 	move to/from ccr, move to/from sr, move usp, movec, movep, moves, nbcd,
 * 	ori to ccr, ori to sr, reset, rtd, rtr, sbcd, stop, tas, trap, trapv
 * These are mostly a load of crap.
 */

#define COMPILE_C	0
#define COMPILE_i386	1
#define COMPILE_MAX	2

enum M68K_OPS {
	OP_NONE, OP_JMP, OP_BCHG, OP_BCLR, OP_BSET, OP_BTST, OP_MULS, OP_MULU,
	OP_AND, OP_OR, OP_XOR, OP_NOT, OP_NEGX, OP_NEG, OP_DIVU, OP_DIVS, OP_ASL,
	OP_LSL, OP_ROXR, OP_ROXL, OP_ROL, OP_ROR, OP_ASR, OP_LSR,
	OP_MOVEM, OP_SUBX, OP_ADDX, OP_ADD, OP_CMPA, OP_CMP, OP_SUB,
	OP_ADDA, OP_SUBA, OP_SWAP, OP_JSR, OP_BCC, OP_DBCC, OP_CLR,
	OP_EXG, OP_EXT, OP_RTE, OP_RTS, OP_ILLEGAL, OP_HCALL, OP_LINK,
	OP_UNLK, OP_MOVE, OP_MOVEA, OP_TST, OP_PEA, OP_LEA, OP_SCC, OP_MAX
};

typedef struct Operation {
	enum M68K_OPS optype;
	int size;
	Opcode opcode;
	ea_t src;
	ea_t dest;
} op_t;

struct Compiler {
	void (*push_op) (enum M68K_OPS type, ea_t *src, ea_t *dest, int size);
	void (*push_op2) (enum M68K_OPS type, ea_t *src, ea_t *dest, int size, int opcode);
	void (*push_op_basic) (enum M68K_OPS type);

	void (*label) (const char *lab);
	void (*addr_label) (int labelled);
	
	void (*begin) (const char *src_filename, const char *bin_filename);
	void (*end) (const char *src_filename);
};

extern struct Compiler compiler[COMPILE_MAX];

extern void c_push_op (enum M68K_OPS type, ea_t *src, ea_t *dest, int size);
extern void c_push_op2 (enum M68K_OPS type, ea_t *src, ea_t *dest, int size, int opcode);
extern void c_push_op_basic (enum M68K_OPS type);
extern void c_begin (const char *src_filename, const char *bin_filename);
extern void c_end (const char *src_filename);
extern void c_label (const char *lab);
extern void c_addr_label ();

extern void i386_push_op (enum M68K_OPS type, ea_t *src, ea_t *dest, int size);
extern void i386_push_op2 (enum M68K_OPS type, ea_t *src, ea_t *dest, int size, int opcode);
extern void i386_push_op_basic (enum M68K_OPS type);
extern void i386_begin (const char *src_filename, const char *bin_filename);
extern void i386_end (const char *src_filename);
extern void i386_label (const char *lab);
extern void i386_addr_label ();

#endif /* _OUTPUT_H */
