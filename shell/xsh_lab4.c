/* xsh_lab4.c - user-mode test program for the paged-memory lab (2023202296)
 *
 *   Everything in this file executes at CPL=3 (user mode).  All kernel
 *   services are reached through int $0x80 system calls.  fork()/exec() use
 *   the Lab4 paged implementations; printf() is formatted in user space and
 *   emitted with a single SYS_PRINTS call.
 */

#include <xinu.h>
#include <stdarg.h>

/* _fdoprnt is the kernel's formatter; it only invokes the supplied callback
 * and touches the user buffer, so it is safe to run at CPL3.            */
extern void _fdoprnt(char *, va_list, int (*func)(int, int), int);

/* ---- user-mode helpers (all run at CPL3) ---- */

static int u2023202296_putbuf(int acpp, int ac)
{
	char **cpp = (char **)acpp;
	char c = (char)ac;
	return (*(*cpp)++ = c);
}

static void u2023202296_printf(char *fmt, ...)
{
	char buf[256];
	va_list ap;
	char *s = buf;

	va_start(ap, fmt);
	_fdoprnt(fmt, ap, u2023202296_putbuf, (int)&s);
	va_end(ap);
	u2023202296_syscall(SYS_PRINTS, (uint32)buf, (uint32)(s - buf), 0, 0);
}

static pid32 u2023202296_fork(void)
{
	return (pid32)u2023202296_syscall(SYS_FORK, 0, 0, 0, 0);
}

static void u2023202296_exec(void *func, pri16 prio, char *name,
			     uint32 nargs, ...)
{
	struct k2023202296_execargs ea;
	va_list ap;
	uint32 i;

	ea.func = func;
	ea.prio = prio;
	ea.name = name;
	ea.nargs = nargs;
	va_start(ap, nargs);
	for (i = 0; i < nargs && i < K4_MAXARGS; i++)
		ea.args[i] = va_arg(ap, uint32);
	va_end(ap);
	u2023202296_syscall(SYS_EXEC, (uint32)&ea, 0, 0, 0);
}

static pid32 u2023202296_getpid(void)
{
	return (pid32)u2023202296_syscall(SYS_GETPID, 0, 0, 0, 0);
}

static pid32 u2023202296_receive(void)
{
	return (pid32)u2023202296_syscall(SYS_RECEIVE, 0, 0, 0, 0);
}

static uint32 u2023202296_cpl(void)
{
	uint32 cs;
	asm volatile("movl %%cs, %0" : "=r"(cs));
	return cs & 3;
}

/* paged-heap wrappers (req 2.5.a); the caller remembers the block size. */
static uint32 u2023202296_malloc(uint32 nbytes)
{
	return u2023202296_syscall(SYS_MALLOC, nbytes, 0, 0, 0);
}

static void u2023202296_free(uint32 va, uint32 nbytes)
{
	u2023202296_syscall(SYS_FREE, va, nbytes, 0, 0);
}

static void u2023202296_dumpmap(void)
{
	u2023202296_syscall(SYS_DUMPMAP, 0, 0, 0, 0);
}

/*------------------------------------------------------------------------
 * fork_test1 - the child does NOT exec; it runs the same code as the parent.
 *   Demonstrates address-space isolation: parent and child print the same
 *   virtual address for `lvar`, yet the child's write to `lvar` is NOT
 *   visible to the parent (they back the same VA with different frames).
 *------------------------------------------------------------------------
 */
void u2023202296_fork_test1(void)
{
	int32 lvar = 111;
	pid32 pid;

	u2023202296_printf("  [fork_test1] before fork: pid=%d &lvar=0x%x lvar=%d\n",
			   u2023202296_getpid(), (uint32)&lvar, lvar);
	pid = u2023202296_fork();
	if (pid == 0) {				/* child */
		lvar = 999;
		u2023202296_printf("  [fork_test1/CHILD ] pid=%d name=%s func=fork_test1 &lvar=0x%x lvar=%d\n",
				   u2023202296_getpid(),
				   proctab[u2023202296_getpid()].prname,
				   (uint32)&lvar, lvar);
		u2023202296_user_exit();
	} else {				/* parent */
		u2023202296_receive();		/* wait for the child to finish */
		u2023202296_printf("  [fork_test1/PARENT] pid=%d name=%s func=fork_test1 &lvar=0x%x lvar=%d (child=%d; lvar unchanged => isolated)\n",
				   u2023202296_getpid(),
				   proctab[u2023202296_getpid()].prname,
				   (uint32)&lvar, lvar, pid);
	}
}

/*------------------------------------------------------------------------
 * child_entry - new program image entered through exec()
 *------------------------------------------------------------------------
 */
void u2023202296_child_entry(int32 arg)
{
	int32 lvar = arg;

	u2023202296_printf("  [child_entry/EXEC ] pid=%d name=%s func=child_entry &lvar=0x%x arg=%d CPL=%d\n",
			   u2023202296_getpid(),
			   proctab[u2023202296_getpid()].prname,
			   (uint32)&lvar, arg, u2023202296_cpl());
	u2023202296_user_exit();
}

/*------------------------------------------------------------------------
 * fork_test2 - the child execs a different entry function.
 *------------------------------------------------------------------------
 */
void u2023202296_fork_test2(void)
{
	int32 lvar = 333;
	pid32 pid;

	u2023202296_printf("  [fork_test2] before fork: pid=%d &lvar=0x%x\n",
			   u2023202296_getpid(), (uint32)&lvar);
	pid = u2023202296_fork();
	if (pid == 0) {				/* child execs a new image */
		u2023202296_exec((void *)u2023202296_child_entry, 20,
				 "lab4_child", 1, 4242);
		u2023202296_printf("  [fork_test2/CHILD ] exec failed!\n");
		u2023202296_user_exit();
	} else {				/* parent */
		u2023202296_printf("  [fork_test2/PARENT] pid=%d name=%s func=fork_test2 &lvar=0x%x child=%d\n",
				   u2023202296_getpid(),
				   proctab[u2023202296_getpid()].prname,
				   (uint32)&lvar, pid);
		u2023202296_receive();		/* wait for the child */
	}
}

/*------------------------------------------------------------------------
 * heap_test - paged heap allocate/free, in the lab4 process AND a child
 *   (req 2.5.a).  The parent allocates four pages, frees two, and leaks the
 *   other two on purpose (allocated > freed); the forked child does the same
 *   in its own address space.  Neither frees everything, yet free_vproc
 *   reclaims every heap page when each process exits.
 *------------------------------------------------------------------------
 */
void u2023202296_heap_test(void)
{
	uint32 a, b, c;
	pid32 pid;

	u2023202296_printf("  [heap_test] pid=%d heap base=0x%x; allocating 3 blocks\n",
			   u2023202296_getpid(), (uint32)K4_HEAP_BASE);
	a = u2023202296_malloc(100);		/* 1 page */
	b = u2023202296_malloc(5000);		/* 2 pages */
	c = u2023202296_malloc(40);		/* 1 page */
	u2023202296_printf("  [heap_test] a=0x%x b=0x%x c=0x%x\n", a, b, c);

	*(int32 *)a = 0x1111;			/* prove the heap is usable */
	*(int32 *)b = 0x2222;
	*(int32 *)c = 0x3333;
	u2023202296_printf("  [heap_test] wrote/read back a=0x%x b=0x%x c=0x%x\n",
			   *(int32 *)a, *(int32 *)b, *(int32 *)c);

	u2023202296_dumpmap();			/* show heap pages in the map */

	u2023202296_free(b, 5000);		/* free 2 pages */
	u2023202296_printf("  [heap_test] freed b; a and c stay allocated (alloc>free)\n");

	pid = u2023202296_fork();
	if (pid == 0) {				/* child: its own paged heap */
		uint32 d = u2023202296_malloc(8000);	/* 2 pages */
		*(int32 *)d = 0x4444;
		u2023202296_printf("  [heap_test/CHILD  pid=%d] malloc d=0x%x (leaked; freed at exit)\n",
				   u2023202296_getpid(), d);
		u2023202296_user_exit();
	} else {
		u2023202296_receive();
		u2023202296_printf("  [heap_test/PARENT pid=%d] child done; a,c still leaked => freed when this process exits\n",
				   u2023202296_getpid());
	}
}

/*------------------------------------------------------------------------
 * stack_consume - recurse, holding a ~2KB frame per call, to drive the user
 *   stack below its initial 16KB allocation.  noinline + volatile + a post-
 *   recursion use keep the frame live so the compiler cannot fold it away.
 *------------------------------------------------------------------------
 */
static void __attribute__((noinline)) u2023202296_stack_consume(int32 depth)
{
	volatile char buf[2048];
	int32 i;

	for (i = 0; i < (int32)sizeof(buf); i += 512)
		buf[i] = (char)(depth + i);
	u2023202296_printf("  [stack_test] depth=%2d &buf=0x%x\n",
			   depth, (uint32)&buf[0]);
	if (depth > 0)
		u2023202296_stack_consume(depth - 1);
	buf[1] = buf[0];			/* use buf after the call (no TCO) */
}

/*------------------------------------------------------------------------
 * stack_test - force on-demand user-stack growth (req 2.5.b).  The recursion
 *   uses ~24KB of stack, more than the 16KB eager allocation, so the lower
 *   pages are mapped lazily by the page-fault handler (watch the [vm] grow
 *   lines), then the call chain unwinds normally.
 *------------------------------------------------------------------------
 */
void u2023202296_stack_test(void)
{
	u2023202296_printf("  [stack_test] pid=%d initial stack is 16KB; recursing to ~24KB\n",
			   u2023202296_getpid());
	u2023202296_stack_consume(11);
	u2023202296_printf("  [stack_test] recursion returned OK -> stack grew on demand\n");
}

/*------------------------------------------------------------------------
 * u2023202296_xsh_lab4 - the user-mode "lab4" shell command (CPL=3)
 *------------------------------------------------------------------------
 */
shellcmd u2023202296_xsh_lab4(int32 nargs, char *args[])
{
	int32 x;
	int32 i;

	u2023202296_printf("\n[lab4] xsh_lab4 running in USER mode: pid=%d name=%s CPL=%d\n",
			   u2023202296_getpid(),
			   proctab[u2023202296_getpid()].prname,
			   u2023202296_cpl());
	u2023202296_printf("[lab4] &x = 0x%x  (this local variable lives on the user stack)\n",
			   (uint32)&x);

	/* "lab4 1 hold": spin in user mode so the QEMU monitor's `info mem`	*/
	/* can be captured while this address space is the active CR3.		*/
	for (i = 1; i < nargs; i++) {
		if (args[i][0] == 'h') {
			volatile uint32 spin;
			u2023202296_printf("[lab4] holding in user mode for `info mem` ...\n");
			for (spin = 0; spin < 2500000000u; spin++)
				;
		}
	}

	if (nargs >= 2 && args[1][0] == '2') {
		u2023202296_printf("[lab4] mode 2: fork + exec\n");
		u2023202296_fork_test2();
	} else if (nargs >= 2 && args[1][0] == '3') {
		u2023202296_printf("[lab4] mode 3: paged heap alloc/free (req 2.5.a)\n");
		u2023202296_heap_test();
	} else if (nargs >= 2 && args[1][0] == '4') {
		u2023202296_printf("[lab4] mode 4: on-demand user-stack growth (req 2.5.b)\n");
		u2023202296_stack_test();
	} else {
		u2023202296_printf("[lab4] mode 1: fork only (child runs the same code)\n");
		u2023202296_fork_test1();
	}

	u2023202296_printf("[lab4] 2023202296 Li Gan\n");
	return 0;
}
