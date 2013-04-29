#include <stdio.h>
#include "m68000.h"
#include "hcalls.h"
extern int exceptions_pending;
int main ()
{
	if (!Init680x0 ()) {
		printf ("Error loading poop.cunt\n");
	}
	//FlagException (0);
	//printf ("P 0x%x\n", exceptions_pending);
	SetReg (14, 0xdeadbeef);
	MemWriteLong (0x50, 0xdeadcafe);
	printf ("%p\n", STRam);
	Start680x0 ();
	
	DumpRegs ();
	
	return 0;
}
