/* Lab4_fork.c - paged fork/exec and Lab4 system-call dispatch (2023202296) */

#include <xinu.h>
#include <stdarg.h>
#include <string.h>

/*
 * Layout of the trap frame that Lab3's syscall_entry leaves on the kernel
 * stack, as seen through the `regs` pointer handed to the dispatcher:
 *
 *   regs[0..7]  = EDI ESI EBP ESP EBX EDX ECX EAX   (pushal)
 *   regs[8]     = DS
 *   regs[9]     = ES
 *   regs[10]    = EIP    (user return address)
 *   regs[11]    = CS     (0x23, RPL=3)
 *   regs[12]    = EFLAGS
 *   regs[13]    = ESP    (user stack pointer)   - present because CPL changed
 *   regs[14]    = SS     (0x2b)
 */

/*------------------------------------------------------------------------
 * k2023202296_fork - duplicate the calling user process
 *
 *   The child gets its own page directory and a private copy of the user
 *   stack frames mapped at the SAME virtual addresses, so every pointer in
 *   the copied stack stays valid without fix-up.  The child resumes in user
 *   mode at the instruction after int $0x80 with eax = 0.
 *------------------------------------------------------------------------
 */
syscall k2023202296_vfork(uint32 *regs)
{
	intmask mask;
	pid32 ppid, cpid;
	struct procent *pp, *cp;
	char *kstk;
	uint32 cpd, cupt, ppd, pupt, pde, frame[15];
	uint32 *vpt;
	struct { int32 idx; uint32 pframe; } present[64];
	uint32 cframes[64];		/* child frames, for cleanup on failure */
	int32 np, i;

	mask = disable();
	ppid = currpid;
	pp = &proctab[ppid];

	cpid = k2023202296_newpid_lab4();
	kstk = getstk(pp->prstklen);
	if (cpid == SYSERR || kstk == (char *)SYSERR) {
		restore(mask);
		return SYSERR;
	}

	kprintf("\n[lab4] fork: parent pid=%d -> child pid=%d\n", ppid, cpid);

	/* new address space sharing the kernel region */
	cpd = k2023202296_new_pgdir();
	if (cpd == 0) {				/* out of physical frames */
		freestk(kstk, pp->prstklen);
		restore(mask);
		return SYSERR;
	}
	cupt = k2023202296_palloc();
	if (cupt == 0) {
		k2023202296_pfree(cpd);
		freestk(kstk, pp->prstklen);
		restore(mask);
		return SYSERR;
	}
	{
		void *z = k2023202296_kmap(cupt);
		memset(z, 0, PAGE_SIZE);
		k2023202296_kunmap(z);
	}
	{
		uint32 *vpd = (uint32 *)k2023202296_kmap(cpd);
		vpd[K4_USTACK_PDI] = (cupt & ~0xFFFu) | K4_PRESENT | K4_RW | K4_USER;
		k2023202296_kunmap(vpd);
	}
	kprintf("[vm] alloc phys=0x%08X  child page directory (CR3)\n", cpd);
	kprintf("[vm] alloc phys=0x%08X  child user page table\n", cupt);

	/* snapshot the parent's present user PTEs */
	ppd = pp->prpgdir;
	{
		uint32 *vpd = (uint32 *)k2023202296_kmap(ppd);
		pde = vpd[K4_USTACK_PDI];
		k2023202296_kunmap(vpd);
	}
	np = 0;
	if (pde & K4_PRESENT) {
		pupt = pde & ~0xFFFu;
		vpt = (uint32 *)k2023202296_kmap(pupt);
		for (i = 0; i < 1024; i++) {
			if (vpt[i] & K4_PRESENT) {
				present[np].idx = i;
				present[np].pframe = vpt[i] & ~0xFFFu;
				if (++np >= 64)
					break;
			}
		}
		k2023202296_kunmap(vpt);
	}

	/* copy each parent user page into a fresh child frame.  The parent    */
	/* page is readable directly at its VA (CR3 is still the parent's).    */
	for (i = 0; i < np; i++) {
		uint32 cf = k2023202296_palloc();
		uint32 va = ((uint32)K4_USTACK_PDI << 22) |
			    ((uint32)present[i].idx << 12);
		void *cv;
		if (cf == 0) {			/* out of frames: unwind child */
			int32 j;
			for (j = 0; j < i; j++)
				k2023202296_pfree(cframes[j]);
			k2023202296_pfree(cupt);
			k2023202296_pfree(cpd);
			freestk(kstk, pp->prstklen);
			restore(mask);
			return SYSERR;
		}
		cframes[i] = cf;
		cv = k2023202296_kmap(cf);
		memcpy(cv, (void *)va, PAGE_SIZE);	/* dynamic-map copy */
		k2023202296_kunmap(cv);
		{
			uint32 *pt = (uint32 *)k2023202296_kmap(cupt);
			pt[present[i].idx] = (cf & ~0xFFFu)
					     | K4_PRESENT | K4_RW | K4_USER;
			k2023202296_kunmap(pt);
		}
		kprintf("[vm] alloc phys=0x%08X  child %s page copy  VA=0x%08X\n",
			cf, (va >= K4_USTACK_LIMIT) ? "stack" : "heap ", va);
	}

	/* fabricate the child's kernel-stack iret frame (eax = 0) */
	for (i = 0; i < 15; i++)
		frame[i] = regs[i];
	frame[7] = 0;				/* child fork() returns 0 */

	prcount++;
	cp = &proctab[cpid];
	*cp = *pp;				/* inherit descriptors, name, ... */
	cp->prstate   = PR_SUSP;
	cp->prparent  = ppid;
	cp->prsem     = -1;
	cp->prhasmsg  = FALSE;
	cp->prstkbase = kstk;
	cp->prstklen  = pp->prstklen;
	cp->prstkptr  = k2023202296_build_uframe((uint32 *)kstk, frame);
	cp->prpgdir   = cpd;
	cp->prvm      = TRUE;
	cp->prisuser  = TRUE;
	cp->prusrstkbase = pp->prusrstkbase;
	cp->prusrstklen  = pp->prusrstklen;

	ready(cpid);
	restore(mask);
	return cpid;			/* parent fork() return value */
}

/*------------------------------------------------------------------------
 * k2023202296_exec - replace the calling process image with a new function
 *
 *   The current user stack frames are reused; the stack pointer is reset to
 *   the top and the new arguments are pushed.  The saved trap frame is
 *   rewritten so the syscall return irets straight into the new function.
 *------------------------------------------------------------------------
 */
void k2023202296_vexec(uint32 *regs)
{
	struct procent *prptr = &proctab[currpid];
	struct k2023202296_execargs *ea =
		(struct k2023202296_execargs *)regs[4];	/* arg1 = EBX */
	void *func;
	uint32 nargs, args[K4_MAXARGS];
	char namebuf[PNMLEN];
	uint32 *usp;
	int32 i;
	uint32 eap = (uint32)ea;

	/* Validate the argument block pointer: a legitimate `ea` is a local on	*/
	/* the caller's user stack.  Reject a wild pointer so a bad syscall	*/
	/* argument returns SYSERR instead of faulting in kernel mode.  The	*/
	/* lower bound is the whole growable stack region (req 2.5.b).		*/
	if (eap < K4_USTACK_LIMIT ||
	    eap > K4_USTACK_TOP - sizeof(struct k2023202296_execargs)) {
		regs[7] = SYSERR;
		return;
	}

	/* copy everything out of user memory before we overwrite the stack */
	func = ea->func;
	nargs = ea->nargs;
	if (nargs > K4_MAXARGS)
		nargs = K4_MAXARGS;
	for (i = 0; i < (int32)nargs; i++)
		args[i] = ea->args[i];
	namebuf[0] = NULLCH;
	/* The name string must live in the kernel region or the user stack;	*/
	/* otherwise leave the name unchanged rather than fault on a bad ptr.	*/
	if ((uint32)ea->name >= 0x1000 && (uint32)ea->name < K4_USTACK_TOP) {
		namebuf[PNMLEN - 1] = NULLCH;
		for (i = 0; i < PNMLEN - 1 &&
			    (namebuf[i] = ea->name[i]) != NULLCH; i++)
			;
	}

	kprintf("[lab4] exec: pid=%d now runs '%s' at 0x%08X\n",
		currpid, namebuf, (uint32)func);

	prptr->prprio = (pri16)ea->prio;
	prptr->prname[PNMLEN - 1] = NULLCH;
	for (i = 0; i < PNMLEN - 1 && (prptr->prname[i] = namebuf[i]) != NULLCH; i++)
		;
	prptr->prusrfuncaddr = func;

	/* reset the user stack (CR3 is this process: user pages are mapped) */
	usp = (uint32 *)K4_USTACK_TOP;
	for (i = (int32)nargs; i > 0; i--)
		*(--usp) = args[i - 1];
	*(--usp) = (uint32)u2023202296_user_exit;	/* return address */

	/* rewrite the trap frame so we iret into the new function */
	regs[10] = (uint32)func;		/* EIP */
	regs[13] = (uint32)usp;			/* user ESP */
	regs[7]  = 0;				/* EAX */
}

/*------------------------------------------------------------------------
 * k2023202296_lab4_syscall - dispatch Lab4-specific system calls
 *
 *   Returns TRUE if the call number was handled, FALSE otherwise.  Invoked
 *   from the default arm of Lab3's syscall handler.
 *------------------------------------------------------------------------
 */
int32 k2023202296_lab4_syscall(uint32 num, uint32 *regs)
{
	switch (num) {

	case SYS_FORK:
		regs[7] = (uint32)k2023202296_vfork(regs);
		return TRUE;

	case SYS_EXEC:
		k2023202296_vexec(regs);
		return TRUE;

	case SYS_RECEIVE:
		regs[7] = (uint32)receive();
		return TRUE;

	case SYS_PRINTS: {
		char *s = (char *)regs[4];	/* arg1 = EBX */
		uint32 n = regs[6];		/* arg2 = ECX */
		uint32 i;
		for (i = 0; i < n; i++)
			putc(CONSOLE, s[i]);
		regs[7] = OK;
		return TRUE;
	}

	case SYS_MALLOC:			/* paged heap alloc (req 2.5.a) */
		regs[7] = k2023202296_heap_alloc(regs[4]);	/* nbytes = EBX */
		return TRUE;

	case SYS_FREE:				/* paged heap free */
		k2023202296_heap_free(regs[4], regs[6]);	/* VA=EBX, n=ECX */
		regs[7] = OK;
		return TRUE;

	case SYS_DUMPMAP:			/* show caller's address space */
		k2023202296_dump_mappings(currpid);
		regs[7] = OK;
		return TRUE;

	default:
		return FALSE;
	}
}
