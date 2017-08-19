#include <string.h>


#include "defs.h"
#include "cpu.h"
#include "hw.h"
#include "regs.h"
#include "lcd.h"
#include "mem.h"
#include "fastmem.h"


struct hw hw;



/*
 * hw_interrupt changes the virtual interrupt lines included in the
 * specified mask to the values the corresponding bits in i take, and
 * in doing so, raises the appropriate bit of R_IF for any interrupt
 * lines that transition from low to high.
 */

void hw_interrupt(byte i, byte mask)
{
	byte oldif = R_IF;
	i &= 0x1F & mask;
	R_IF |= i & (hw.ilines ^ i);

	/* FIXME - is this correct? not sure the docs understand... */
	if ((R_IF & (R_IF ^ oldif) & R_IE) && cpu.ime) cpu.halt = 0;
	/* if ((i & (hw.ilines ^ i) & R_IE) && cpu.ime) cpu.halt = 0; */
	/* if ((i & R_IE) && cpu.ime) cpu.halt = 0; */
	
	hw.ilines &= ~mask;
	hw.ilines |= i;
}


/*
 * hw_dma performs plain old memory-to-oam dma, the original dmg
 * dma. Although on the hardware it takes a good deal of time, the cpu
 * continues running during this mode of dma, so no special tricks to
 * stall the cpu are necessary.
 */

void hw_dma(byte b)
{
	int i;
	addr a;

	a = ((addr)b) << 8;
	for (i = 0; i < 160; i++, a++)
		lcd.oam.mem[i] = readb(a);
}

/* COMMENT A:
 * Beware was pretty sure that this HDMA implementation was incorrect, as when
 * he used it in bgb, it broke Pokemon Crystal (J). I tested it with this and
 * it seems to work fine, so until I find any problems with it, it's staying.
 * (Lord Nightmare)
 */


/* COMMENT B:
Timings for GDMA by /dalias/ by all means are accurate within few cycles, given
whatever we feed these values in takes a time to run expressed in double-speed
machine cycles as an argument. Whatever DMA-related issues remain, they are
likely in how intervals are applied and not in how they are calculated.

Had to replace cpu_timers() in GDMA section as it was calling lcdc_trans() and
breaking Shantae. Figured out the same thing happens to Pokemon Crystal and
HDMA so I also got HDMA init interval in place. Ghostbusters got fixed somehow
somewhere along the way.

Not sure how to be about now-missing lcd update. It doesn't seem to damage any
games so I decided not to touch anything else.
*/


void hw_hdma()
{
	int cnt;
	addr sa;
	int da;

	sa = ((addr)R_HDMA1 << 8) | (R_HDMA2&0xf0);
	da = 0x8000 | ((int)(R_HDMA3&0x1f) << 8) | (R_HDMA4&0xf0);
	cnt = 16;
	while (cnt--)
		writeb(da++, readb(sa++));
		
	/* SEE COMMENT B ABOVE */
	div_advance(16 << cpu.speed);
	timer_advance(16 << cpu.speed);
	sound_advance(16);
	
	R_HDMA1 = sa >> 8;
	R_HDMA2 = sa & 0xF0;
	R_HDMA3 = 0x1F & (da >> 8);
	R_HDMA4 = da & 0xF0;
	R_HDMA5--;
	hw.hdma--;
}


void hw_hdma_cmd(byte c)
{
	int cnt;
	addr sa;
	int da;
	
	int advance;

	/* Begin or cancel HDMA */
	if ((hw.hdma|c) & 0x80)
	{
		hw.hdma = c;
		R_HDMA5 = c & 0x7f;
		
		/* SEE COMMENT B ABOVE */
		advance = 460 >> cpu.speed;
		div_advance(advance << cpu.speed);
		timer_advance(advance << cpu.speed);
		sound_advance(advance);
		
		if ((R_STAT&0x03) == 0x00) hw_hdma(); /* SEE COMMENT A ABOVE */
		return;
	}
	
	/* Perform GDMA */
	sa = ((addr)R_HDMA1 << 8) | (R_HDMA2&0xf0);
	da = 0x8000 | ((int)(R_HDMA3&0x1f) << 8) | (R_HDMA4&0xf0);
	cnt = ((int)c)+1;
	
	/* SEE COMMENT B ABOVE */
	/*cpu_timers((460>>cpu.speed)+cnt*16);*/ /*dalias*/
	advance = (460 >> cpu.speed) + cnt*16;
	div_advance(advance << cpu.speed);
	timer_advance(advance << cpu.speed);
	sound_advance(advance);
	
	cnt <<= 4;
	while (cnt--)
		writeb(da++, readb(sa++));
	R_HDMA1 = sa >> 8;
	R_HDMA2 = sa & 0xF0;
	R_HDMA3 = 0x1F & (da >> 8);
	R_HDMA4 = da & 0xF0;
	R_HDMA5 = 0xFF;
}


/*
 * pad_refresh updates the P1 register from the pad states, generating
 * the appropriate interrupts (by quickly raising and lowering the
 * interrupt line) if a transition has been made.
 */

void pad_refresh()
{
	byte oldp1;
	oldp1 = R_P1;
	R_P1 &= 0x30;
	R_P1 |= 0xc0;
	if (!(R_P1 & 0x10))
		R_P1 |= (hw.pad & 0x0F);
	if (!(R_P1 & 0x20))
		R_P1 |= (hw.pad >> 4);
	R_P1 ^= 0x0F;
	if (oldp1 & ~R_P1 & 0x0F)
	{
		hw_interrupt(IF_PAD, IF_PAD);
		hw_interrupt(0, IF_PAD);
	}
}


/*
 * These simple functions just update the state of a button on the
 * pad.
 */

void pad_press(byte k)
{
	if (hw.pad & k)
		return;
	hw.pad |= k;
	pad_refresh();
}

void pad_release(byte k)
{
	if (!(hw.pad & k))
		return;
	hw.pad &= ~k;
	pad_refresh();
}

void pad_set(byte k, int st)
{
	st ? pad_press(k) : pad_release(k);
}

void hw_reset()
{
	hw.ilines = hw.pad = 0;

	memset(ram.hi, 0, sizeof ram.hi);

	R_P1 = 0xFF;
	R_LCDC = 0x91;
	R_BGP = 0xFC;
	R_OBP0 = 0xFF;
	R_OBP1 = 0xFF;
	R_SVBK = 0x01;
	R_HDMA5 = 0xFF;
	R_VBK = 0xFE;
}







