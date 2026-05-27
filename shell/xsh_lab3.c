#include <xinu.h>
#include <stdarg.h>

/* Internal printf formatting helper */
extern void _fdoprnt(char *, va_list, int (*func) (int, int), int);

/* Private helper for user-mode printf */
static int u2023202296_sprntf(int acpp, int ac) {
	char **cpp = (char **)acpp;
	char c = (char)ac;
	return (*(*cpp)++ = c);
}

/* User-mode printf using system call */
void u2023202296_printf(char *fmt, ...) {
	char buf[256];
	va_list ap;
	char *s = buf;
	
	va_start(ap, fmt);
	_fdoprnt(fmt, ap, u2023202296_sprntf, (int)&s);
	va_end(ap);
	*s = '\0';
	
	char *p = buf;
	while (*p) {
		u2023202296_syscall(SYS_PUTCHAR, (uint32)*p, 0, 0, 0);
		p++;
	}
}

/* User-mode system call wrappers */
pid32 u2023202296_getpid(void) {
	return (pid32)u2023202296_syscall(SYS_GETPID, 0, 0, 0, 0);
}

status u2023202296_sleepms(uint32 ms) {
	return (status)u2023202296_syscall(SYS_SLEEP, ms, 0, 0, 0);
}

status u2023202296_resume(pid32 pid) {
	return (status)u2023202296_syscall(SYS_RESUME, (uint32)pid, 0, 0, 0);
}

/* User-mode process function */
void u2023202296_uptest(int32 a, int32 b) {
	pid32 pid = u2023202296_getpid();
	uint32 cs;
	
	/* Read CS register to verify privilege level (CPL = CS & 3) */
	asm volatile("movl %%cs, %0" : "=r"(cs));
	
	/* Step 1: Output process ID, name, 1st parameter and privilege mode */
	u2023202296_printf("proc=%d (%s) [CS=0x%X (CPL=%d)]: a=%d\n", 
		pid, proctab[pid].prname, cs, cs & 3, a);
	
	/* Step 2: Sleep for at least 1 time slice (e.g. 150 ms) */
	u2023202296_sleepms(150);
	
	/* Step 3: Output process ID, name, 2nd parameter and privilege mode */
	u2023202296_printf("proc=%d (%s) [CS=0x%X (CPL=%d)]: b=%d\n", 
		pid, proctab[pid].prname, cs, cs & 3, b);
}

/* User-mode shell command */
shellcmd u2023202296_xsh_lab3(int32 nargs, char *args[]) {
	pid32 p1, p2, p3;
	uint32 cs;
	
	asm volatile("movl %%cs, %0" : "=r"(cs));
	
	u2023202296_printf("\n--- Lab3: Privilege Levels and System Calls ---\n");
	u2023202296_printf("xsh_lab3 process running at [CS=0x%X (CPL=%d)]\n\n", cs, cs & 3);
	
	/* Create three user-mode child processes with different parameters */
	p1 = u2023202296_create_user_proc((void *)u2023202296_uptest, 8192, 20, "proc_test1", 2, 100, 200);
	p2 = u2023202296_create_user_proc((void *)u2023202296_uptest, 8192, 20, "proc_test2", 2, 300, 400);
	p3 = u2023202296_create_user_proc((void *)u2023202296_uptest, 8192, 20, "proc_test3", 2, 500, 600);
	
	if (p1 == SYSERR || p2 == SYSERR || p3 == SYSERR) {
		u2023202296_printf("Error: Failed to create user-mode processes\n");
		return SYSERR;
	}
	
	/* Resume the three child processes */
	u2023202296_resume(p1);
	u2023202296_resume(p2);
	u2023202296_resume(p3);
	
	/* Sleep to let children complete */
	u2023202296_sleepms(600);
	
	/* Output student ID and name */
	u2023202296_printf("\nStudent ID: 2023202296, Name: Li Gan (LiGan)\n");
	u2023202296_printf("----------------------------------------------\n\n");
	
	return OK;
}
