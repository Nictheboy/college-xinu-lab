#include <xinu.h>
#include <stdarg.h>
#include <string.h>

struct ctxsw_frame {
    uint32 edi;
    uint32 esi;
    uint32 ebp;
    uint32 esp;
    uint32 ebx;
    uint32 edx;
    uint32 ecx;
    uint32 eax;
    uint32 eflags;
    uint32 saved_ebp;
    uint32 ret_addr;
};

/* Obtain a new process ID */
static pid32 k2023202296_newpid(void) {
    uint32 i;
    static pid32 nextpid = 1;

    for (i = 0; i < NPROC; i++) {
        nextpid %= NPROC;
        if (proctab[nextpid].prstate == PR_FREE) {
            return nextpid++;
        } else {
            nextpid++;
        }
    }
    return (pid32) SYSERR;
}

/*------------------------------------------------------------------------
 *  k2023202296_fork  -  Fork the current process to create a child
 *------------------------------------------------------------------------
 */
syscall k2023202296_fork(void) {
    uint32 parent_esp;
    uint32 parent_ebp;
    uint32 reg_ebx, reg_esi, reg_edi;
    intmask mask;
    pid32 parent_pid;
    struct procent *parent_prptr;
    pid32 child_pid;
    uint32 parent_stack_len;
    char *child_saddr;
    uint32 parent_stack_base;
    uint32 child_stack_base;
    uint32 delta;
    uint32 copy_size;
    uint32 child_copy_start;
    uint32 child_ebp;
    uint32 child_EBP_caller;
    uint32 parent_stack_limit;
    uint32 curr_ebp;
    struct ctxsw_frame *cf;
    struct procent *child_prptr;

    // Get current EBP, ESP, and callee-saved registers (ebx, esi, edi) at function start
    asm volatile("movl %%esp, %0" : "=r"(parent_esp));
    asm volatile("movl %%ebp, %0" : "=r"(parent_ebp));
    asm volatile("movl %%ebx, %0" : "=r"(reg_ebx));
    asm volatile("movl %%esi, %0" : "=r"(reg_esi));
    asm volatile("movl %%edi, %0" : "=r"(reg_edi));

    mask = disable();

    parent_pid = currpid;
    parent_prptr = &proctab[parent_pid];

    child_pid = k2023202296_newpid();
    if (child_pid == SYSERR) {
        restore(mask);
        return SYSERR;
    }

    parent_stack_len = parent_prptr->prstklen;
    child_saddr = getstk(parent_stack_len);
    if (child_saddr == (char *)SYSERR) {
        restore(mask);
        return SYSERR;
    }

    parent_stack_base = (uint32)parent_prptr->prstkbase;
    child_stack_base = (uint32)child_saddr;
    delta = child_stack_base - parent_stack_base;

    // Copy parent's stack from parent_ebp + 4 upwards to the base
    copy_size = parent_stack_base - (parent_ebp + 4);
    child_copy_start = child_stack_base - copy_size;
    memcpy((void *)child_copy_start, (void *)(parent_ebp + 4), copy_size);

    // Calculate child's EBP and caller's EBP
    child_ebp = parent_ebp + delta;
    child_EBP_caller = *(uint32 *)parent_ebp + delta;

    // Walk and adjust the frame pointer (%ebp) chain on the child stack
    parent_stack_limit = parent_stack_base - parent_stack_len + 4;
    curr_ebp = child_EBP_caller;
    while (curr_ebp >= (parent_stack_limit + delta) && curr_ebp < child_stack_base) {
        uint32 *next_ebp_ptr = (uint32 *)curr_ebp;
        uint32 next_ebp = *next_ebp_ptr;
        if (next_ebp >= parent_stack_limit && next_ebp < parent_stack_base) {
            *next_ebp_ptr = next_ebp + delta;
            curr_ebp = next_ebp + delta;
        } else {
            break;
        }
    }

    // Set up ctxsw frame for the child process, preserving parent's registers
    cf = (struct ctxsw_frame *)(child_copy_start - sizeof(struct ctxsw_frame));
    cf->edi = reg_edi;
    cf->esi = reg_esi;
    cf->ebp = child_ebp;
    cf->esp = 0;
    cf->ebx = reg_ebx; // Preserve GOT pointer/register
    cf->edx = 0;
    cf->ecx = 0;
    cf->eax = 0; // Return value for child is 0
    cf->eflags = 0x00000200; // Interrupts enabled
    cf->saved_ebp = child_EBP_caller;
    cf->ret_addr = *(uint32 *)(parent_ebp + 4); // return address of fork()

    // Initialize child process table entry
    child_prptr = &proctab[child_pid];
    *child_prptr = *parent_prptr;

    child_prptr->prstate = PR_SUSP;
    child_prptr->prstkbase = (char *)child_stack_base;
    child_prptr->prstklen = parent_stack_len;
    child_prptr->prstkptr = (char *)cf;
    child_prptr->prparent = parent_pid;
    child_prptr->prsem = -1;
    child_prptr->prhasmsg = FALSE;

    prcount++;

    ready(child_pid);

    restore(mask);
    return child_pid;
}

/*------------------------------------------------------------------------
 *  k2023202296_exec  -  Overlay current process context and run new func
 *------------------------------------------------------------------------
 */
syscall k2023202296_exec(void *funcaddr, pri16 priority, char *name, uint32 nargs, ...) {
    intmask mask;
    struct procent *prptr;
    uint32 *saddr;
    va_list ap;
    uint32 args[8] = {0};
    uint32 i;

    mask = disable();

    prptr = &proctab[currpid];

    // Update PCB
    prptr->prprio = priority;
    prptr->prname[PNMLEN - 1] = NULLCH;
    for (i = 0; i < PNMLEN - 1 && (prptr->prname[i] = name[i]) != NULLCH; i++)
        ;

    // Reset the stack to base
    saddr = (uint32 *)prptr->prstkbase;
    *saddr = STACKMAGIC;

    // Parse the variable arguments
    va_start(ap, nargs);
    for (i = 0; i < nargs && i < 8; i++) {
        args[i] = va_arg(ap, uint32);
    }
    va_end(ap);

    // Push the arguments onto the stack
    for (i = nargs; i > 0; i--) {
        *--saddr = args[i - 1];
    }

    // Push return address placeholder (INITRET / userret)
    *--saddr = (uint32)INITRET;

    // Reset ESP, EBP, enable interrupts, and jump to the entry point
    asm volatile(
        "movl %0, %%esp\n\t"
        "movl %1, %%ebp\n\t"
        "pushl %2\n\t"
        "sti\n\t"
        "ret\n\t"
        :
        : "r"(saddr), "r"(prptr->prstkbase), "r"(funcaddr)
        : "memory"
    );

    return OK; // Never reached
}
