/* Lab5_term.c - text terminal on the VBE framebuffer (Lab 5, 2023202296)
 *
 * A double-buffered 80x30 text console drawn with an 8x16 bitmap font.
 * All drawing happens in a RAM back-buffer (k5_back) and is then blitted
 * to the linear framebuffer one cell at a time, so the visible screen
 * never tears and scrolling is a fast RAM memmove plus a single blit.
 *
 * term_putc() understands the ordinary control characters (\n \r \t \b)
 * and a useful subset of ANSI/VT100 escape sequences (SGR colours, clear
 * screen, cursor home).  That makes the existing CONSOLE_RESET string and
 * the red XINU banner render correctly with no change to the callers.
 */

#include <xinu.h>

int32  k2023202296_term_ready = 0;
uint32 k2023202296_term_scrolls = 0;	/* incremented on every scroll	*/

/* RAM back-buffer: the authoritative pixel image of the screen.		*/
static uint32 k5_back[K5_SCR_W * K5_SCR_H];

/* Cursor position (cell coordinates) and current colours.		*/
static int32  k5_row = 0, k5_col = 0;
static uint32 k5_fg  = 0x00AAAAAA;	/* default light grey		*/
static uint32 k5_bg  = 0x00000000;	/* default black		*/

/* ANSI parser state							*/
static int32  k5_fg_idx = -1;		/* -1 = default colour		*/
static int32  k5_bg_idx = -1;
static int32  k5_bold   = 0;
static int32  k5_esc    = 0;		/* 0 normal, 1 saw ESC, 2 in CSI*/
static int32  k5_params[8];
static int32  k5_nparam = 0;

/* Standard 16-colour palette (normal then bright)			*/
static const uint32 k5_pal_normal[8] = {
	0x00000000, 0x00AA0000, 0x0000AA00, 0x00AA5500,
	0x000000AA, 0x00AA00AA, 0x0000AAAA, 0x00AAAAAA
};
static const uint32 k5_pal_bright[8] = {
	0x00555555, 0x00FF5555, 0x0055FF55, 0x00FFFF55,
	0x005555FF, 0x00FF55FF, 0x0055FFFF, 0x00FFFFFF
};

/*------------------------------------------------------------------------
 * k5_color_of - map an ANSI colour index (with the bold flag) to RGB
 *------------------------------------------------------------------------
 */
static uint32 k5_color_of(int32 idx, int32 bold, int32 isfg)
{
	if (idx < 0)				/* default			*/
		return isfg ? (bold ? 0x00FFFFFF : 0x00AAAAAA) : 0x00000000;
	return bold ? k5_pal_bright[idx & 7] : k5_pal_normal[idx & 7];
}

/*------------------------------------------------------------------------
 * k5_blit_cell - copy one 8x16 character cell from the back-buffer to the
 *		  visible framebuffer
 *------------------------------------------------------------------------
 */
static void k5_blit_cell(int32 row, int32 col)
{
	uint32 stride = k2023202296_fb_pitch / 4;
	int32  x0 = col * K5_FONT_W;
	int32  y0 = row * K5_FONT_H;
	int32  y, x;

	for (y = 0; y < K5_FONT_H; y++) {
		uint32 src = (y0 + y) * K5_SCR_W + x0;
		uint32 dst = (y0 + y) * stride   + x0;
		for (x = 0; x < K5_FONT_W; x++)
			k2023202296_fb[dst + x] = k5_back[src + x];
	}
}

/*------------------------------------------------------------------------
 * k5_blit_all - copy the entire back-buffer to the framebuffer
 *------------------------------------------------------------------------
 */
static void k5_blit_all(void)
{
	uint32 stride = k2023202296_fb_pitch / 4;
	int32  y, x;

	for (y = 0; y < K5_SCR_H; y++) {
		uint32 src = y * K5_SCR_W;
		uint32 dst = y * stride;
		for (x = 0; x < K5_SCR_W; x++)
			k2023202296_fb[dst + x] = k5_back[src + x];
	}
}

/*------------------------------------------------------------------------
 * k5_draw_glyph - render one glyph into the back-buffer cell and blit it
 *------------------------------------------------------------------------
 */
static void k5_draw_glyph(int32 row, int32 col, unsigned char ch,
			  uint32 fg, uint32 bg)
{
	const unsigned char *gl = k2023202296_font8x16[ch];
	int32  x0 = col * K5_FONT_W;
	int32  y0 = row * K5_FONT_H;
	int32  y, x;

	for (y = 0; y < K5_FONT_H; y++) {
		unsigned char bits = gl[y];
		uint32 base = (y0 + y) * K5_SCR_W + x0;
		for (x = 0; x < K5_FONT_W; x++)
			k5_back[base + x] = (bits & (0x80 >> x)) ? fg : bg;
	}
	k5_blit_cell(row, col);
}

/*------------------------------------------------------------------------
 * cursor_show / cursor_hide - software underline cursor (drawn only to the
 *	visible framebuffer, never into the back-buffer, so it can be wiped
 *	by simply re-blitting the cell)
 *------------------------------------------------------------------------
 */
void k2023202296_term_cursor_show(void)
{
	uint32 stride = k2023202296_fb_pitch / 4;
	int32  x0 = k5_col * K5_FONT_W;
	int32  y0 = k5_row * K5_FONT_H;
	int32  y, x;

	if (!k2023202296_term_ready)
		return;
	for (y = K5_FONT_H - 2; y < K5_FONT_H; y++)
		for (x = 0; x < K5_FONT_W; x++)
			k2023202296_fb[(y0 + y) * stride + x0 + x] = 0x00AAAAAA;
}

void k2023202296_term_cursor_hide(void)
{
	if (!k2023202296_term_ready)
		return;
	k5_blit_cell(k5_row, k5_col);		/* restore from back-buffer */
}

/*------------------------------------------------------------------------
 * k5_clear_row - paint one text row with the current background
 *------------------------------------------------------------------------
 */
static void k5_clear_row(int32 row)
{
	int32 y0 = row * K5_FONT_H;
	int32 y;

	for (y = 0; y < K5_FONT_H; y++) {
		uint32 base = (y0 + y) * K5_SCR_W;
		int32  x;
		for (x = 0; x < K5_SCR_W; x++)
			k5_back[base + x] = k5_bg;
	}
}

/*------------------------------------------------------------------------
 * k2023202296_term_clear - clear the whole screen and home the cursor
 *------------------------------------------------------------------------
 */
void k2023202296_term_clear(void)
{
	int32 i;

	if (!k2023202296_term_ready)
		return;
	for (i = 0; i < K5_SCR_W * K5_SCR_H; i++)
		k5_back[i] = k5_bg;
	k5_blit_all();
	k5_row = k5_col = 0;
}

/*------------------------------------------------------------------------
 * k5_scroll - move the image up one text line, clear the new bottom line
 *------------------------------------------------------------------------
 */
static void k5_scroll(void)
{
	uint32 line = K5_FONT_H * K5_SCR_W;	/* pixels per text line	*/
	uint32 total = K5_SCR_W * K5_SCR_H;
	uint32 i;

	/* shift everything up by one line inside the RAM back-buffer	*/
	for (i = 0; i < total - line; i++)
		k5_back[i] = k5_back[i + line];
	k5_clear_row(K5_ROWS - 1);

	k5_blit_all();
	k2023202296_term_scrolls++;
}

/*------------------------------------------------------------------------
 * term_getpos / term_setpos - query / move the logical cursor cell (used
 *	by the keyboard line editor for arrow-key cursor movement)
 *------------------------------------------------------------------------
 */
void k2023202296_term_getpos(int32 *row, int32 *col)
{
	if (row) *row = k5_row;
	if (col) *col = k5_col;
}

void k2023202296_term_setpos(int32 row, int32 col)
{
	if (row < 0) row = 0;
	if (col < 0) col = 0;
	if (row >= K5_ROWS) row = K5_ROWS - 1;
	if (col >= K5_COLS) col = K5_COLS - 1;
	k2023202296_term_cursor_hide();
	k5_row = row;
	k5_col = col;
	k2023202296_term_cursor_show();
}

/*------------------------------------------------------------------------
 * k5_newline / k5_advance - cursor movement with wrap and scroll
 *------------------------------------------------------------------------
 */
static void k5_newline(void)
{
	k5_col = 0;
	if (++k5_row >= K5_ROWS) {
		k5_row = K5_ROWS - 1;
		k5_scroll();
	}
}

static void k5_advance(void)
{
	if (++k5_col >= K5_COLS)
		k5_newline();
}

/*------------------------------------------------------------------------
 * k5_apply_sgr - apply the collected ANSI "ESC [ ... m" parameters
 *------------------------------------------------------------------------
 */
static void k5_apply_sgr(void)
{
	int32 i, p;

	if (k5_nparam == 0) {			/* "ESC[m" == reset	*/
		k5_fg_idx = k5_bg_idx = -1;
		k5_bold = 0;
	}
	for (i = 0; i < k5_nparam; i++) {
		p = k5_params[i];
		if (p == 0)        { k5_fg_idx = k5_bg_idx = -1; k5_bold = 0; }
		else if (p == 1)   { k5_bold = 1; }
		else if (p == 22)  { k5_bold = 0; }
		else if (p >= 30 && p <= 37) { k5_fg_idx = p - 30; }
		else if (p == 39)  { k5_fg_idx = -1; }
		else if (p >= 40 && p <= 47) { k5_bg_idx = p - 40; }
		else if (p == 49)  { k5_bg_idx = -1; }
		else if (p >= 90 && p <= 97) { k5_fg_idx = p - 90; k5_bold = 1; }
	}
	k5_fg = k5_color_of(k5_fg_idx, k5_bold, 1);
	k5_bg = k5_color_of(k5_bg_idx, 0, 0);
}

/*------------------------------------------------------------------------
 * k5_csi_final - handle the final byte of a CSI escape sequence
 *------------------------------------------------------------------------
 */
static void k5_csi_final(char c)
{
	switch (c) {
	case 'm':			/* SGR - select graphic rendition */
		k5_apply_sgr();
		break;
	case 'J':			/* erase display (2J = whole)	  */
		if (k5_nparam == 0 || k5_params[0] == 2)
			k2023202296_term_clear();
		break;
	case 'H':			/* cursor position (1-based)	  */
	case 'f':
		k5_row = (k5_nparam >= 1 && k5_params[0] > 0) ?
			 k5_params[0] - 1 : 0;
		k5_col = (k5_nparam >= 2 && k5_params[1] > 0) ?
			 k5_params[1] - 1 : 0;
		if (k5_row >= K5_ROWS) k5_row = K5_ROWS - 1;
		if (k5_col >= K5_COLS) k5_col = K5_COLS - 1;
		break;
	default:
		break;			/* silently ignore the rest	  */
	}
}

/*------------------------------------------------------------------------
 * k5_feed - run one byte through the ANSI state machine; returns 1 if the
 *	     byte was consumed by an escape sequence, 0 if it is ordinary
 *------------------------------------------------------------------------
 */
static int32 k5_feed(char c)
{
	if (k5_esc == 0) {
		if (c == 0x1B) { k5_esc = 1; return 1; }
		return 0;
	}
	if (k5_esc == 1) {		/* expecting '['		  */
		if (c == '[') {
			k5_esc = 2;
			k5_nparam = 0;
			k5_params[0] = 0;
		} else {
			k5_esc = 0;	/* unsupported escape, drop	  */
		}
		return 1;
	}
	/* k5_esc == 2: collecting CSI parameters			  */
	if (c >= '0' && c <= '9') {
		if (k5_nparam == 0) k5_nparam = 1;
		k5_params[k5_nparam - 1] =
			k5_params[k5_nparam - 1] * 10 + (c - '0');
		return 1;
	}
	if (c == ';') {
		if (k5_nparam < 8) {
			k5_params[k5_nparam] = 0;
			k5_nparam++;
		}
		return 1;
	}
	/* any other byte terminates the sequence			  */
	k5_csi_final(c);
	k5_esc = 0;
	return 1;
}

/*------------------------------------------------------------------------
 * k2023202296_term_putc - the public entry point: render one character,
 *	honouring control characters and ANSI escapes, keeping the cursor
 *	visible after every call
 *------------------------------------------------------------------------
 */
void k2023202296_term_putc(char c)
{
	if (!k2023202296_term_ready)
		return;

	k2023202296_term_cursor_hide();		/* lift cursor overlay	  */

	if (k5_esc || c == 0x1B) {		/* in / entering escape	  */
		if (k5_feed(c)) {
			k2023202296_term_cursor_show();
			return;
		}
	}

	switch (c) {
	case '\n':				/* line feed		  */
		k5_newline();
		break;
	case '\r':				/* carriage return	  */
		k5_col = 0;
		break;
	case '\t':				/* tab -> next 8th column */
		do {
			k5_draw_glyph(k5_row, k5_col, ' ', k5_fg, k5_bg);
			k5_advance();
		} while (k5_col & 7);
		break;
	case '\b':				/* backspace: move left	  */
		if (k5_col > 0)
			k5_col--;
		else if (k5_row > 0) {
			k5_row--;
			k5_col = K5_COLS - 1;
		}
		break;
	case 0x07:				/* bell: ignore		  */
		break;
	default:
		if ((unsigned char)c >= 0x20) {
			k5_draw_glyph(k5_row, k5_col, (unsigned char)c,
				      k5_fg, k5_bg);
			k5_advance();
		}
		break;
	}

	k2023202296_term_cursor_show();		/* re-arm cursor overlay  */
}

/*------------------------------------------------------------------------
 * k2023202296_term_init - clear the screen and make the terminal usable
 *------------------------------------------------------------------------
 */
void k2023202296_term_init(void)
{
	if (!k2023202296_fb_ready)
		return;
	k5_row = k5_col = 0;
	k5_fg_idx = k5_bg_idx = -1;
	k5_bold = 0;
	k5_fg = 0x00AAAAAA;
	k5_bg = 0x00000000;
	k5_esc = 0;
	k2023202296_term_ready = 1;
	k2023202296_term_clear();
	k2023202296_term_cursor_show();
}
