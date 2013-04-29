#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "dict.h"
#include "as68k.h"
#include "output.h"

struct Compiler compiler[COMPILE_MAX] = {
	{
		.push_op = c_push_op,
		.push_op2 = c_push_op2,
		.push_op_basic = c_push_op_basic,
		.label = c_label,
		.addr_label = c_addr_label,
		.begin = c_begin,
		.end = c_end
	}, {
		.push_op = i386_push_op,
		.push_op2 = i386_push_op2,
		.push_op_basic = i386_push_op_basic,
		.label = i386_label,
		.addr_label = i386_addr_label,
		.begin = i386_begin,
		.end = i386_end
	}
};

