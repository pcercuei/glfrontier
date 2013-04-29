
/* d0.b = exception number, a0 = handler. */
static void SetExceptionHandler ()
{
	printf ("Setting exception handler %d to $%x\n", GetReg (0), GetReg (8));
	/* only 32 handlers */
	exception_handlers[GetReg(0) & 31] = GetReg (8);
}

static void PutChar ()
{
	putchar (GetReg (0));
}

HOSTCALL hcalls [] = {
	&SetExceptionHandler,
	&PutChar,
	NULL,				/* 0x50 */
};
