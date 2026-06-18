/* xsh_lab5.c - user-mode test program for the keyboard/VGA console lab
 *              (Lab 5, 2023202296 Li Gan)
 *
 *   Like the lab4 command, this runs entirely at CPL=3 (paged user mode):
 *   it is created with k2023202296_create_vproc and reaches every kernel
 *   service through int $0x80.  printf() is formatted in user space and
 *   emitted with one SYS_PRINTS call; its output lands on the graphics-mode
 *   VGA text console (and the serial mirror).
 *
 *   The command demonstrates the console output requirements (req 2.2):
 *   the address of a local variable, the argument vector, the student id
 *   and name, a live address-space map, lines longer than one 80-column
 *   row, a screenful of scrolling text, and the \r \n \t control chars.
 */

#include <xinu.h>
#include <stdarg.h>

extern void _fdoprnt(char *, va_list, int (*func)(int, int), int);

/* ---- user-mode helpers (all run at CPL=3) ---- */

static int u2023202296_putbuf(int acpp, int ac)
{
	char **cpp = (char **)acpp;
	char c = (char)ac;
	return (*(*cpp)++ = c);
}

static void u2023202296_printf(char *fmt, ...)
{
	char buf[512];
	va_list ap;
	char *s = buf;

	va_start(ap, fmt);
	_fdoprnt(fmt, ap, u2023202296_putbuf, (int)&s);
	va_end(ap);
	u2023202296_syscall(SYS_PRINTS, (uint32)buf, (uint32)(s - buf), 0, 0);
}

static pid32 u2023202296_getpid(void)
{
	return (pid32)u2023202296_syscall(SYS_GETPID, 0, 0, 0, 0);
}

static uint32 u2023202296_cpl(void)
{
	uint32 cs;
	asm volatile("movl %%cs, %0" : "=r"(cs));
	return cs & 3;
}

static void u2023202296_dumpmap(void)
{
	u2023202296_syscall(SYS_DUMPMAP, 0, 0, 0, 0);
}

/*------------------------------------------------------------------------
 * u2023202296_lab5_longline - print a string wider than one 80-column row
 *	with no embedded newline, so the terminal must wrap it (req 2.2.d)
 *------------------------------------------------------------------------
 */
static void u2023202296_lab5_longline(void)
{
	char line[121];
	int32 i;

	for (i = 0; i < 120; i++)
		line[i] = (char)('0' + (i % 10));	/* 0123456789 ... */
	line[120] = '\0';
	u2023202296_printf("[lab5] long line (120 cols, no newline; wraps at column 80):\n");
	u2023202296_printf("%s\n", line);
}

/*------------------------------------------------------------------------
 * u2023202296_lab5_special - exercise digits, letters, symbols and the
 *	\r \n \t control characters (req 2.2.d)
 *------------------------------------------------------------------------
 */
static void u2023202296_lab5_special(void)
{
	u2023202296_printf("[lab5] digits/letters/symbols:\n");
	u2023202296_printf("  0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz\n");
	u2023202296_printf("  !\"#$%%&'()*+,-./:;<=>?@[\\]^_`{|}~\n");
	u2023202296_printf("[lab5] TAB alignment (\\t between fields):\n");
	u2023202296_printf("  a\tbb\tccc\tdddd\tend\n");
	u2023202296_printf("[lab5] carriage return (\\r): 'aaaaaaaa\\rBBBB' -> ");
	u2023202296_printf("aaaaaaaa\rBBBB\n");
}

/*------------------------------------------------------------------------
 * u2023202296_lab5_scroll - print more lines than fit on one screen so the
 *	terminal scrolls (req 2.2.d: printf("abcd\n") repeated)
 *------------------------------------------------------------------------
 */
static void u2023202296_lab5_scroll(void)
{
	int32 i;

	u2023202296_printf("[lab5] scrolling test: 40 numbered lines (screen holds 30)\n");
	for (i = 1; i <= 40; i++)
		u2023202296_printf("  line %2d: abcdefghij ABCDEFGHIJ 0123456789\n", i);
	u2023202296_printf("[lab5] scrolling test done\n");
}

/*------------------------------------------------------------------------
 * u2023202296_xsh_lab5 - the user-mode "lab5" shell command (CPL=3)
 *------------------------------------------------------------------------
 */
shellcmd u2023202296_xsh_lab5(int32 nargs, char *args[])
{
	int32 x;
	int32 i;

	u2023202296_printf("\n[lab5] xsh_lab5 running in USER mode: pid=%d name=%s CPL=%d\n",
			   u2023202296_getpid(),
			   proctab[u2023202296_getpid()].prname,
			   u2023202296_cpl());

	/* req 2.2.b: address of a local variable + the argument vector	*/
	u2023202296_printf("[lab5] &x = 0x%x  (this local variable lives on the user stack)\n",
			   (uint32)&x);
	u2023202296_printf("[lab5] argc = %d\n", nargs);
	for (i = 0; i < nargs; i++)
		u2023202296_printf("  arg-%d: %s\n", i, args[i]);

	/* req 2.2.b: student id and name (pinyin)			*/
	u2023202296_printf("[lab5] 2023202296 Li Gan\n");

	/* Sub-tests selected by the first argument:			*/
	/*   lab5 scroll  -> req 2.2.d screenful scrolling		*/
	/*   lab5 chars   -> req 2.2.d long line + special characters	*/
	/*   lab5 [...]    -> req 2.2.b/c basics + address-space map	*/
	if (nargs >= 2 && args[1][0] == 's') {
		u2023202296_lab5_scroll();
	} else if (nargs >= 2 && args[1][0] == 'c') {
		u2023202296_lab5_special();
		u2023202296_lab5_longline();
	} else {
		/* req 2.2.c: view the address-space map during the run	*/
		u2023202296_printf("[lab5] address-space map of this user process:\n");
		u2023202296_dumpmap();
	}

	u2023202296_printf("[lab5] 2023202296 Li Gan\n");

	/* req 2.2.c/e: return normally; the kernel tears down the page	*/
	/* directory, page tables and user stack in kill()/free_vproc.	*/
	u2023202296_user_exit();
	return 0;
}
