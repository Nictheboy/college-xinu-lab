/* Lab4_mem.c - paged user heap and demand-grown user stack (2023202296 Li Gan)
 *
 *   Optional / extension features of Lab4:
 *
 *   2.5.a  paged user heap
 *       k2023202296_heap_alloc / k2023202296_heap_free implement a page-
 *       granular heap that lives in the per-process user page table (PDE 255),
 *       just below the stack region.  Frames come from the same high-memory
 *       pool as everything else and are mapped into the calling process's
 *       address space through the kmap window.  Because the heap shares PDE
 *       255 with the stack, k2023202296_free_vproc already reclaims every heap
 *       frame when the process dies, so a process that allocates more than it
 *       frees still releases everything on exit.
 *
 *   2.5.b  demand-grown user stack
 *       The user stack is created with only a small eager allocation
 *       (K4_USTACK_SIZE).  When the program touches a lower, not-yet-mapped
 *       page inside the stack region, the CPU raises a page fault (vector 14);
 *       k2023202296_pgfault_handler maps a fresh frame there and returns, so
 *       the faulting instruction re-executes and succeeds.  Growth is capped
 *       at K4_USTACK_MAX (<=4MB, req 2.6.d); a fault outside the stack region
 *       is a genuine error and the process is killed.
 */

#include <xinu.h>
#include <string.h>

/*------------------------------------------------------------------------
 * k4m_invlpg - flush one TLB entry for the current address space
 *------------------------------------------------------------------------
 */
static void k4m_invlpg(uint32 va)
{
	asm volatile("invlpg (%0)" : : "r"(va) : "memory");
}

/*------------------------------------------------------------------------
 * k4m_log - same "[vm] ..." format used by system/Lab4.c
 *------------------------------------------------------------------------
 */
static void k4m_log(char *what, uint32 phys, char *purpose, uint32 va)
{
	if (!k2023202296_vm_verbose)
		return;
	kprintf("[vm] %-5s phys=0x%08X  %-26s VA=0x%08X\n",
		what, phys, purpose, va);
}

/*------------------------------------------------------------------------
 * k4m_user_pt - physical frame of the current process's user page table
 *		 (the table mapped by PDE K4_USTACK_PDI), or 0 if none
 *------------------------------------------------------------------------
 */
static uint32 k4m_user_pt(void)
{
	struct procent *prptr = &proctab[currpid];
	uint32 pd = prptr->prpgdir;
	uint32 *vpd, pde;

	if (pd == 0 || pd == (uint32)k2023202296_kernel_pgdir)
		return 0;
	vpd = (uint32 *)k2023202296_kmap(pd);
	pde = vpd[K4_USTACK_PDI];
	k2023202296_kunmap(vpd);
	if (!(pde & K4_PRESENT))
		return 0;
	return pde & ~0xFFFu;
}

/*------------------------------------------------------------------------
 * k2023202296_heap_alloc - allocate nbytes from the calling process heap
 *
 *   Rounds up to whole pages, allocates a frame for each, maps them at the
 *   process's current heap top (growing up), and returns the base VA.
 *   Returns 0 on failure.
 *------------------------------------------------------------------------
 */
uint32 k2023202296_heap_alloc(uint32 nbytes)
{
	intmask mask;
	struct procent *prptr = &proctab[currpid];
	uint32 upt, base, npages, i;
	uint32 *vpt;

	if (nbytes == 0)
		return 0;
	npages = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;

	mask = disable();
	upt = k4m_user_pt();
	if (upt == 0) {
		restore(mask);
		return 0;
	}

	base = prptr->prheaptop;
	if (base < K4_HEAP_BASE)
		base = K4_HEAP_BASE;
	if (base + npages * PAGE_SIZE > K4_HEAP_LIMIT) {	/* heap exhausted */
		restore(mask);
		return 0;
	}

	vpt = (uint32 *)k2023202296_kmap(upt);
	for (i = 0; i < npages; i++) {
		uint32 va = base + i * PAGE_SIZE;
		uint32 pti = (va >> 12) & 0x3FF;
		uint32 frame = k2023202296_palloc();
		void *z;

		if (frame == 0) {			/* unwind partial allocation */
			uint32 j;
			for (j = 0; j < i; j++) {
				uint32 v = base + j * PAGE_SIZE;
				uint32 p = (v >> 12) & 0x3FF;
				k2023202296_pfree(vpt[p] & ~0xFFFu);
				vpt[p] = 0;
				k4m_invlpg(v);
			}
			k2023202296_kunmap(vpt);
			restore(mask);
			return 0;
		}
		z = k2023202296_kmap(frame);
		memset(z, 0, PAGE_SIZE);
		k2023202296_kunmap(z);
		vpt[pti] = (frame & ~0xFFFu) | K4_PRESENT | K4_RW | K4_USER;
		k4m_invlpg(va);
		k4m_log("alloc", frame, "user heap page", va);
	}
	k2023202296_kunmap(vpt);
	prptr->prheaptop = base + npages * PAGE_SIZE;
	restore(mask);
	return base;
}

/*------------------------------------------------------------------------
 * k2023202296_heap_free - release a heap block previously returned by
 *			   k2023202296_heap_alloc (caller supplies its size)
 *------------------------------------------------------------------------
 */
void k2023202296_heap_free(uint32 va, uint32 nbytes)
{
	intmask mask;
	uint32 upt, npages, i;
	uint32 *vpt;

	if (nbytes == 0)
		return;
	npages = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;
	va &= ~0xFFFu;

	mask = disable();
	upt = k4m_user_pt();
	if (upt == 0) {
		restore(mask);
		return;
	}
	vpt = (uint32 *)k2023202296_kmap(upt);
	for (i = 0; i < npages; i++) {
		uint32 v = va + i * PAGE_SIZE;
		uint32 pti = (v >> 12) & 0x3FF;
		if (v >= K4_HEAP_BASE && v < K4_HEAP_LIMIT &&
		    (vpt[pti] & K4_PRESENT)) {
			uint32 frame = vpt[pti] & ~0xFFFu;
			k4m_log("free", frame, "user heap page", v);
			k2023202296_pfree(frame);
			vpt[pti] = 0;
			k4m_invlpg(v);
		}
	}
	k2023202296_kunmap(vpt);
	restore(mask);
}

/*------------------------------------------------------------------------
 * k2023202296_grow_stack - map the stack page(s) needed to cover a fault
 *
 *   Returns TRUE if at least one page was mapped (the fault is resolved),
 *   FALSE if the address is outside the growable stack region, the page is
 *   already present (a protection fault, not a missing page), or no frame
 *   is available.  Called from the page-fault handler with interrupts off.
 *------------------------------------------------------------------------
 */
int32 k2023202296_grow_stack(uint32 cr2)
{
	uint32 upt, va, page, pti;
	uint32 *vpt;
	int32 grew = 0;

	if (cr2 < K4_USTACK_LIMIT || cr2 >= K4_USTACK_TOP)
		return FALSE;			/* not the user stack -> real fault */
	upt = k4m_user_pt();
	if (upt == 0)
		return FALSE;

	page = cr2 & ~0xFFFu;
	vpt = (uint32 *)k2023202296_kmap(upt);
	pti = (page >> 12) & 0x3FF;
	if (vpt[pti] & K4_PRESENT) {		/* present already => protection fault */
		k2023202296_kunmap(vpt);
		return FALSE;
	}

	/* Map from the faulting page upward until the current stack bottom,	*/
	/* so any access pattern (e.g. a large local array) is fully covered.	*/
	for (va = page; va < K4_USTACK_TOP; va += PAGE_SIZE) {
		uint32 frame;
		void *z;
		pti = (va >> 12) & 0x3FF;
		if (vpt[pti] & K4_PRESENT)
			break;
		frame = k2023202296_palloc();
		if (frame == 0)
			break;			/* out of memory: caller kills proc */
		z = k2023202296_kmap(frame);
		memset(z, 0, PAGE_SIZE);
		k2023202296_kunmap(z);
		vpt[pti] = (frame & ~0xFFFu) | K4_PRESENT | K4_RW | K4_USER;
		k4m_invlpg(va);
		k4m_log("grow", frame, "user stack page (on demand)", va);
		grew = 1;
	}
	k2023202296_kunmap(vpt);
	return grew;
}

/*------------------------------------------------------------------------
 * k2023202296_pgfault_handler - C side of the vector-14 (#PF) handler
 *
 *   regs[] layout (built by the asm entry below):
 *     [0..7]  EDI ESI EBP ESP EBX EDX ECX EAX   (pushal)
 *     [8] DS  [9] ES  [10] error-code  [11] EIP  [12] CS
 *     [13] EFLAGS  [14] user-ESP  [15] user-SS
 *------------------------------------------------------------------------
 */
void k2023202296_pgfault_handler(uint32 *regs, uint32 cr2)
{
	uint32 err = regs[10];
	uint32 eip = regs[11];
	uint32 cs  = regs[12];

	/* A not-present fault (err bit0 == 0) from user mode (CPL3) inside the	*/
	/* stack region is the normal "grow the stack" case.			*/
	if ((cs & 3) == 3 && !(err & 0x1)) {
		if (k2023202296_grow_stack(cr2))
			return;			/* iret retries the faulting insn */
	}

	/* Anything else is a genuine fault. */
	kprintf("\n[vm] PAGE FAULT pid=%d cr2=0x%08X err=0x%X cs=0x%X eip=0x%08X\n",
		currpid, cr2, err, cs, eip);
	if ((cs & 3) == 3) {
		kprintf("[vm] terminating user process %d\n", currpid);
		kill(currpid);			/* never returns for currpid */
	}
	panic("Lab4: unrecoverable page fault in kernel mode");
}

/* Assembly entry for vector 14.  The CPU has already pushed an error code. */
__asm__(
".globl k2023202296_pgfault_entry\n"
"k2023202296_pgfault_entry:\n"
"    pushl %es\n"
"    pushl %ds\n"
"    pushal\n"
"    movl $0x10, %eax\n"		/* kernel data segment */
"    movw %ax, %ds\n"
"    movw %ax, %es\n"
"    movl %cr2, %eax\n"		/* faulting linear address */
"    movl %esp, %ecx\n"		/* &regs[0] (before pushing args) */
"    pushl %eax\n"			/* arg2 = cr2 */
"    pushl %ecx\n"			/* arg1 = regs */
"    call k2023202296_pgfault_handler\n"
"    addl $8, %esp\n"		/* drop the two C arguments */
"    popal\n"
"    popl %ds\n"
"    popl %es\n"
"    addl $4, %esp\n"		/* drop the CPU-pushed error code */
"    iret\n"
);

/*------------------------------------------------------------------------
 * k2023202296_pgfault_init - install the page-fault handler in the IDT
 *
 *   set_evec writes a DPL-0 interrupt gate with the kernel code selector,
 *   which is exactly what a CPU-generated exception needs.
 *------------------------------------------------------------------------
 */
void k2023202296_pgfault_init(void)
{
	set_evec(14, (uint32)k2023202296_pgfault_entry);
}
