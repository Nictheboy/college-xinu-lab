/* sleep.c - sleep sleepms delay_run */

#include <xinu.h>
#include <stdarg.h>

#define	MAXSECONDS	2147483		/* Max seconds per 32-bit msec	*/

/*------------------------------------------------------------------------
 *  sleep  -  Delay the calling process n seconds
 *------------------------------------------------------------------------
 */
syscall	sleep(
	  int32	delay		/* Time to delay in seconds	*/
	)
{
	if ( (delay < 0) || (delay > MAXSECONDS) ) {
		return SYSERR;
	}
	sleepms(1000*delay);
	return OK;
}

/*------------------------------------------------------------------------
 *  sleepms  -  Delay the calling process n milliseconds
 *------------------------------------------------------------------------
 */
syscall	sleepms(
	  int32	delay			/* Time to delay in msec.	*/
	)
{
	intmask	mask;			/* Saved interrupt mask		*/

	if (delay < 0) {
		return SYSERR;
	}

	if (delay == 0) {
		yield();
		return OK;
	}

	/* Delay calling process */

	mask = disable();
	if (insertd(currpid, sleepq, delay) == SYSERR) {
		restore(mask);
		return SYSERR;
	}

	proctab[currpid].prstate = PR_SLEEP;
	resched();
	restore(mask);
	return OK;
}

/*------------------------------------------------------------------------
 * delay_executor  -  (内部函数) 代理进程执行体，先休眠后执行目标函数
 *------------------------------------------------------------------------
 */
local void u2023202296_delay_executor(int seconds, void (*func)(int,int,int,int,int), 
                         int a1, int a2, int a3, int a4, int a5) 
{
    sleep(seconds); 
    (*func)(a1, a2, a3, a4, a5);
}

/*------------------------------------------------------------------------
 * delay_run  -  异步延时调用指定的函数，不阻塞当前进程
 *------------------------------------------------------------------------
 */
syscall k2023202296_delay_run(int seconds, void *func, int nargs, ...) 
{
    int32 i;
    va_list ap;
    int args[5] = {0};
    if (seconds < 0 || func == NULL)
        return SYSERR;
    va_start(ap, nargs);
    for (i = 0; i < nargs && i < 5; i++)
        args[i] = va_arg(ap, int);
    va_end(ap);
    pid32 pid = create(u2023202296_delay_executor, 1024, 20, "delay_proc", 7, 
                       seconds, func, args, args, args, args, args);
    if (pid == SYSERR)
        return SYSERR;
    resume(pid);
    return OK;
}
