/* Lab5_video.c - VBE linear-framebuffer bring-up (Lab 5, 2023202296)	*/

#include <xinu.h>

/* Global video state (declared in Lab5.h)				*/
volatile uint32 *k2023202296_fb       = (volatile uint32 *)0;
uint32		k2023202296_fb_phys   = 0;
uint32		k2023202296_fb_pitch  = 0;
int32		k2023202296_fb_ready  = 0;

/*------------------------------------------------------------------------
 * k5_vbe_write - write one Bochs DISPI register
 *------------------------------------------------------------------------
 */
static void k5_vbe_write(uint16 index, uint16 value)
{
	outw(K5_VBE_INDEX, index);
	outw(K5_VBE_DATA,  value);
}

/*------------------------------------------------------------------------
 * k5_vbe_read - read one Bochs DISPI register
 *------------------------------------------------------------------------
 */
static uint16 k5_vbe_read(uint16 index)
{
	outw(K5_VBE_INDEX, index);
	return (uint16)inw(K5_VBE_DATA);
}

/*------------------------------------------------------------------------
 * k5_pci_read32 - read a 32-bit word from PCI config space
 *------------------------------------------------------------------------
 */
static uint32 k5_pci_read32(uint32 bus, uint32 dev, uint32 func, uint32 off)
{
	uint32 addr = 0x80000000u | (bus << 16) | (dev << 11) |
		      (func << 8) | (off & 0xFC);
	outl(K5_PCI_ADDR, addr);
	return (uint32)inl(K5_PCI_DATA);
}

/*------------------------------------------------------------------------
 * k5_find_lfb - locate the linear-framebuffer physical base.
 *
 * Scans the PCI bus for the QEMU/Bochs VGA controller (vendor 0x1234
 * device 0x1111, or any class-0x03 display controller) and returns the
 * memory address programmed into BAR0 (the framebuffer aperture).
 *------------------------------------------------------------------------
 */
static uint32 k5_find_lfb(void)
{
	uint32 bus, dev;

	for (bus = 0; bus < 256; bus++) {
		for (dev = 0; dev < 32; dev++) {
			uint32 id   = k5_pci_read32(bus, dev, 0, 0x00);
			uint32 vend = id & 0xFFFF;
			uint32 cls;
			uint32 bar0;

			if (vend == 0xFFFF)
				continue;	/* no device in this slot */

			cls = k5_pci_read32(bus, dev, 0, 0x08) >> 24;

			/* QEMU std-VGA (1234:1111) or any VGA-class device */
			if (id == 0x11111234 || cls == 0x03) {
				bar0 = k5_pci_read32(bus, dev, 0, 0x10);
				if (bar0 & 0x1)
					continue;	/* I/O BAR, not memory */
				return bar0 & 0xFFFFFFF0u;
			}
		}
	}
	return 0;
}

/*------------------------------------------------------------------------
 * k2023202296_fb_clear - fill the whole screen with one colour
 *------------------------------------------------------------------------
 */
void k2023202296_fb_clear(uint32 rgb)
{
	uint32 n = (k2023202296_fb_pitch / 4) * K5_SCR_H;
	uint32 i;

	if (!k2023202296_fb_ready)
		return;
	for (i = 0; i < n; i++)
		k2023202296_fb[i] = rgb;
}

/*------------------------------------------------------------------------
 * k2023202296_fb_fillrect - fill a rectangle (clipped to the screen)
 *------------------------------------------------------------------------
 */
void k2023202296_fb_fillrect(int32 x, int32 y, int32 w, int32 h, uint32 rgb)
{
	int32  row, col;
	uint32 stride = k2023202296_fb_pitch / 4;

	if (!k2023202296_fb_ready)
		return;
	for (row = y; row < y + h; row++) {
		if (row < 0 || row >= K5_SCR_H)
			continue;
		for (col = x; col < x + w; col++) {
			if (col < 0 || col >= K5_SCR_W)
				continue;
			k2023202296_fb[row * stride + col] = rgb;
		}
	}
}

/*------------------------------------------------------------------------
 * k2023202296_video_init - probe the LFB, switch to 640x480x32, and draw
 *                          a recognisable test pattern.
 *
 * Runs early in sysinit (paging is still OFF, so the high physical LFB
 * address is directly addressable through the flat segments).
 *------------------------------------------------------------------------
 */
void k2023202296_video_init(void)
{
	uint32 lfb;
	uint16 id;

	/* Confirm the DISPI interface is present                        */
	id = k5_vbe_read(K5_VBE_ID);
	kprintf("[lab5] VBE DISPI id=0x%04X\n", id);

	/* Locate the framebuffer through PCI BAR0                       */
	lfb = k5_find_lfb();
	kprintf("[lab5] LFB phys base = 0x%08X\n", lfb);
	if (lfb == 0) {
		kprintf("[lab5] ERROR: no VGA framebuffer found\n");
		return;
	}

	/* Program a 640x480x32 linear-framebuffer mode                  */
	k5_vbe_write(K5_VBE_ENABLE, K5_VBE_DISABLED);
	k5_vbe_write(K5_VBE_XRES,   K5_SCR_W);
	k5_vbe_write(K5_VBE_YRES,   K5_SCR_H);
	k5_vbe_write(K5_VBE_BPP,    K5_SCR_BPP);
	k5_vbe_write(K5_VBE_ENABLE, K5_VBE_ENABLED | K5_VBE_LFB_ENABLED);

	k2023202296_fb_phys  = lfb;
	k2023202296_fb       = (volatile uint32 *)lfb;
	k2023202296_fb_pitch = K5_SCR_W * 4;	/* linear, no padding	*/
	k2023202296_fb_ready = 1;

	kprintf("[lab5] mode set: %dx%dx%d pitch=%d fb=0x%08X\n",
		K5_SCR_W, K5_SCR_H, K5_SCR_BPP, k2023202296_fb_pitch, lfb);

	/* Bring up the text terminal on top of the framebuffer          */
	k2023202296_term_init();
}

/* Page table backing the identity map of the framebuffer aperture.	*/
static uint32 k5_fb_pt[1024] __attribute__((aligned(4096)));

/*------------------------------------------------------------------------
 * k2023202296_map_framebuffer - identity-map the LFB aperture into a page
 *	directory so the terminal keeps working once paging is enabled.
 *
 * Called from vminit() on the master kernel page directory before CR3 is
 * loaded; every per-process directory is later copied from it, so the
 * framebuffer is mapped in every address space (kernel writes to it always
 * happen at CPL=0, so the mapping is supervisor-only).
 *------------------------------------------------------------------------
 */
void k2023202296_map_framebuffer(uint32 *pgdir)
{
	uint32 base, pdi, i;

	if (!k2023202296_fb_ready || k2023202296_fb_phys == 0)
		return;
	base = k2023202296_fb_phys & 0xFFC00000u;	/* 4MB-aligned	*/
	pdi  = base >> 22;
	for (i = 0; i < 1024; i++)
		k5_fb_pt[i] = (base + i * PAGE_SIZE) | K4_PRESENT | K4_RW;
	pgdir[pdi] = ((uint32)k5_fb_pt) | K4_PRESENT | K4_RW;
}
