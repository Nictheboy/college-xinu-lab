/* Lab5.h - declarations for the keyboard + graphics-mode VGA console	*/
/*          (Lab 5, 2023202296 Li Gan)					*/

#ifndef _LAB5_H_
#define _LAB5_H_

/*------------------------------------------------------------------------
 *  Bochs / QEMU VBE (DISPI) framebuffer
 *------------------------------------------------------------------------
 */
#define K5_VBE_INDEX	0x01CE	/* DISPI index register port		*/
#define K5_VBE_DATA	0x01CF	/* DISPI data register port		*/

#define K5_VBE_ID	0	/* DISPI register indices		*/
#define K5_VBE_XRES	1
#define K5_VBE_YRES	2
#define K5_VBE_BPP	3
#define K5_VBE_ENABLE	4
#define K5_VBE_BANK	5
#define K5_VBE_VWIDTH	6
#define K5_VBE_VHEIGHT	7
#define K5_VBE_XOFF	8
#define K5_VBE_YOFF	9

#define K5_VBE_DISABLED		0x00
#define K5_VBE_ENABLED		0x01
#define K5_VBE_LFB_ENABLED	0x40
#define K5_VBE_NOCLEAR		0x80

#define K5_SCR_W	640	/* screen geometry (pixels)		*/
#define K5_SCR_H	480
#define K5_SCR_BPP	32

/* PCI configuration space (mechanism #1) for locating the LFB		*/
#define K5_PCI_ADDR	0xCF8
#define K5_PCI_DATA	0xCFC

/*------------------------------------------------------------------------
 *  Text terminal geometry (8x16 glyphs on a 640x480 surface)
 *------------------------------------------------------------------------
 */
#define K5_FONT_W	8
#define K5_FONT_H	16
#define K5_COLS		(K5_SCR_W / K5_FONT_W)	/* 80 columns		*/
#define K5_ROWS		(K5_SCR_H / K5_FONT_H)	/* 30 rows		*/

/* video state (system/Lab5_video.c)					*/
extern volatile uint32 *k2023202296_fb;		/* linear framebuffer	*/
extern uint32	k2023202296_fb_phys;		/* its physical base	*/
extern uint32	k2023202296_fb_pitch;		/* bytes per scanline	*/
extern int32	k2023202296_fb_ready;		/* video initialised?	*/

extern void	k2023202296_video_init(void);
extern void	k2023202296_fb_clear(uint32 rgb);
extern void	k2023202296_fb_fillrect(int32 x, int32 y, int32 w, int32 h,
					uint32 rgb);
extern void	k2023202296_map_framebuffer(uint32 *pgdir);

/*------------------------------------------------------------------------
 *  Text terminal (system/Lab5_term.c) and 8x16 font (Lab5_font.c)
 *------------------------------------------------------------------------
 */
extern const unsigned char k2023202296_font8x16[256][16];

extern int32	k2023202296_term_ready;		/* terminal usable?	*/

extern void	k2023202296_term_init(void);
extern void	k2023202296_term_putc(char c);	/* full control+ANSI	*/
extern void	k2023202296_term_clear(void);
extern void	k2023202296_term_cursor_show(void);
extern void	k2023202296_term_cursor_hide(void);
extern void	k2023202296_term_getpos(int32 *row, int32 *col);
extern void	k2023202296_term_setpos(int32 row, int32 col);
extern uint32	k2023202296_term_scrolls;	/* # of scrolls so far	*/

/*------------------------------------------------------------------------
 *  Keyboard + console device (system/Lab5_kbd.c)
 *------------------------------------------------------------------------
 */
#define K5_IBUFLEN	512		/* console input line buffer	*/

struct k2023202296_concblk {
	/* committed-character circular queue (read by kvdgetc)		*/
	char	ibuf[K5_IBUFLEN];
	int32	ihead;			/* next char to hand to a reader*/
	int32	itail;			/* next free slot		*/
	sid32	isem;			/* counts committed characters	*/

	/* the line currently being edited				*/
	char	line[K5_IBUFLEN];
	int32	llen;			/* characters in the line	*/
	int32	lpos;			/* cursor index within the line	*/
	int32	srow, scol;		/* screen anchor of line start	*/
	int32	anchored;		/* anchor (srow,scol) is valid	*/
	uint32	ascrolls;		/* term scroll count at anchor	*/

	/* modifier / decoder state					*/
	int32	shift;			/* Shift held			*/
	int32	ctrl;			/* Ctrl held			*/
	int32	caps;			/* CapsLock latched		*/
	int32	ext;			/* saw 0xE0 prefix		*/
	int32	echo;			/* echo input characters	*/
};

extern struct k2023202296_concblk k2023202296_contab[];

/* device-switch entry points (named in config/Configuration)		*/
extern devcall	k2023202296_kvdinit(struct dentry *devptr);
extern devcall	k2023202296_kvdgetc(struct dentry *devptr);
extern devcall	k2023202296_kvdread(struct dentry *devptr, char *buff,
				    int32 count);
extern devcall	k2023202296_kvdputc(struct dentry *devptr, char ch);
extern devcall	k2023202296_kvdwrite(struct dentry *devptr, char *buff,
				     int32 count);
extern devcall	k2023202296_kvdcontrol(struct dentry *devptr, int32 func,
				       int32 arg1, int32 arg2);
extern void	k2023202296_kbd_dispatch(void);	/* IRQ1 asm stub	*/
extern void	k2023202296_kbd_handler(void);	/* C interrupt handler	*/

/* serial mirror of console output (req 2.5.a)				*/
extern void	k2023202296_serial_putc(char c);

/* user-mode test command (shell/xsh_lab5.c)				*/
extern shellcmd	u2023202296_xsh_lab5(int32 nargs, char *args[]);

#endif /* _LAB5_H_ */
