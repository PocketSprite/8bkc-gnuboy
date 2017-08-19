#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "refresh.h"
#include "palette.h"
#include "defs.h"
#include "regs.h"
#include "hw.h"
#include "mem.h"
#include "lcd.h"
#include "fb.h"

#include "esp_attr.h"

struct lcd lcd;

struct scan scan;

#define BG (scan.bg)
#define WND (scan.wnd)
#define BUF (scan.buf)
#define PRI (scan.pri)

#define PAL1 (scan.pal1)
#define PAL2 (scan.pal2)
#define PAL4 (scan.pal4)

#define VS (scan.vs) /* vissprites */
#define NS (scan.ns)

#define L (scan.l) /* line */
#define X (scan.x) /* screen position */
#define Y (scan.y)
#define S (scan.s) /* tilemap position */
#define T (scan.t)
#define U (scan.u) /* position within tile */
#define V (scan.v)
#define WX (scan.wx)
#define WY (scan.wy)
#define WT (scan.wt)
#define WV (scan.wv)


//Lowering this decreases memory, but increases risk of slowdown.
#define CACHED_PATPIX_NO 1200

typedef struct {
	int16_t pat;
	byte pix[64];
} PatcacheEntry;

static PatcacheEntry patcache[CACHED_PATPIX_NO];
static IRAM_ATTR PatcacheEntry* patcacheptr[4096];
static int patcachefill;


static int rgb332;

static int sprsort = 1;
static int sprdebug;

#define DEF_PAL { 0x98d0e0, 0x68a0b0, 0x60707C, 0x2C3C3C }

static int dmg_pal[4][4] = { DEF_PAL, DEF_PAL, DEF_PAL, DEF_PAL };

static int usefilter, filterdmg;
static int filter[3][4] = {
	{ 195,  25,   0,  35 },
	{  25, 170,  25,  35 },
	{  25,  60, 125,  40 }
};

/*
const rcvar_t lcd_exports[] =
{
	RCV_INT("scale", &scale),
	RCV_INT("density", &density),
	RCV_BOOL("rgb332", &rgb332),
	RCV_VECTOR("dmg_bgp", dmg_pal[0], 4),
	RCV_VECTOR("dmg_wndp", dmg_pal[1], 4),
	RCV_VECTOR("dmg_obp0", dmg_pal[2], 4),
	RCV_VECTOR("dmg_obp1", dmg_pal[3], 4),
	RCV_BOOL("sprsort", &sprsort),
	RCV_BOOL("sprdebug", &sprdebug),
	RCV_BOOL("colorfilter", &usefilter),
	RCV_BOOL("filterdmg", &filterdmg),
	RCV_VECTOR("red", filter[0], 4),
	RCV_VECTOR("green", filter[1], 4),
	RCV_VECTOR("blue", filter[2], 4),
	RCV_END
};
*/

static byte *vdest;

#ifdef ALLOW_UNALIGNED_IO /* long long is ok since this is i386-only anyway? */
#define MEMCPY8(d, s) ((*(long long *)(d)) = (*(long long *)(s)))
#else
#define MEMCPY8(d, s) memcpy((d), (s), 8)
#endif


static void genpatpix(int patno, byte *buf) {
	int no=patno&1023;
	int or=patno>>10;
	int a, j, k, c;

	byte *vram = lcd.vbank[0];

	for (j = 0; j < 8; j++) {
		a = ((no<<4) | (j<<1));
		for (k = 0; k < 8; k++) {
			c = vram[a] & (1<<k) ? 1 : 0;
			c |= vram[a+1] & (1<<k) ? 2 : 0;
			if (or==0) buf[j*8+(7-k)]=c;
			if (or==1) buf[j*8+k]=c;
			if (or==2) buf[(7-j)*8+(7-k)]=c;
			if (or==3) buf[(7-j)*8+k]=c;
		}
	}
}


int patcachemiss, patcachehit;

static byte *getpatpix(int pat) {
	static int pos=0;
	/* If pat found, return immediately */
	if (patcacheptr[pat]!=NULL) {
		patcachehit++;
		return patcacheptr[pat]->pix;
	}
	patcachemiss++;
	/* Pat not found. Generate. */
	//ToDo: better way to retire patterns.
	pos++;
	if (pos>=CACHED_PATPIX_NO) pos=0;

	if (patcache[pos].pat!=-1) {
		patcacheptr[patcache[pos].pat]=NULL;
	}

	genpatpix(pat, patcache[pos].pix);
	patcache[pos].pat=pat;
	patcacheptr[pat]=&patcache[pos];
	return patcache[pos].pix;
}



static void tilebuf()
{
	int i, cnt;
	int base;
	byte *tilemap, *attrmap;
	int *tilebuf;
	const int *wrap;
	static const int wraptable[64] =
	{
		0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,-32
	};

	base = ((R_LCDC&0x08)?0x1C00:0x1800) + (T<<5) + S;
	tilemap = lcd.vbank[0] + base;
	attrmap = lcd.vbank[1] + base;
	tilebuf = BG;
	wrap = wraptable + S;
	cnt = ((WX + 7) >> 3) + 1;

	if (hw.cgb)
	{
		if (R_LCDC & 0x10)
			for (i = cnt; i > 0; i--)
			{
				*(tilebuf++) = *tilemap
					| (((int)*attrmap & 0x08) << 6)
					| (((int)*attrmap & 0x60) << 5);
				*(tilebuf++) = (((int)*attrmap & 0x07) << 2);
				attrmap += *wrap + 1;
				tilemap += *(wrap++) + 1;
			}
		else
			for (i = cnt; i > 0; i--)
			{
				*(tilebuf++) = (256 + ((n8)*tilemap))
					| (((int)*attrmap & 0x08) << 6)
					| (((int)*attrmap & 0x60) << 5);
				*(tilebuf++) = (((int)*attrmap & 0x07) << 2);
				attrmap += *wrap + 1;
				tilemap += *(wrap++) + 1;
			}
	}
	else
	{
		if (R_LCDC & 0x10)
			for (i = cnt; i > 0; i--)
			{
				*(tilebuf++) = *(tilemap++);
				tilemap += *(wrap++);
			}
		else
			for (i = cnt; i > 0; i--)
			{
				*(tilebuf++) = (256 + ((n8)*(tilemap++)));
				tilemap += *(wrap++);
			}
	}

	if (WX >= 160) return;
	
	base = ((R_LCDC&0x40)?0x1C00:0x1800) + (WT<<5);
	tilemap = lcd.vbank[0] + base;
	attrmap = lcd.vbank[1] + base;
	tilebuf = WND;
	cnt = ((160 - WX) >> 3) + 1;

	if (hw.cgb)
	{
		if (R_LCDC & 0x10)
			for (i = cnt; i > 0; i--)
			{
				*(tilebuf++) = *(tilemap++)
					| (((int)*attrmap & 0x08) << 6)
					| (((int)*attrmap & 0x60) << 5);
				*(tilebuf++) = (((int)*(attrmap++)&7) << 2);
			}
		else
			for (i = cnt; i > 0; i--)
			{
				*(tilebuf++) = (256 + ((n8)*(tilemap++)))
					| (((int)*attrmap & 0x08) << 6)
					| (((int)*attrmap & 0x60) << 5);
				*(tilebuf++) = (((int)*(attrmap++)&7) << 2);
			}
	}
	else
	{
		if (R_LCDC & 0x10)
			for (i = cnt; i > 0; i--)
				*(tilebuf++) = *(tilemap++);
		else
			for (i = cnt; i > 0; i--)
				*(tilebuf++) = (256 + ((n8)*(tilemap++)));
	}
}


static void bg_scan()
{
	int cnt;
	byte *src, *dest;
	int *tile;

	if (WX <= 0) return;
	cnt = WX;
	tile = BG;
	dest = BUF;
	
	src = getpatpix(*(tile++))+ (V*8) + U;
	memcpy(dest, src, 8-U);
	dest += 8-U;
	cnt -= 8-U;
	if (cnt <= 0) return;
	while (cnt >= 8)
	{
		src = getpatpix(*(tile++))+(V*8);
		MEMCPY8(dest, src);
		dest += 8;
		cnt -= 8;
	}
	src = getpatpix(*tile)+(V*8);
	while (cnt--)
		*(dest++) = *(src++);
}

static void wnd_scan()
{
	int cnt;
	byte *src, *dest;
	int *tile;

	if (WX >= 160) return;
	cnt = 160 - WX;
	tile = WND;
	dest = BUF + WX;
	
	while (cnt >= 8)
	{
		src = getpatpix(*(tile++))+(WV*8);
		MEMCPY8(dest, src);
		dest += 8;
		cnt -= 8;
	}
	src = getpatpix(*tile)+(WV*8);
	while (cnt--)
		*(dest++) = *(src++);
}

static void blendcpy(byte *dest, byte *src, byte b, int cnt)
{
	while (cnt--) *(dest++) = *(src++) | b;
}

static int priused(void *attr)
{
	un32 *a = attr;
	return (int)((a[0]|a[1]|a[2]|a[3]|a[4]|a[5]|a[6]|a[7])&0x80808080);
}

static void bg_scan_pri()
{
	int cnt, i;
	byte *src, *dest;

	if (WX <= 0) return;
	i = S;
	cnt = WX;
	dest = PRI;
	src = lcd.vbank[1] + ((R_LCDC&0x08)?0x1C00:0x1800) + (T<<5);

	if (!priused(src))
	{
		memset(dest, 0, cnt);
		return;
	}
	
	memset(dest, src[i++&31]&128, 8-U);
	dest += 8-U;
	cnt -= 8-U;
	if (cnt <= 0) return;
	while (cnt >= 8)
	{
		memset(dest, src[i++&31]&128, 8);
		dest += 8;
		cnt -= 8;
	}
	memset(dest, src[i&31]&128, cnt);
}

static void wnd_scan_pri()
{
	int cnt, i;
	byte *src, *dest;

	if (WX >= 160) return;
	i = 0;
	cnt = 160 - WX;
	dest = PRI + WX;
	src = lcd.vbank[1] + ((R_LCDC&0x40)?0x1C00:0x1800) + (WT<<5);
	
	if (!priused(src))
	{
		memset(dest, 0, cnt);
		return;
	}
	
	while (cnt >= 8)
	{
		memset(dest, src[i++]&128, 8);
		dest += 8;
		cnt -= 8;
	}
	memset(dest, src[i]&128, cnt);
}

#ifndef ASM_BG_SCAN_COLOR
static void bg_scan_color()
{
	int cnt;
	byte *src, *dest;
	int *tile;

	if (WX <= 0) return;
	cnt = WX;
	tile = BG;
	dest = BUF;
	
	src = getpatpix(*(tile++))+(V*8) + U;
	blendcpy(dest, src, *(tile++), 8-U);
	dest += 8-U;
	cnt -= 8-U;
	if (cnt <= 0) return;
	while (cnt >= 8)
	{
		src = getpatpix(*(tile++))+(V*8);
		blendcpy(dest, src, *(tile++), 8);
		dest += 8;
		cnt -= 8;
	}
	src = getpatpix(*(tile++))+(V*8);
	blendcpy(dest, src, *(tile++), cnt);
}
#endif

void wnd_scan_color()
{
	int cnt;
	byte *src, *dest;
	int *tile;

	if (WX >= 160) return;
	cnt = 160 - WX;
	tile = WND;
	dest = BUF + WX;
	
	while (cnt >= 8)
	{
		src = getpatpix(*(tile++))+(WV*8);
		blendcpy(dest, src, *(tile++), 8);
		dest += 8;
		cnt -= 8;
	}
	src = getpatpix(*(tile++))+(WV*8);
	blendcpy(dest, src, *(tile++), cnt);
}

static void recolor(byte *buf, byte fill, int cnt)
{
	while (cnt--) *(buf++) |= fill;
}

static void spr_count()
{
	int i;
	struct obj *o;
	
	NS = 0;
	if (!(R_LCDC & 0x02)) return;
	
	o = lcd.oam.obj;
	
	for (i = 40; i; i--, o++)
	{
		if (L >= o->y || L + 16 < o->y)
			continue;
		if (L + 8 >= o->y && !(R_LCDC & 0x04))
			continue;
		if (++NS == 10) break;
	}
}

static void spr_enum()
{
	int i, j;
	struct obj *o;
	struct vissprite ts[10];
	int v, pat;
	int l, x;

	NS = 0;
	if (!(R_LCDC & 0x02)) return;

	o = lcd.oam.obj;
	
	for (i = 40; i; i--, o++)
	{
		if (L >= o->y || L + 16 < o->y)
			continue;
		if (L + 8 >= o->y && !(R_LCDC & 0x04))
			continue;
		VS[NS].x = (int)o->x - 8;
		v = L - (int)o->y + 16;
		if (hw.cgb)
		{
			pat = o->pat | (((int)o->flags & 0x60) << 5)
				| (((int)o->flags & 0x08) << 6);
			VS[NS].pal = 32 + ((o->flags & 0x07) << 2);
		}
		else
		{
			pat = o->pat | (((int)o->flags & 0x60) << 5);
			VS[NS].pal = 32 + ((o->flags & 0x10) >> 2);
		}
		VS[NS].pri = (o->flags & 0x80) >> 7;
		if ((R_LCDC & 0x04))
		{
			pat &= ~1;
			if (v >= 8)
			{
				v -= 8;
				pat++;
			}
			if (o->flags & 0x40) pat ^= 1;
		}
		VS[NS].buf = getpatpix(pat)+(v*8);
		if (++NS == 10) break;
	}
	if (!sprsort || hw.cgb) return;
	/* not quite optimal but it finally works! */
	for (i = 0; i < NS; i++)
	{
		l = 0;
		x = VS[0].x;
		for (j = 1; j < NS; j++)
		{
			if (VS[j].x < x)
			{
				l = j;
				x = VS[j].x;
			}
		}
		ts[i] = VS[l];
		VS[l].x = 160;
	}
	memcpy(VS, ts, sizeof VS);
}

static void spr_scan()
{
	int i, x;
	byte pal, b, ns = NS;
	byte *src, *dest, *bg, *pri;
	struct vissprite *vs;
	static byte bgdup[256];

	if (!ns) return;

	memcpy(bgdup, BUF, 256);
	vs = &VS[ns-1];
	
	for (; ns; ns--, vs--)
	{
		x = vs->x;
		if (x >= 160) continue;
		if (x <= -8) continue;
		if (x < 0)
		{
			src = vs->buf - x;
			dest = BUF;
			i = 8 + x;
		}
		else
		{
			src = vs->buf;
			dest = BUF + x;
			if (x > 152) i = 160 - x;
			else i = 8;
		}
		pal = vs->pal;
		if (vs->pri)
		{
			bg = bgdup + (dest - BUF);
			while (i--)
			{
				b = src[i];
				if (b && !(bg[i]&3)) dest[i] = pal|b;
			}
		}
		else if (hw.cgb)
		{
			bg = bgdup + (dest - BUF);
			pri = PRI + (dest - BUF);
			while (i--)
			{
				b = src[i];
				if (b && (!pri[i] || !(bg[i]&3)))
					dest[i] = pal|b;
			}
		}
		else while (i--) if (src[i]) dest[i] = pal|src[i];
		/* else while (i--) if (src[i]) dest[i] = 31 + ns; */
	}
	if (sprdebug) for (i = 0; i < NS; i++) BUF[i<<1] = 36;
}






void lcd_begin()
{
	vdest = fb.ptr + ((fb.w*fb.pelsize)>>1) - (80*fb.pelsize) + ((fb.h>>1) - 72) * fb.pitch;
	WY = R_WY;
}

void lcd_refreshline()
{
	int i;
	byte *dest;
	
	if (!fb.enabled) return;
	
	if (!(R_LCDC & 0x80))
		return; /* should not happen... */

	L = R_LY;
	X = R_SCX;
	Y = (R_SCY + L) & 0xff;
	S = X >> 3;
	T = Y >> 3;
	U = X & 7;
	V = Y & 7;
	
	WX = R_WX - 7;
	if (WY>L || WY<0 || WY>143 || WX<-7 || WX>159 || !(R_LCDC&0x20))
		WX = 160;
	WT = (L - WY) >> 3;
	WV = (L - WY) & 7;

	spr_enum();

	tilebuf();
	if (hw.cgb)
	{
		bg_scan_color();
		wnd_scan_color();
		if (NS)
		{
			bg_scan_pri();
			wnd_scan_pri();
		}
	}
	else
	{
		bg_scan();
		wnd_scan();
		recolor(BUF+WX, 0x04, 160-WX);
	}
	spr_scan();

	if (fb.dirty) memset(fb.ptr, 0, fb.pitch * fb.h);
	fb.dirty = 0;

	dest = vdest;
	
	switch (fb.pelsize)
	{
	case 1:
		refresh_1((byte*)dest, BUF, PAL1, 160);
		break;
	case 2:
		refresh_2((un16*)dest, BUF, PAL2, 160);
		break;
	case 3:
		refresh_3((byte*)dest, BUF, PAL4, 160);
		break;
	case 4:
		refresh_4((un32*)dest, BUF, PAL4, 160);
		break;
	}

	vdest += fb.pitch;
}







static void updatepalette(int i)
{
	int c, r, g, b, y, u, v, rr, gg;

	c = (lcd.pal[i<<1] | ((int)lcd.pal[(i<<1)|1] << 8)) & 0x7FFF;
	r = (c & 0x001F) << 3;
	g = (c & 0x03E0) >> 2;
	b = (c & 0x7C00) >> 7;
	r |= (r >> 5);
	g |= (g >> 5);
	b |= (b >> 5);

	if (usefilter && (filterdmg || hw.cgb))
	{
		rr = ((r * filter[0][0] + g * filter[0][1] + b * filter[0][2]) >> 8) + filter[0][3];
		gg = ((r * filter[1][0] + g * filter[1][1] + b * filter[1][2]) >> 8) + filter[1][3];
		b = ((r * filter[2][0] + g * filter[2][1] + b * filter[2][2]) >> 8) + filter[2][3];
		r = rr;
		g = gg;
	}
	
	if (fb.yuv)
	{
		y = (((r *  263) + (g * 516) + (b * 100)) >> 10) + 16;
		u = (((r *  450) - (g * 377) - (b *  73)) >> 10) + 128;
		v = (((r * -152) - (g * 298) + (b * 450)) >> 10) + 128;
		if (y < 0) y = 0; if (y > 255) y = 255;
		if (u < 0) u = 0; if (u > 255) u = 255;
		if (v < 0) v = 0; if (v > 255) v = 255;
		PAL4[i] = (y<<fb.cc[0].l) | (y<<fb.cc[3].l)
			| (u<<fb.cc[1].l) | (v<<fb.cc[2].l);
		return;
	}
	

	r = (r >> fb.cc[0].r) << fb.cc[0].l;
	g = (g >> fb.cc[1].r) << fb.cc[1].l;
	b = (b >> fb.cc[2].r) << fb.cc[2].l;
	c = r|g|b;
	
	switch (fb.pelsize)
	{
	case 1:
		PAL1[i] = c;
		PAL2[i] = (c<<8) | c;
		PAL4[i] = (c<<24) | (c<<16) | (c<<8) | c;
		break;
	case 2:
		PAL2[i] = c;
		PAL4[i] = (c<<16) | c;
		break;
	case 3:
	case 4:
		PAL4[i] = c;
		break;
	}
}

void pal_write(int i, byte b)
{
	if (lcd.pal[i] == b) return;
	lcd.pal[i] = b;
	updatepalette(i>>1);
}

void pal_write_dmg(int i, int mapnum, byte d)
{
	int j;
	int *cmap = dmg_pal[mapnum];
	int c, r, g, b;

	if (hw.cgb) return;

	/* if (mapnum >= 2) d = 0xe4; */
	for (j = 0; j < 8; j += 2)
	{
		c = cmap[(d >> j) & 3];
		r = (c & 0xf8) >> 3;
		g = (c & 0xf800) >> 6;
		b = (c & 0xf80000) >> 9;
		c = r|g|b;
		/* FIXME - handle directly without faking cgb */
		pal_write(i+j, c & 0xff);
		pal_write(i+j+1, c >> 8);
	}
}

void vram_write(int a, byte b)
{
	int n, i;
	lcd.vbank[R_VBK&1][a] = b;
	if (a >= 0x1800) return;
	n=(((R_VBK&1)<<9)+(a>>4));
	for (i=0; i<4; i++) {
		if (patcacheptr[n+1024*i]!=NULL) {
			patcacheptr[n+1024*i]->pat=-1;
			patcacheptr[n+1024*i]=NULL;
		}
	}
}

void vram_dirty() {
	int i;
	for (i=0; i<CACHED_PATPIX_NO; i++) {
		patcache[i].pat=-1;
	}
	for (i=0; i<4096; i++) {
		patcacheptr[i]=NULL;
	}
}

void pal_dirty()
{
	int i;
	if (!hw.cgb)
	{
		pal_write_dmg(0, 0, R_BGP);
		pal_write_dmg(8, 1, R_BGP);
		pal_write_dmg(64, 2, R_OBP0);
		pal_write_dmg(72, 3, R_OBP1);
	}
	for (i = 0; i < 64; i++) {
		updatepalette(i);
	}
}

void lcd_reset()
{
	memset(&lcd, 0, sizeof lcd);
	vram_dirty();
	lcd_begin();
	pal_dirty();
}
















