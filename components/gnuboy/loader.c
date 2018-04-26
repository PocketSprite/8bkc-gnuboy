#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <string.h>

#include "defs.h"
#include "regs.h"
#include "mem.h"
#include "hw.h"
#include "rtc.h"
#include "lcd.h"
#include "inflate.h"
#include "save.h"
#include "sound.h"
#include "sys.h"

#include "rombank.h"
#include "esp_heap_caps.h"

static const int mbc_table[256] =
{
	0, 1, 1, 1, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3,
	3, 3, 3, 3, 0, 0, 0, 0, 0, 5, 5, 5, MBC_RUMBLE, MBC_RUMBLE, MBC_RUMBLE, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MBC_HUC3, MBC_HUC1
};

static const int rtc_table[256] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static const int batt_table[256] =
{
	0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
	1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	0
};

static const int romsize_table[256] =
{
	2, 4, 8, 16, 32, 64, 128, 256, 512,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 128, 128, 128
	/* 0, 0, 72, 80, 96  -- actual values but bad to use these! */
};

static const int ramsize_table[256] =
{
	1, 1, 1, 4, 16,
	4 /* FIXME - what value should this be?! */
};


static char *romfile;
static char *sramfile;
static char *rtcfile;
static char *saveprefix;

static char *savename;
static char *savedir;

static int saveslot;

static int forcebatt, nobatt;
static int forcedmg, gbamode;

static int memfill = -1, memrand = -1;


static void initmem(void *mem, int size)
{
	char *p = mem;
	if (memrand >= 0)
	{
		srand(memrand ? memrand : time(0));
		while(size--) *(p++) = rand();
	}
	else if (memfill >= 0)
		memset(p, memfill, size);
}

void rom_unload() {
	rombankUnload();
}

int rom_load()
{
	FILE *f;
	byte c, *data, *header;
	int len = 0, rlen, i;

	rombankLoad(romfile);
	data = getRomBank(0);
	header = data;
	
	memcpy(rom.name, header+0x0134, 16);
	if (rom.name[14] & 0x80) rom.name[14] = 0;
	if (rom.name[15] & 0x80) rom.name[15] = 0;
	rom.name[16] = 0;

	c = header[0x0147];
	mbc.type = mbc_table[c];
	mbc.batt = (batt_table[c] && !nobatt) || forcebatt;
	rtc.batt = rtc_table[c];
	mbc.romsize = romsize_table[header[0x0148]];
	mbc.ramsize = ramsize_table[header[0x0149]];

	if (!mbc.romsize) {
		printf("unknown ROM size %02X\n", header[0x0148]);
		return 0;
	}
	if (!mbc.ramsize) {
		printf("unknown SRAM size %02X\n", header[0x0149]);
		return 0;
	}

	rlen = 16384 * mbc.romsize;
	
	if (mbc.ramsize > 4) {
		printf("Header says cart has %d * 8K of save RAM. We don't have that; trying with less.\n", mbc.ramsize);
		mbc.ramsize=4;
	}
	
	ram.prev_rambank=-1;
	ram.sbank = malloc(8192); //swap space
	if (ram.sbank==NULL) {
		printf("Can't allocate SRAM swap area!\n");
		return 0;
	}
	
	ram.sbanks = calloc(sizeof(byte*), mbc.ramsize);
	if (ram.sbanks==NULL) {
		printf("Can't allocate SRAM pointers!\n");
		return 0;
	}

	for (int i=0; i<mbc.ramsize; i++) {
		ram.sbanks[i] = heap_caps_malloc(8192,  MALLOC_CAP_32BIT); //allocate in iram
		if (ram.sbanks[i]==NULL) {
			printf("Can't allocate 8K bytes for SRAM bank %d in IRAM!\n", i);
			return 0;
		}
		select_rambank(i);
		initmem(ram.sbank, 8192);
	}
	select_rambank(0);

	printf("%d banks of 8K memory, %d rom banks of 16K initialized.\n", mbc.ramsize, mbc.romsize);

	initmem(ram.ibank, 4096 * 8);

	mbc.rombank = 1;
	mbc.rambank = 0;

	if (hw.gbbootromdata) {
		//cgb boot rom will figure things out
		hw.cgb=1;
	} else {
		//need to do setup ourselves
		c = header[0x0143];
		hw.cgb = ((c == 0x80) || (c == 0xc0)) && !forcedmg;
		hw.gba = (hw.cgb && gbamode);
	}

	return 1;
}


void loader_unload()
{
	rombankUnload();
	//if (romfile) free(romfile);
	if (sramfile) free(sramfile);
	if (saveprefix) free(saveprefix);
	printf("loader_unload: freeing %d sram banks\n", mbc.ramsize);
	for (int i=0; i<mbc.ramsize; i++) {
		free(ram.sbanks[i]);
	}
	free(ram.sbanks);
	if (ram.sbank) free(ram.sbank);
	romfile = sramfile = saveprefix = 0;
	ram.sbank = 0;
	mbc.type = mbc.romsize = mbc.ramsize = mbc.batt = 0;
}

static char *base(char *s)
{
	char *p;
	p = strrchr(s, '/');
	if (p) return p+1;
	return s;
}

static void cleanup()
{
//	sram_save();
//	rtc_save();
	/* IDEA - if error, write emergency savestate..? */
}

int loader_init(char *s)
{
	char *name, *p;
#if 0
	sys_checkdir(savedir, 1); /* needs to be writable */
#endif

	romfile = s;
	int r=rom_load();
	if (!r) return r;

	vid_settitle(rom.name);
	return r;
}

