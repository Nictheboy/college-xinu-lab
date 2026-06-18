/* Lab5_kbd.c - PS/2 keyboard driver + keyboard/VGA console device
 *              (Lab 5, 2023202296 Li Gan)
 *
 * The CONSOLE device is configured (config/Configuration) as type "kvd":
 * its lower half is this PS/2 keyboard driver (IRQ1, scancode set 1) and
 * its output is the graphics-mode text terminal (Lab5_term.c).  Command
 * output is also mirrored to the COM1 UART (req 2.5.a).
 *
 * Cooked-mode line editing (echo, Backspace, Tab, Enter) is performed in
 * the interrupt handler, exactly like the original tty driver, so the
 * unmodified shell read()/getc() path works unchanged.
 */

#include <xinu.h>

struct k2023202296_concblk k2023202296_contab[Nkvd];

/* keyboard I/O ports							*/
#define K5_KBD_DATA	0x60
#define K5_KBD_STAT	0x64
#define K5_KBD_OBF	0x01		/* output buffer full		*/
#define K5_KBD_AUX	0x20		/* byte came from the aux (mouse)*/

/* scancode-set-1 make codes for the modifier / lock keys		*/
#define SC_LSHIFT	0x2A
#define SC_RSHIFT	0x36
#define SC_CTRL		0x1D
#define SC_ALT		0x38
#define SC_CAPS		0x3A

/*------------------------------------------------------------------------
 *  US-layout scancode -> ASCII tables (unshifted and shifted).  Letters
 *  are stored lowercase in both; case is computed from Shift ^ CapsLock.
 *------------------------------------------------------------------------
 */
static const char k5_map[0x40] = {
	[0x01] = 0x1B,                                            /* Esc   */
	[0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
	[0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
	[0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
	[0x0E] = '\b', [0x0F] = '\t',
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
	[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
	[0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
	[0x1C] = '\n',
	[0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
	[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
	[0x2B] = '\\',
	[0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
	[0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
	[0x34] = '.', [0x35] = '/', [0x37] = '*', [0x39] = ' ',
};

static const char k5_shift[0x40] = {
	[0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
	[0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
	[0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
	[0x1A] = '{', [0x1B] = '}', [0x27] = ':', [0x28] = '"',
	[0x29] = '~', [0x2B] = '|', [0x33] = '<', [0x34] = '>',
	[0x35] = '?', [0x37] = '*',
};

/*------------------------------------------------------------------------
 * IRQ1 dispatch stub.  Modelled on ttydispatch.S: save state, send EOI to
 * the master 8259A, call the C handler, restore and iret.  Embedded as
 * file-scope inline assembly (the same technique used in Lab4.c).
 *------------------------------------------------------------------------
 */
__asm__(
".globl k2023202296_kbd_dispatch\n"
"k2023202296_kbd_dispatch:\n"
"    pushal\n"
"    pushfl\n"
"    cli\n"
"    movb $0x20, %al\n"		/* EOI ... */
"    outb %al, $0x20\n"		/* ... to master PIC command port	*/
"    call k2023202296_kbd_handler\n"
"    sti\n"
"    popfl\n"
"    popal\n"
"    iret\n"
);

/*------------------------------------------------------------------------
 * k2023202296_serial_putc - polled write of one byte to COM1 (the serial
 *	mirror; req 2.5.a).  CRLF is expanded for the serial line only.
 *------------------------------------------------------------------------
 */
void k2023202296_serial_putc(char c)
{
	volatile struct uart_csreg *u =
		(volatile struct uart_csreg *)devtab[SERIAL].dvcsr;

	while ((inb((uint32)&u->lsr) & UART_LSR_THRE) == 0)
		;
	outb((uint32)&u->buffer, c);
	if (c == '\n') {
		while ((inb((uint32)&u->lsr) & UART_LSR_THRE) == 0)
			;
		outb((uint32)&u->buffer, '\r');
	}
}

/*------------------------------------------------------------------------
 * k5_emit - send one character to both output surfaces: the VGA text
 *	terminal and the serial mirror.
 *------------------------------------------------------------------------
 */
static void k5_emit(char c)
{
	k2023202296_term_putc(c);
	k2023202296_serial_putc(c);
}

/*------------------------------------------------------------------------
 * k5_commit - push one finished input character into the reader queue and
 *	wake one waiting reader
 *------------------------------------------------------------------------
 */
static void k5_commit(struct k2023202296_concblk *c, char ch)
{
	c->ibuf[c->itail++] = ch;
	if (c->itail >= K5_IBUFLEN)
		c->itail = 0;
	signal(c->isem);
}

/*------------------------------------------------------------------------
 * k5_anchor_sync - keep the line-start anchor row correct across scrolls
 *	that happened while the line was being echoed
 *------------------------------------------------------------------------
 */
static void k5_anchor_sync(struct k2023202296_concblk *c)
{
	uint32 now = k2023202296_term_scrolls;
	c->srow -= (int32)(now - c->ascrolls);
	c->ascrolls = now;
}

/*------------------------------------------------------------------------
 * k5_linepos - screen (row,col) of logical line index k, walking from the
 *	anchor and honouring 80-column wrap and 8-column tab stops
 *------------------------------------------------------------------------
 */
static void k5_linepos(struct k2023202296_concblk *c, int32 k,
		       int32 *prow, int32 *pcol)
{
	int32 row = c->srow, col = c->scol, i;

	for (i = 0; i < k; i++) {
		if (c->line[i] == '\t') {
			do {
				if (++col >= K5_COLS) { col = 0; row++; }
			} while (col & 7);
		} else {
			if (++col >= K5_COLS) { col = 0; row++; }
		}
	}
	*prow = row;
	*pcol = col;
}

/*------------------------------------------------------------------------
 * k5_goto - move the visible cursor to logical line index k
 *------------------------------------------------------------------------
 */
static void k5_goto(struct k2023202296_concblk *c, int32 k)
{
	int32 row, col;

	k5_anchor_sync(c);
	k5_linepos(c, k, &row, &col);
	k2023202296_term_setpos(row, col);
}

/*------------------------------------------------------------------------
 * k5_redraw_tail - repaint line[from..llen) to the VGA terminal, optionally
 *	erasing one trailing leftover cell, then park the cursor at lpos
 *------------------------------------------------------------------------
 */
static void k5_redraw_tail(struct k2023202296_concblk *c, int32 from,
			   int32 shrunk)
{
	int32 row, col, i;

	k5_anchor_sync(c);
	k5_linepos(c, from, &row, &col);
	k2023202296_term_setpos(row, col);
	for (i = from; i < c->llen; i++)
		k2023202296_term_putc(c->line[i]);	/* VGA only	  */
	if (shrunk)
		k2023202296_term_putc(' ');		/* wipe old tail  */
	k5_goto(c, c->lpos);
}

/*------------------------------------------------------------------------
 * k5_edit - apply one decoded character to the line being edited.
 *	Runs in interrupt context with rescheduling deferred.  When the
 *	cursor is at the end of the line (the common case, including when no
 *	arrow key has been used) the fast inline-echo path is taken; cursor
 *	movement into the middle of the line switches to the repaint path.
 *------------------------------------------------------------------------
 */
static void k5_edit(struct k2023202296_concblk *c, char ch)
{
	int32 i;

	switch (ch) {

	case '\n':				/* Enter: commit the line  */
	case '\r':
		if (c->lpos < c->llen)		/* park cursor at line end */
			k5_goto(c, c->llen);
		for (i = 0; i < c->llen; i++)
			k5_commit(c, c->line[i]);
		k5_commit(c, '\n');
		if (c->echo)
			k5_emit('\n');
		c->llen = 0;
		c->lpos = 0;
		c->anchored = 0;
		break;

	case '\b':				/* Backspace / Ctrl-H	  */
	case 0x7F:				/* Delete			  */
		if (c->lpos > 0) {		/* delete char left of cursor */
			for (i = c->lpos - 1; i < c->llen - 1; i++)
				c->line[i] = c->line[i + 1];
			c->llen--;
			c->lpos--;
			if (c->echo) {
				if (c->lpos == c->llen) {	/* at end */
					k5_emit('\b');
					k5_emit(' ');
					k5_emit('\b');
				} else {		/* middle: repaint */
					k2023202296_serial_putc('\b');
					k5_redraw_tail(c, c->lpos, 1);
				}
			}
		}
		/* on an empty line a Backspace does nothing (req 2.3.f)  */
		break;

	case '\t':				/* Tab			  */
	default:
		if (ch == '\t' ||
		    ((unsigned char)ch >= 0x20 && (unsigned char)ch < 0x7F)) {
			if (c->llen >= K5_IBUFLEN - 2)
				break;
			if (!c->anchored) {	/* remember line start	  */
				int32 r, col;
				k2023202296_term_getpos(&r, &col);
				c->srow = r;
				c->scol = col;
				c->ascrolls = k2023202296_term_scrolls;
				c->anchored = 1;
			}
			for (i = c->llen; i > c->lpos; i--)	/* make room */
				c->line[i] = c->line[i - 1];
			c->line[c->lpos] = ch;
			c->llen++;
			if (c->lpos == c->llen - 1) {		/* append */
				if (c->echo)
					k5_emit(ch);
				c->lpos++;
			} else {				/* insert */
				if (c->echo)
					k2023202296_serial_putc(ch);
				c->lpos++;
				if (c->echo)
					k5_redraw_tail(c, c->lpos - 1, 0);
			}
		} else if ((unsigned char)ch >= 1 && (unsigned char)ch < 0x20) {
			/* other control char (Ctrl+letter): show ^X only  */
			if (c->echo) {
				k5_emit('^');
				k5_emit((char)(ch + 0x40));
			}
		}
		break;
	}
}

/*------------------------------------------------------------------------
 * k5_decode - translate a make code into a character, honouring Shift,
 *	CapsLock and Ctrl.  Returns 0 for keys that produce no character.
 *------------------------------------------------------------------------
 */
static char k5_decode(struct k2023202296_concblk *c, uint8 sc)
{
	char base;

	if (sc >= 0x40)
		return 0;
	base = k5_map[sc];
	if (base == 0)
		return 0;

	if (base >= 'a' && base <= 'z') {		/* a letter	  */
		int32 upper = c->shift ^ c->caps;	/* Shift xor Caps */
		char letter = upper ? (char)(base - 32) : base;
		if (c->ctrl)
			return (char)(base & 0x1F);	/* Ctrl+letter	  */
		return letter;
	}

	/* non-letter: Shift selects the alternate symbol		  */
	return c->shift ? (k5_shift[sc] ? k5_shift[sc] : base) : base;
}

/*------------------------------------------------------------------------
 * k5_extended - handle a key that followed the 0xE0 prefix.  Implements
 *	left/right cursor movement (req 2.5.c) plus Home, End and Delete.
 *------------------------------------------------------------------------
 */
#define SCE_LEFT	0x4B
#define SCE_RIGHT	0x4D
#define SCE_HOME	0x47
#define SCE_END		0x4F
#define SCE_DELETE	0x53

static void k5_extended(struct k2023202296_concblk *c, uint8 sc)
{
	int32 i;

	switch (sc) {
	case SCE_LEFT:				/* move cursor left	  */
		if (c->lpos > 0) {
			c->lpos--;
			k5_goto(c, c->lpos);
		}
		break;
	case SCE_RIGHT:				/* move cursor right	  */
		if (c->lpos < c->llen) {
			c->lpos++;
			k5_goto(c, c->lpos);
		}
		break;
	case SCE_HOME:				/* jump to line start	  */
		if (c->lpos != 0) {
			c->lpos = 0;
			k5_goto(c, c->lpos);
		}
		break;
	case SCE_END:				/* jump to line end	  */
		if (c->lpos != c->llen) {
			c->lpos = c->llen;
			k5_goto(c, c->lpos);
		}
		break;
	case SCE_DELETE:			/* delete char at cursor  */
		if (c->lpos < c->llen) {
			for (i = c->lpos; i < c->llen - 1; i++)
				c->line[i] = c->line[i + 1];
			c->llen--;
			if (c->echo)
				k5_redraw_tail(c, c->lpos, 1);
		}
		break;
	default:
		break;
	}
}

/*------------------------------------------------------------------------
 * k5_process - feed one raw scancode through the state machine
 *------------------------------------------------------------------------
 */
static void k5_process(struct k2023202296_concblk *c, uint8 sc)
{
	char ch;

	if (sc == 0xE0) {			/* extended-key prefix	  */
		c->ext = 1;
		return;
	}
	if (c->ext) {
		c->ext = 0;
		if (!(sc & 0x80))
			k5_extended(c, sc);
		return;
	}

	if (sc & 0x80) {			/* key release		  */
		uint8 mk = sc & 0x7F;
		if (mk == SC_LSHIFT || mk == SC_RSHIFT)
			c->shift = 0;
		else if (mk == SC_CTRL)
			c->ctrl = 0;
		return;
	}

	switch (sc) {				/* key press		  */
	case SC_LSHIFT:
	case SC_RSHIFT:	c->shift = 1;		return;
	case SC_CTRL:	c->ctrl = 1;		return;
	case SC_CAPS:	c->caps ^= 1;		return;	/* latch toggle	  */
	case SC_ALT:				return;
	case 0x45:				return;	/* NumLock	  */
	case 0x46:				return;	/* ScrollLock	  */
	}

	/* navigation / keypad keys that some emulators deliver without an	*/
	/* 0xE0 prefix (Home, End, arrows, Delete) -> editing actions		*/
	if (sc == SCE_HOME || sc == SCE_END || sc == SCE_LEFT ||
	    sc == SCE_RIGHT || sc == SCE_DELETE || sc == 0x48 || sc == 0x50) {
		k5_extended(c, sc);
		return;
	}

	ch = k5_decode(c, sc);
	if (ch)
		k5_edit(c, ch);
}

/*------------------------------------------------------------------------
 * k2023202296_kbd_handler - the C half of the IRQ1 handler.  Drains every
 *	pending byte from the controller and runs it through the decoder,
 *	with rescheduling deferred so signal() is safe in interrupt context.
 *------------------------------------------------------------------------
 */
void k2023202296_kbd_handler(void)
{
	struct k2023202296_concblk *c = &k2023202296_contab[0];
	uint8 status;

	resched_cntl(DEFER_START);
	while ((status = (uint8)inb(K5_KBD_STAT)) & K5_KBD_OBF) {
		uint8 sc = (uint8)inb(K5_KBD_DATA);
		if (status & K5_KBD_AUX)	/* mouse byte: discard	  */
			continue;
		k5_process(c, sc);
	}
	resched_cntl(DEFER_STOP);
}

/*========================================================================
 *  Device-switch entry points
 *======================================================================*/

/*------------------------------------------------------------------------
 * k2023202296_kvdinit - initialise the console control block, flush the
 *	keyboard controller and install the IRQ1 handler
 *------------------------------------------------------------------------
 */
devcall k2023202296_kvdinit(struct dentry *devptr)
{
	struct k2023202296_concblk *c = &k2023202296_contab[devptr->dvminor];

	c->ihead = c->itail = 0;
	c->isem  = semcreate(0);
	c->llen  = c->lpos = 0;
	c->srow  = c->scol = 0;
	c->shift = c->ctrl = c->caps = c->ext = 0;
	c->echo  = TRUE;

	/* drain any bytes the firmware left in the controller		*/
	while (inb(K5_KBD_STAT) & K5_KBD_OBF)
		(void)inb(K5_KBD_DATA);

	/* register the IRQ1 handler (also unmasks IRQ1 in the 8259A)	*/
	set_evec(devptr->dvirq, (uint32)devptr->dvintr);
	return OK;
}

/*------------------------------------------------------------------------
 * k2023202296_kvdgetc - return the next committed input character, blocking
 *------------------------------------------------------------------------
 */
devcall k2023202296_kvdgetc(struct dentry *devptr)
{
	struct k2023202296_concblk *c = &k2023202296_contab[devptr->dvminor];
	char ch;

	wait(c->isem);
	ch = c->ibuf[c->ihead++];
	if (c->ihead >= K5_IBUFLEN)
		c->ihead = 0;
	return (devcall)ch;
}

/*------------------------------------------------------------------------
 * k2023202296_kvdread - cooked read: return one edited line
 *------------------------------------------------------------------------
 */
devcall k2023202296_kvdread(struct dentry *devptr, char *buff, int32 count)
{
	int32 nread;
	char  ch;

	if (count < 0)
		return SYSERR;
	if (count == 0)
		return 0;

	ch = (char)k2023202296_kvdgetc(devptr);
	*buff++ = ch;
	nread = 1;
	while (nread < count && ch != '\n' && ch != '\r') {
		ch = (char)k2023202296_kvdgetc(devptr);
		*buff++ = ch;
		nread++;
	}
	return nread;
}

/*------------------------------------------------------------------------
 * k2023202296_kvdputc - write one character to the screen and serial line
 *------------------------------------------------------------------------
 */
devcall k2023202296_kvdputc(struct dentry *devptr, char ch)
{
	(void)devptr;
	k5_emit(ch);
	return OK;
}

/*------------------------------------------------------------------------
 * k2023202296_kvdwrite - write a buffer of characters
 *------------------------------------------------------------------------
 */
devcall k2023202296_kvdwrite(struct dentry *devptr, char *buff, int32 count)
{
	if (count < 0)
		return SYSERR;
	for (; count > 0; count--)
		k2023202296_kvdputc(devptr, *buff++);
	return OK;
}

/*------------------------------------------------------------------------
 * k2023202296_kvdcontrol - minimal control: echo on/off and char count
 *------------------------------------------------------------------------
 */
devcall k2023202296_kvdcontrol(struct dentry *devptr, int32 func,
			       int32 arg1, int32 arg2)
{
	struct k2023202296_concblk *c = &k2023202296_contab[devptr->dvminor];
	(void)arg1;
	(void)arg2;

	switch (func) {
	case 9:		c->echo = TRUE;  return OK;	/* TC_ECHO	  */
	case 10:	c->echo = FALSE; return OK;	/* TC_NOECHO	  */
	case 8:		return semcount(c->isem);	/* TC_ICHARS	  */
	default:	return OK;
	}
}
