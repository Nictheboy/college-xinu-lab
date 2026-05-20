#ifndef _LAB3_H_
#define _LAB3_H_

/* TSS Segment Struct */
struct __attribute__((packed)) k2023202296_tss_struct {
	uint32	link;
	uint32	esp0;
	uint32	ss0;
	uint32	esp1;
	uint32	ss1;
	uint32	esp2;
	uint32	ss2;
	uint32	cr3;
	uint32	eip;
	uint32	eflags;
	uint32	eax;
	uint32	ecx;
	uint32	edx;
	uint32	ebx;
	uint32	esp;
	uint32	ebp;
	uint32	esi;
	uint32	edi;
	uint32	es;
	uint32	cs;
	uint32	ss;
	uint32	ds;
	uint32	fs;
	uint32	gs;
	uint32	ldt;
	uint16	trap;
	uint16	iomap;
};

extern struct k2023202296_tss_struct k2023202296_tss;

/* Syscall numbers */
#define SYS_GETPID   0
#define SYS_PUTCHAR  1
#define SYS_CREATE   2
#define SYS_RESUME   3
#define SYS_SLEEP    4
#define SYS_EXIT     5

/* Kernel functions */
void k2023202296_lab3_init(void);
void k2023202296_set_syscall_vector(uint32 handler);
void k2023202296_syscall_handler(uint32 *regs);
void k2023202296_user_bootstrap(void);
pid32 k2023202296_create_user_proc_internal(void *funcaddr, uint32 ssize, char *name, uint32 *args);
void k2023202296_user_exit_internal(void);

/* User-mode functions */
uint32 u2023202296_syscall(uint32 num, uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4);
pid32 u2023202296_create_user_proc(void *funcaddr, uint32 ssize, pri16 priority, char *name, uint32 nargs, ...);
void u2023202296_user_exit(void);

/* Shell commands */
shellcmd u2023202296_xsh_lab3(int32 nargs, char *args[]);
void u2023202296_uptest(int32 a, int32 b);

/* Helper wrapper for creating user process */
pid32 k2023202296_create_user_proc(
	void		*funcaddr,	/* Address of the function	*/
	uint32		ssize,		/* Stack size in bytes		*/
	pri16		priority,	/* Process priority > 0		*/
	char		*name,		/* Name (for debugging)		*/
	uint32		nargs,		/* Number of args that follow	*/
	...
);

#endif /* _LAB3_H_ */
