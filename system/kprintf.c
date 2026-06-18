/* kprintf.c -  kputc, kgetc, kprintf */

#include <xinu.h>
#include <stdarg.h>


/*------------------------------------------------------------------------
 * kputc  -  use polled I/O to write a character to the console
 *------------------------------------------------------------------------
 */
syscall kputc(byte c)	/* Character to write	*/
{
	struct	dentry	*devptr;
	volatile struct uart_csreg *csrptr;
	intmask	mask;

	/* Disable interrupts */
	mask = disable();

	/* Lab5 2023202296: CONSOLE is now the keyboard+VGA device, so the	*/
	/* polled serial path talks to the dedicated SERIAL (COM1) device.	*/
	devptr = (struct dentry *) &devtab[SERIAL];
	csrptr = (struct uart_csreg *)devptr->dvcsr;

	/* Fail if no console device was found */
	if (csrptr == NULL) {
		restore(mask);
		return SYSERR;
	}

	/* Repeatedly poll the device until it becomes nonbusy */
	while ((inb((uint32)&csrptr->lsr) & UART_LSR_THRE) == 0) {
		;
	}

	/* Write the character */
	outb((uint32)&csrptr->buffer, c);

	/* Honor CRLF - when writing NEWLINE also send CARRIAGE RETURN	*/
	if (c == '\n') {
		/* Poll until transmitter queue is empty */
		while ((inb((uint32)&csrptr->lsr) & UART_LSR_THRE) == 0) {
			;
		}
		outb((int)&csrptr->buffer, '\r');
	}

	/*Lab5 2023202296: Begin*/
	/* Mirror polled kernel output onto the VGA text terminal so the	*/
	/* boot banner, VERSION, [vm] logs and panics appear on screen	*/
	/* (the serial write above is kept as the 2.5a mirror).		*/
	k2023202296_term_putc(c);
	/*Lab5 2023202296: End*/

	restore(mask);
	return OK;
}

/*------------------------------------------------------------------------
 * kgetc - use polled I/O to read a character from the console serial line
 *------------------------------------------------------------------------
 */
syscall kgetc(void)
{
	int irmask;
	volatile struct uart_csreg *csrptr;
	byte c;
	struct	dentry	*devptr;
	intmask	mask;

	/* Disable interrupts */
	mask = disable();

	/* Lab5 2023202296: read the polled serial line from SERIAL (COM1).	*/
	devptr = (struct dentry *) &devtab[SERIAL];
	csrptr = (struct uart_csreg *)devptr->dvcsr;

	/* Fail if no console device was found */
	if (csrptr == NULL) {
		restore(mask);
		return SYSERR;
	}

	irmask = inb((uint32)&csrptr->ier);		/* Save UART interrupt state.   */
	outb((uint32)&csrptr->ier, 0);		/* Disable UART interrupts.     */

	/* wait for UART transmit queue to empty */

	while (0 == (inb((uint32)&csrptr->lsr) & UART_LSR_DR)) {
		; /* Do Nothing */
	}

	/* Read character from Receive Holding Register */

	c = inb((uint32)&csrptr->rbr);
	outb((uint32)&csrptr->ier, irmask);		/* Restore UART interrupts.     */

	restore(mask);
	return c;
}

extern	void	_doprnt(char *, va_list ap, int (*)(int));

/*------------------------------------------------------------------------
 * kprintf  -  use polled I/O to print formatted output on the console
 *------------------------------------------------------------------------
 */
syscall kprintf(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_doprnt(fmt, ap, (int (*)(int))kputc);
	va_end(ap);
	return OK;
}
