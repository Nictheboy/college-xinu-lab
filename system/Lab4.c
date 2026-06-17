/* Lab4.c - paged virtual memory core (2023202296 Li Gan)
 *
 *   vminit ............ build the master kernel page directory and enable
 *                       paging while the boot/null stack stays valid.
 *   palloc / pfree .... physical frame allocator over high memory.
 *   kmap / kunmap ..... dynamic (temporary) mapping window for high frames.
 *   new_pgdir ......... build a per-process page directory.
 *   create_vproc ...... create a paged user process (used by xsh_lab4).
 *   free_vproc ........ release every frame owned by a paged process.
 *   dump_mappings ..... print a process address-space map (a la "info mem").
 */

#include <xinu.h>
#include <stdarg.h>
#include <string.h>

/* ----- page-table storage (all in low memory, identity mapped) ----- */

uint32 k2023202296_kernel_pgdir[1024] __attribute__((aligned(4096)));
static uint32 k4_low_pt[K4_NLOWPT][1024] __attribute__((aligned(4096)));
static uint32 k4_kmap_pt[1024] __attribute__((aligned(4096)));
static uint32 k4_boot_pt[1024] __attribute__((aligned(4096)));

/* ----- physical frame allocator (bitmap over [K4_FRAME_BASE,k4_frame_end)) */

#define K4_MAXFRAMES  ((0x08000000u - K4_FRAME_BASE) / PAGE_SIZE)  /* up to 112MB */
static uint8 k4_framebm[(K4_MAXFRAMES + 7) / 8];
static uint32 k4_frame_end;       /* first physical address NOT in the pool   */
static uint32 k4_nframes;         /* number of frames in the pool             */
uint32 k2023202296_frames_alloced;/* statistics                               */

/* ----- dynamic-mapping window slot bitmap ----- */
static uint8 k4_kmap_used[K4_KMAP_SLOTS];

/* verbose logging of frame alloc/free and dynamic maps (for the report) */
int32 k2023202296_vm_verbose = 1;

/*------------------------------------------------------------------------
 * k4_invlpg - invalidate one TLB entry
 *------------------------------------------------------------------------
 */
static void k4_invlpg(uint32 va)
{
	asm volatile("invlpg (%0)" : : "r"(va) : "memory");
}

/*------------------------------------------------------------------------
 * k2023202296_kmap - temporarily map a physical frame and return its VA
 *------------------------------------------------------------------------
 */
void *k2023202296_kmap(uint32 paddr)
{
	intmask mask;
	int32 slot;

	mask = disable();
	for (slot = 0; slot < K4_KMAP_SLOTS; slot++) {
		if (!k4_kmap_used[slot]) {
			uint32 va = K4_KMAP_VBASE + slot * PAGE_SIZE;
			k4_kmap_used[slot] = 1;
			k4_kmap_pt[slot] = (paddr & ~0xFFFu) | K4_PRESENT | K4_RW;
			k4_invlpg(va);
			restore(mask);
			return (void *)(va + (paddr & 0xFFFu));
		}
	}
	restore(mask);
	return NULL;			/* out of slots (should not happen) */
}

/*------------------------------------------------------------------------
 * k2023202296_kunmap - release a dynamic mapping
 *------------------------------------------------------------------------
 */
void k2023202296_kunmap(void *va)
{
	intmask mask;
	uint32 v = ((uint32)va) & ~0xFFFu;
	int32 slot = (int32)((v - K4_KMAP_VBASE) / PAGE_SIZE);

	if (slot < 0 || slot >= K4_KMAP_SLOTS)
		return;
	mask = disable();
	k4_kmap_pt[slot] = 0;
	k4_kmap_used[slot] = 0;
	k4_invlpg(v);
	restore(mask);
}

/*------------------------------------------------------------------------
 * k2023202296_palloc - allocate one physical frame from high memory
 *------------------------------------------------------------------------
 */
uint32 k2023202296_palloc(void)
{
	intmask mask;
	uint32 i;

	mask = disable();
	for (i = 0; i < k4_nframes; i++) {
		if (!(k4_framebm[i >> 3] & (1 << (i & 7)))) {
			k4_framebm[i >> 3] |= (1 << (i & 7));
			k2023202296_frames_alloced++;
			restore(mask);
			return K4_FRAME_BASE + i * PAGE_SIZE;
		}
	}
	restore(mask);
	return 0;			/* out of memory */
}

/*------------------------------------------------------------------------
 * k2023202296_pfree - return a physical frame to the pool
 *------------------------------------------------------------------------
 */
void k2023202296_pfree(uint32 paddr)
{
	intmask mask;
	uint32 i;

	if (paddr < K4_FRAME_BASE || paddr >= k4_frame_end)
		return;
	i = (paddr - K4_FRAME_BASE) / PAGE_SIZE;
	mask = disable();
	if (k4_framebm[i >> 3] & (1 << (i & 7))) {
		k4_framebm[i >> 3] &= ~(1 << (i & 7));
		k2023202296_frames_alloced--;
	}
	restore(mask);
}

/*------------------------------------------------------------------------
 * k4_zero_frame - clear a freshly allocated frame (uses dynamic mapping)
 *------------------------------------------------------------------------
 */
static void k4_zero_frame(uint32 paddr)
{
	void *v = k2023202296_kmap(paddr);
	memset(v, 0, PAGE_SIZE);
	k2023202296_kunmap(v);
}

/*------------------------------------------------------------------------
 * k4_set_pde - write one entry of a page directory frame
 *------------------------------------------------------------------------
 */
static void k4_set_pde(uint32 pd_phys, int32 idx, uint32 tbl_phys, uint32 fl)
{
	uint32 *pd = (uint32 *)k2023202296_kmap(pd_phys);
	pd[idx] = (tbl_phys & ~0xFFFu) | fl;
	k2023202296_kunmap(pd);
}

/*------------------------------------------------------------------------
 * k4_set_pte - write one entry of a page table frame
 *------------------------------------------------------------------------
 */
static void k4_set_pte(uint32 pt_phys, int32 idx, uint32 frame_phys, uint32 fl)
{
	uint32 *pt = (uint32 *)k2023202296_kmap(pt_phys);
	pt[idx] = (frame_phys & ~0xFFFu) | fl;
	k2023202296_kunmap(pt);
}

/*------------------------------------------------------------------------
 * k2023202296_new_pgdir - allocate a page directory that shares the kernel
 *			   region with the master kernel page directory
 *------------------------------------------------------------------------
 */
uint32 k2023202296_new_pgdir(void)
{
	uint32 pd = k2023202296_palloc();
	uint32 *v;
	int32 i;

	if (pd == 0)
		return 0;
	v = (uint32 *)k2023202296_kmap(pd);
	for (i = 0; i < 1024; i++)
		v[i] = k2023202296_kernel_pgdir[i];	/* shared kernel PDEs */
	k2023202296_kunmap(v);
	return pd;
}

/*------------------------------------------------------------------------
 * k2023202296_build_uframe - lay out a process kernel stack so that the
 *	first context switch into it runs the trampoline, which performs an
 *	iret into user mode using frame[0..14].
 *
 *	frame[] (low->high) = EDI ESI EBP ESP EBX EDX ECX EAX  (pushal block)
 *			      DS ES                            (segment regs)
 *			      EIP CS EFLAGS USER_ESP USER_SS   (iret frame)
 *------------------------------------------------------------------------
 */
char *k2023202296_build_uframe(uint32 *kstktop, uint32 frame[15])
{
	uint32 *sp = kstktop;	/* highest word of the kernel stack */
	int32 i;

	*sp = STACKMAGIC;
	for (i = 14; i >= 0; i--)		/* trap frame (consumed by tramp) */
		*(--sp) = frame[i];
	*(--sp) = (uint32)k2023202296_fork_trampoline;	/* ctxsw "ret" target */
	*(--sp) = 0;				/* saved ebp (dummy)              */
	*(--sp) = 0x00000002;			/* eflags for ctxsw popfl (IF=0)  */
	for (i = 0; i < 8; i++)			/* ctxsw pushal block (dummy)     */
		*(--sp) = 0;
	return (char *)sp;
}

/* Assembly trampoline: ctxsw "returns" here, we iret into user mode. */
__asm__(
".globl k2023202296_fork_trampoline\n"
"k2023202296_fork_trampoline:\n"
"    popal\n"
"    popl %ds\n"
"    popl %es\n"
"    iret\n"
);

/*------------------------------------------------------------------------
 * k4_log - verbose VM logging helper
 *------------------------------------------------------------------------
 */
static void k4_log(char *what, uint32 phys, char *purpose, uint32 va)
{
	if (!k2023202296_vm_verbose)
		return;
	if (va != 0xFFFFFFFFu)
		kprintf("[vm] %-5s phys=0x%08X  %-22s VA=0x%08X\n",
			what, phys, purpose, va);
	else
		kprintf("[vm] %-5s phys=0x%08X  %-22s (paging structure)\n",
			what, phys, purpose);
}

/*------------------------------------------------------------------------
 * k2023202296_create_vproc - create a paged user process
 *
 *   funcaddr : user entry function (linked in the kernel image)
 *   name     : process name
 *   nargs    : argument count (argc)
 *   argv     : argument vector (pointers remain valid in the kernel region)
 *------------------------------------------------------------------------
 */
pid32 k2023202296_create_vproc(void *funcaddr, char *name, int32 nargs,
			       char *argv[])
{
	intmask mask;
	pid32 pid;
	struct procent *prptr;
	char *kstk;
	uint32 pd, upt, f, topframe;
	uint32 va, frame[15];
	int32 i, k, nf, pti, ntok;
	uint32 *tv;
	uint32 user_esp;
	uint32 sframes[K4_USTACK_SIZE / PAGE_SIZE];	/* for cleanup on failure */

	mask = disable();
	if (nargs < 1)
		nargs = 1;
	ntok = nargs;
	nf = K4_USTACK_SIZE / PAGE_SIZE;

	pid = k2023202296_newpid_lab4();
	kstk = getstk(K4_KSTK_SIZE);
	if (pid == SYSERR || kstk == (char *)SYSERR) {
		restore(mask);
		return SYSERR;
	}

	kprintf("\n[lab4] creating paged user process '%s' (pid=%d)\n", name, pid);

	/* 1. page directory (CR3) */
	pd = k2023202296_new_pgdir();
	if (pd == 0) {				/* out of physical frames */
		freestk(kstk, K4_KSTK_SIZE);
		restore(mask);
		return SYSERR;
	}
	k4_log("alloc", pd, "page directory (CR3)", 0xFFFFFFFFu);

	/* 2. user page table for PDE 255 (covers VA 0x3FC00000-0x40000000) */
	upt = k2023202296_palloc();
	if (upt == 0) {
		k2023202296_pfree(pd);
		freestk(kstk, K4_KSTK_SIZE);
		restore(mask);
		return SYSERR;
	}
	k4_zero_frame(upt);
	k4_set_pde(pd, K4_USTACK_PDI, upt, K4_PRESENT | K4_RW | K4_USER);
	k4_log("alloc", upt, "user page table", 0xFFFFFFFFu);
	{	/* Demonstrate dynamic mapping: this frame lives above the	*/
		/* 16MB identity-mapped region and is only reachable through	*/
		/* the kmap window (the "high memory" mechanism, req 2.6.b).	*/
		/* Note phys != VA, so this is genuinely a dynamic mapping.	*/
		void *kv = k2023202296_kmap(upt);
		kprintf("[vm] dynamic-map: high frame phys=0x%08X (>=16MB, NOT "
			"identity-mapped) reached at kmap VA=0x%08X\n",
			upt, (uint32)kv);
		k2023202296_kunmap(kv);
	}

	/* 3. user stack frames (mapped at the top of PDE 255) */
	topframe = 0;
	for (k = 0; k < nf; k++) {
		f = k2023202296_palloc();
		if (f == 0) {			/* out of frames: unwind */
			for (i = 0; i < k; i++)
				k2023202296_pfree(sframes[i]);
			k2023202296_pfree(upt);
			k2023202296_pfree(pd);
			freestk(kstk, K4_KSTK_SIZE);
			restore(mask);
			return SYSERR;
		}
		sframes[k] = f;
		va = K4_USTACK_TOP - (k + 1) * PAGE_SIZE;
		pti = (va >> 12) & 0x3FF;
		k4_zero_frame(f);		/* dynamic mapping in action */
		k4_set_pte(upt, pti, f, K4_PRESENT | K4_RW | K4_USER);
		k4_log("alloc", f, "user stack page", va);
		if (k == 0)
			topframe = f;		/* frame backing VA 0x3FFFF000 */
	}

	/* 4. push argc, argv, return address onto the top stack frame.        */
	/*    The child reads them at its own high VA; argv entries point into */
	/*    the kernel region (shell tokbuf) which is mapped for the child.  */
	tv = (uint32 *)k2023202296_kmap(topframe);
	tv[1023] = (uint32)argv;			/* VA 0x3FFFFFFC : argv  */
	tv[1022] = (uint32)ntok;			/* VA 0x3FFFFFF8 : argc  */
	tv[1021] = (uint32)u2023202296_user_exit;	/* VA 0x3FFFFFF4 : retpc */
	k2023202296_kunmap(tv);
	user_esp = K4_USTACK_TOP - 12;

	/* 5. fabricate the kernel-stack frame that irets into user mode */
	for (i = 0; i < 15; i++)
		frame[i] = 0;
	frame[8]  = 0x2b;		/* DS  (user data, DPL3) */
	frame[9]  = 0x2b;		/* ES                    */
	frame[10] = (uint32)funcaddr;	/* EIP                   */
	frame[11] = 0x23;		/* CS  (user code, DPL3) */
	frame[12] = 0x00000200;		/* EFLAGS (IF=1)         */
	frame[13] = user_esp;		/* user ESP              */
	frame[14] = 0x2b;		/* SS  (user data, DPL3) */

	prcount++;
	prptr = &proctab[pid];
	prptr->prstate   = PR_SUSP;
	prptr->prprio    = SHELL_CMDPRIO;
	prptr->prstkbase = kstk;
	prptr->prstklen  = K4_KSTK_SIZE;
	prptr->prstkptr  = k2023202296_build_uframe((uint32 *)kstk, frame);
	prptr->prname[PNMLEN - 1] = NULLCH;
	for (i = 0; i < PNMLEN - 1 && (prptr->prname[i] = name[i]) != NULLCH; i++)
		;
	prptr->prsem     = -1;
	prptr->prparent  = (pid32)getpid();
	prptr->prhasmsg  = FALSE;
	prptr->prdesc[0] = CONSOLE;
	prptr->prdesc[1] = CONSOLE;
	prptr->prdesc[2] = CONSOLE;
	prptr->prpgdir       = pd;
	prptr->prvm          = TRUE;
	prptr->prisuser      = TRUE;
	prptr->prusrfuncaddr = funcaddr;
	prptr->prusrstkbase  = (char *)K4_USTACK_TOP;
	prptr->prusrstklen   = K4_USTACK_SIZE;
	prptr->prheaptop     = K4_HEAP_BASE;	/* empty paged heap (req 2.5.a) */

	k2023202296_dump_mappings(pid);

	restore(mask);
	return pid;
}

/*------------------------------------------------------------------------
 * k2023202296_free_vproc - free every frame owned by a paged process
 *------------------------------------------------------------------------
 */
void k2023202296_free_vproc(struct procent *prptr)
{
	uint32 pd = prptr->prpgdir;
	uint32 *vpd, *vpt;
	uint32 pde, upt, frame, va;
	int32 i;

	if (pd == 0 || pd == (uint32)k2023202296_kernel_pgdir)
		return;

	kprintf("[lab4] releasing address space of '%s' (pid=%d)\n",
		prptr->prname, (int)(prptr - proctab));

	/* read the user PDE */
	vpd = (uint32 *)k2023202296_kmap(pd);
	pde = vpd[K4_USTACK_PDI];
	k2023202296_kunmap(vpd);

	if (pde & K4_PRESENT) {
		upt = pde & ~0xFFFu;
		vpt = (uint32 *)k2023202296_kmap(upt);
		for (i = 0; i < 1024; i++) {
			if (vpt[i] & K4_PRESENT) {
				frame = vpt[i] & ~0xFFFu;
				va = ((uint32)K4_USTACK_PDI << 22) |
				     ((uint32)i << 12);
				/* a page below the stack region is heap; on    */
				/* exit we free it whether the user freed it or  */
				/* not, so alloc>free still leaks nothing (2.5a) */
				k4_log("free", frame,
				       (va >= K4_USTACK_LIMIT) ?
					   "user stack page" : "user heap page",
				       va);
				k2023202296_pfree(frame);
			}
		}
		k2023202296_kunmap(vpt);
		k4_log("free", upt, "user page table", 0xFFFFFFFFu);
		k2023202296_pfree(upt);
	}
	k4_log("free", pd, "page directory (CR3)", 0xFFFFFFFFu);
	k2023202296_pfree(pd);
}

/*------------------------------------------------------------------------
 * k2023202296_dump_mappings - print a process's present mappings ("info mem")
 *------------------------------------------------------------------------
 */
void k2023202296_dump_mappings(pid32 pid)
{
	struct procent *prptr = &proctab[pid];
	uint32 pd = prptr->prpgdir;
	uint32 *vpd, *vpt;
	int32 pdi, pti;
	uint32 runstart = 0, runphys = 0, runlen = 0, runfl = 0;

	if (pd == 0)
		return;
	kprintf("[lab4] address-space map of pid=%d (CR3=0x%08X):\n", pid, pd);
	kprintf("       %-12s %-12s %-8s %s\n", "VA-start", "VA-end", "size", "flags(U/RW)");

	vpd = (uint32 *)k2023202296_kmap(pd);
	for (pdi = 0; pdi < 1024; pdi++) {
		uint32 pde = vpd[pdi];
		if (!(pde & K4_PRESENT))
			continue;
		vpt = (uint32 *)k2023202296_kmap(pde & ~0xFFFu);
		for (pti = 0; pti < 1024; pti++) {
			uint32 pte = vpt[pti];
			uint32 va = ((uint32)pdi << 22) | ((uint32)pti << 12);
			uint32 fl = pte & (K4_USER | K4_RW);
			if (pte & K4_PRESENT) {
				if (runlen && runphys + runlen == (pte & ~0xFFFu)
				    && runfl == fl
				    && runstart + runlen == va) {
					runlen += PAGE_SIZE;
				} else {
					if (runlen)
						kprintf("       0x%08X   0x%08X   %4dK   %s%s\n",
							runstart, runstart + runlen - 1,
							runlen / 1024,
							(runfl & K4_USER) ? "U" : "S",
							(runfl & K4_RW) ? "+RW" : "+RO");
					runstart = va;
					runphys = pte & ~0xFFFu;
					runlen = PAGE_SIZE;
					runfl = fl;
				}
			}
		}
		k2023202296_kunmap(vpt);
	}
	if (runlen)
		kprintf("       0x%08X   0x%08X   %4dK   %s%s\n",
			runstart, runstart + runlen - 1, runlen / 1024,
			(runfl & K4_USER) ? "U" : "S",
			(runfl & K4_RW) ? "+RW" : "+RO");
	k2023202296_kunmap(vpd);
}

/*------------------------------------------------------------------------
 * k2023202296_newpid_lab4 - obtain a free process slot
 *------------------------------------------------------------------------
 */
pid32 k2023202296_newpid_lab4(void)
{
	uint32 i;
	static pid32 nextpid = 1;

	for (i = 0; i < NPROC; i++) {
		nextpid %= NPROC;
		if (proctab[nextpid].prstate == PR_FREE)
			return nextpid++;
		nextpid++;
	}
	return (pid32)SYSERR;
}

/*------------------------------------------------------------------------
 * k2023202296_vminit - build the kernel page directory and enable paging
 *
 *   Called at the very end of sysinit, before interrupts are enabled and
 *   before any process is created.  Because the boot/null stack lives at
 *   the top of physical RAM, the PDE that contains it is identity mapped
 *   too, so the stack stays valid the instant paging turns on.
 *------------------------------------------------------------------------
 */
void k2023202296_vminit(void)
{
	int32 i, j;
	uint32 top, boot_pdi, boot_base, pdp;
	struct memblk *cur, *prev;
	uint32 newtotal;

	/* 1. identity map the low 16MB (kernel region, USER accessible) */
	for (j = 0; j < K4_NLOWPT; j++)
		for (i = 0; i < 1024; i++)
			k4_low_pt[j][i] =
			    ((uint32)(j * 1024 + i) * PAGE_SIZE)
			    | K4_PRESENT | K4_RW | K4_USER;

	/* 2. dynamic-map window starts empty */
	for (i = 0; i < 1024; i++)
		k4_kmap_pt[i] = 0;

	/* 3. identity map the 4MB region that holds the boot/null stack */
	top = (uint32)maxheap;
	boot_pdi = (top - 1) >> 22;
	boot_base = boot_pdi << 22;
	for (i = 0; i < 1024; i++)
		k4_boot_pt[i] = (boot_base + (uint32)i * PAGE_SIZE)
				| K4_PRESENT | K4_RW;

	/* 4. assemble the master page directory */
	for (i = 0; i < 1024; i++)
		k2023202296_kernel_pgdir[i] = 0;
	for (j = 0; j < K4_NLOWPT; j++)
		k2023202296_kernel_pgdir[j] =
		    ((uint32)k4_low_pt[j]) | K4_PRESENT | K4_RW | K4_USER;
	k2023202296_kernel_pgdir[K4_KMAP_PDI] =
	    ((uint32)k4_kmap_pt) | K4_PRESENT | K4_RW;
	k2023202296_kernel_pgdir[boot_pdi] =
	    ((uint32)k4_boot_pt) | K4_PRESENT | K4_RW;

	/* 5. frame allocator owns [16MB, boot_base).  Clamp to the size of	*/
	/*    the static bitmap so that running with more than 128MB of RAM	*/
	/*    (e.g. qemu -m 256) cannot overflow k4_framebm.			*/
	k4_frame_end = boot_base;
	k4_nframes = (k4_frame_end - K4_FRAME_BASE) / PAGE_SIZE;
	if (k4_nframes > K4_MAXFRAMES) {
		k4_nframes = K4_MAXFRAMES;
		k4_frame_end = K4_FRAME_BASE + k4_nframes * PAGE_SIZE;
	}
	for (i = 0; i < (int32)sizeof(k4_framebm); i++)
		k4_framebm[i] = 0;

	/* 6. trim the kernel heap so it never exceeds the identity-mapped 16MB */
	prev = &memlist;
	cur = memlist.mnext;
	newtotal = 0;
	while (cur != NULL) {
		uint32 start = (uint32)cur;
		uint32 end = start + cur->mlength;
		if (start >= K4_LOWMEM_END) {
			prev->mnext = NULL;
			break;
		} else if (end > K4_LOWMEM_END) {
			cur->mlength = K4_LOWMEM_END - start;
			newtotal += cur->mlength;
			cur->mnext = NULL;
			break;
		} else {
			newtotal += cur->mlength;
			prev = cur;
			cur = cur->mnext;
		}
	}
	memlist.mlength = newtotal;

	/* null process and the master page directory both use kernel_pgdir */
	proctab[NULLPROC].prpgdir = (uint32)k2023202296_kernel_pgdir;
	proctab[NULLPROC].prvm = FALSE;

	kprintf("[vm] paging: kernel region 0x00000000-0x00FFFFFF identity (U+RW)\n");
	kprintf("[vm] dynamic-map window 0x%08X-0x%08X (%d slots)\n",
		K4_KMAP_VBASE, K4_KMAP_VBASE + K4_KMAP_SLOTS * PAGE_SIZE - 1,
		K4_KMAP_SLOTS);
	kprintf("[vm] frame pool 0x%08X-0x%08X (%d frames, %d MB)\n",
		K4_FRAME_BASE, k4_frame_end - 1, k4_nframes,
		(k4_nframes * PAGE_SIZE) >> 20);
	kprintf("[vm] boot/null stack region 0x%08X-0x%08X identity\n",
		boot_base, boot_base + 0x3FFFFF);

	/* 7. load CR3 and turn on paging */
	pdp = (uint32)k2023202296_kernel_pgdir;
	asm volatile(
		"movl %0, %%cr3\n\t"
		"movl %%cr0, %%eax\n\t"
		"orl  $0x80000000, %%eax\n\t"
		"movl %%eax, %%cr0\n\t"
		: : "r"(pdp) : "eax", "memory");

	kprintf("[vm] paging enabled (CR0.PG=1, CR3=0x%08X)\n", pdp);

	/* 8. install the page-fault handler so the user stack can grow on	*/
	/*    demand (req 2.5.b) and so a bad access is reported, not silent.	*/
	k2023202296_pgfault_init();
	kprintf("[vm] page-fault handler installed: user stack auto-grows to "
		"%dKB, heap 0x%08X-0x%08X\n\n",
		K4_USTACK_MAX >> 10, K4_HEAP_BASE, K4_HEAP_LIMIT - 1);
}
