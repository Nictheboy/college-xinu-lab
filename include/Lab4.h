#ifndef _LAB4_H_
#define _LAB4_H_

/*------------------------------------------------------------------------
 * Lab4.h - Paged (virtual) memory management for Xinu
 *
 *   Author: 2023202296 Li Gan
 *
 *   This lab turns Xinu into a paged-memory kernel.  Design summary:
 *
 *   - x86 32-bit 2-level paging (page directory + page tables), 4KB pages.
 *   - The low 16MB of physical memory is identity mapped into EVERY address
 *     space as the shared "kernel region" (PRESENT|RW|USER).  Kernel code,
 *     data and the kernel heap live here; user functions (which are linked
 *     into the kernel image) can therefore be fetched/executed at CPL3.
 *   - Physical memory above 16MB is NOT identity mapped.  It is a pool of
 *     free frames handed out by k2023202296_palloc().  The kernel reaches a
 *     high frame only through a small "dynamic mapping" window (kmap), the
 *     analogue of Linux high memory (req 2.6.b).
 *   - Each user (paged) process owns its own page directory; the user stack
 *     lives at a fixed high virtual range backed by per-process frames, so
 *     processes are isolated even though they share the kernel region.
 *------------------------------------------------------------------------
 */

/* Page-table entry / directory-entry flag bits */
#define K4_PRESENT   0x001
#define K4_RW        0x002
#define K4_USER      0x004

/* Address-space layout (virtual == linear, segments are flat) */
#define K4_LOWMEM_END   0x01000000u   /* 16MB: end of identity-mapped region */
#define K4_NLOWPT       4             /* page tables covering the low 16MB    */
#define K4_KMAP_PDI     4             /* PDE index of the dynamic-map window  */
#define K4_KMAP_VBASE   0x01000000u   /* VA base of the dynamic-map window    */
#define K4_KMAP_SLOTS   64            /* number of dynamic-map slots          */
#define K4_FRAME_BASE   0x01000000u   /* first allocatable physical frame     */

#define K4_USTACK_TOP   0x40000000u   /* top (exclusive) of the user stack    */
#define K4_USTACK_PDI   255           /* PDE of [0x3FC00000,0x40000000): stack */
#define K4_USTACK_SIZE  0x4000u       /* initial user stack: 16KB (eager)      */
#define K4_KSTK_SIZE    8192          /* kernel stack for a paged process      */

/* Demand-grown user stack (req 2.5.b): the 16KB above is only the initial    */
/* eager allocation; the stack grows downward one page at a time on a page    */
/* fault, capped at K4_USTACK_MAX so it never exceeds 4MB (req 2.6.d).         */
#define K4_USTACK_MAX   0x00200000u   /* 2MB hard cap on the user stack        */
#define K4_USTACK_LIMIT (K4_USTACK_TOP - K4_USTACK_MAX) /* 0x3FE00000: lowest  */

/* Paged user heap (req 2.5.a): grows UP from K4_HEAP_BASE, page granular,     */
/* kept strictly below the stack-growth region so the two never collide.       */
#define K4_HEAP_BASE    0x3FC00000u   /* bottom of PDE 255: user heap start    */
#define K4_HEAP_LIMIT   0x3FE00000u   /* heap must stay below the stack region */

/* Lab4 system-call numbers (Lab3 uses 0..5) */
#define SYS_FORK     10
#define SYS_EXEC     11
#define SYS_RECEIVE  12
#define SYS_PRINTS   13
#define SYS_MALLOC   14               /* paged heap allocate (req 2.5.a)       */
#define SYS_FREE     15               /* paged heap free                       */
#define SYS_DUMPMAP  16               /* dump caller's address-space map       */

#define K4_MAXARGS   8

/* Argument block passed from user-mode exec() to the kernel */
struct k2023202296_execargs {
	void   *func;
	int32   prio;
	char   *name;
	uint32  nargs;
	uint32  args[K4_MAXARGS];
};

/* The master kernel page directory (CR3 of every kernel-mode process) */
extern uint32 k2023202296_kernel_pgdir[1024];

/* statistics / logging toggle (defined in system/Lab4.c) */
extern uint32 k2023202296_frames_alloced;  /* count of frames currently in use */
extern int32  k2023202296_vm_verbose;       /* 1 => print [vm] alloc/free lines */

/* ---- core VM (system/Lab4.c) ---- */
extern void   k2023202296_vminit(void);                 /* enable paging      */
extern uint32 k2023202296_palloc(void);                 /* alloc a phys frame */
extern void   k2023202296_pfree(uint32 paddr);          /* free a phys frame  */
extern void  *k2023202296_kmap(uint32 paddr);           /* dynamic map        */
extern void   k2023202296_kunmap(void *va);             /* dynamic unmap      */
extern uint32 k2023202296_new_pgdir(void);              /* per-process pgdir  */
extern char  *k2023202296_build_uframe(uint32 *kstktop, uint32 frame[15]);
extern void   k2023202296_fork_trampoline(void);        /* asm: ctxsw -> iret */
extern pid32  k2023202296_newpid_lab4(void);
extern pid32  k2023202296_create_vproc(void *funcaddr, char *name,
				       int32 nargs, char *argv[]);
extern void   k2023202296_free_vproc(struct procent *prptr);
extern void   k2023202296_dump_mappings(pid32 pid);     /* "info mem" helper  */

/* ---- fork / exec / lab4 syscalls (system/Lab4_fork.c) ----
 * Named vfork/vexec to avoid clashing with Lab2's kernel-mode
 * k2023202296_fork/k2023202296_exec (declared in prototypes.h).      */
extern syscall k2023202296_vfork(uint32 *regs);
extern void    k2023202296_vexec(uint32 *regs);
extern int32   k2023202296_lab4_syscall(uint32 num, uint32 *regs);

/* ---- paged heap + demand-grown stack (system/Lab4_mem.c) ----
 * Optional/extension features 2.5.a (heap) and 2.5.b (stack growth).  */
extern uint32 k2023202296_heap_alloc(uint32 nbytes);     /* -> user VA or 0  */
extern void   k2023202296_heap_free(uint32 va, uint32 nbytes);
extern int32  k2023202296_grow_stack(uint32 cr2);        /* page-fault fixup  */
extern void   k2023202296_pgfault_init(void);            /* install vector 14 */
extern void   k2023202296_pgfault_entry(void);           /* asm: #PF entry    */
extern void   k2023202296_pgfault_handler(uint32 *regs, uint32 cr2);

/* ---- user-mode test code (shell/xsh_lab4.c) ---- */
extern shellcmd u2023202296_xsh_lab4(int32 nargs, char *args[]);
extern void     u2023202296_fork_test1(void);
extern void     u2023202296_fork_test2(void);
extern void     u2023202296_child_entry(int32 arg);
extern void     u2023202296_heap_test(void);             /* lab4 3: heap demo */
extern void     u2023202296_stack_test(void);            /* lab4 4: stack grow*/

#endif /* _LAB4_H_ */
