#include <string.h>
#include <stdio.h>

#include "defs.h"
#include "cpu.h"
#include "cpuregs.h"
#include "hw.h"
#include "regs.h"
#include "lcd.h"
#include "rtc.h"
#include "mem.h"
#include "sound.h"

#include "appfs.h"
#include <malloc.h>
#include "rombank.h"

#ifdef IS_LITTLE_ENDIAN
#define LIL(x) (x)
#else
#error "Big-endian is not supported!"
#define LIL(x) ((x<<24)|((x&0xff00)<<8)|((x>>8)&0xff00)|(x>>24))
#endif

#define I1(s, p) { 1, s, p }
#define I2(s, p) { 2, s, p }
#define I4(s, p) { 4, s, p }
#define R(r) I1(#r, &R_##r)
#define NOSAVE { -1, "\0\0\0\0", 0 }
#define END { 0, "\0\0\0\0", 0 }

struct svar
{
	int len;
	char key[4];
	void *ptr;
};

static int ver;
static int sramblock, iramblock, vramblock;
static int hramofs, hiofs, palofs, oamofs, wavofs;

const struct svar svars[] = 
{
	I4("GbSs", &ver),
	
	I2("PC  ", &PC),
	I2("SP  ", &SP),
	I2("BC  ", &BC),
	I2("DE  ", &DE),
	I2("HL  ", &HL),
	I2("AF  ", &AF),
	
	I4("IME ", &cpu.ime),
	I4("ima ", &cpu.ima),
	I4("spd ", &cpu.speed),
	I4("halt", &cpu.halt),
	I4("div ", &cpu.div),
	I4("tim ", &cpu.tim),
	I4("lcdc", &cpu.lcdc),
	I4("snd ", &cpu.snd),
	
	I1("ints", &hw.ilines),
	I1("pad ", &hw.pad),
	I4("cgb ", &hw.cgb),
	I4("gba ", &hw.gba),
	
	I4("mbcm", &mbc.model),
	I4("romb", &mbc.rombank),
	I4("ramb", &mbc.rambank),
	I4("enab", &mbc.enableram),
	I4("batt", &mbc.batt),
	
	I4("rtcR", &rtc.sel),
	I4("rtcL", &rtc.latch),
	I4("rtcC", &rtc.carry),
	I4("rtcS", &rtc.stop),
	I4("rtcd", &rtc.d),
	I4("rtch", &rtc.h),
	I4("rtcm", &rtc.m),
	I4("rtcs", &rtc.s),
	I4("rtct", &rtc.t),
	I1("rtR8", &rtc.regs[0]),
	I1("rtR9", &rtc.regs[1]),
	I1("rtRA", &rtc.regs[2]),
	I1("rtRB", &rtc.regs[3]),
	I1("rtRC", &rtc.regs[4]),

	I4("S1on", &snd.ch[0].on),
	I4("S1p ", &snd.ch[0].pos),
	I4("S1c ", &snd.ch[0].cnt),
	I4("S1ec", &snd.ch[0].encnt),
	I4("S1sc", &snd.ch[0].swcnt),
	I4("S1sf", &snd.ch[0].swfreq),

	I4("S2on", &snd.ch[1].on),
	I4("S2p ", &snd.ch[1].pos),
	I4("S2c ", &snd.ch[1].cnt),
	I4("S2ec", &snd.ch[1].encnt),
	
	I4("S3on", &snd.ch[2].on),
	I4("S3p ", &snd.ch[2].pos),
	I4("S3c ", &snd.ch[2].cnt),
	
	I4("S4on", &snd.ch[3].on),
	I4("S4p ", &snd.ch[3].pos),
	I4("S4c ", &snd.ch[3].cnt),
	I4("S4ec", &snd.ch[3].encnt),
	
	I4("hdma", &hw.hdma),
	
	I4("sram", &sramblock),
	I4("iram", &iramblock),
	I4("vram", &vramblock),
	I4("hi  ", &hiofs),
	I4("pal ", &palofs),
	I4("oam ", &oamofs),
	I4("wav ", &wavofs),
	
	I1("boot", &bootromLoaded),
	/* NOSAVE is a special code to prevent the rest of the table
	 * from being saved, used to support old stuff for backwards
	 * compatibility... */
	NOSAVE,
	END
};


void loadstate(appfs_handle_t f)
{
	esp_err_t r;
	int i, j;
	byte *buf=malloc(4096);
	un32 (*header)[2] = (un32 (*)[2])buf;
	un32 d;
	int irl = hw.cgb ? 8 : 2;
	int vrl = hw.cgb ? 4 : 2;
	int srl = mbc.ramsize << 1;

	ver = hramofs = hiofs = palofs = oamofs = wavofs = 0;

	//fseek(f, 0, SEEK_SET);
	//fread(buf, 4096, 1, f);
	r=appfsRead(f, 0, buf, 4096);
	printf("save: load header @%x len %x\n", 0, 4096);
	if (r!=ESP_OK) die("reading header");
	
	for (j = 0; header[j][0]; j++)
	{
		for (i = 0; svars[i].ptr; i++)
		{
			if (header[j][0] != *(un32 *)svars[i].key)
				continue;
			d = LIL(header[j][1]);
//			printf("Load: %s=0x%x\n", svars[i].key, d);
			switch (svars[i].len)
			{
			case 1:
				*(byte *)svars[i].ptr = d;
				break;
			case 2:
				*(un16 *)svars[i].ptr = d;
				break;
			case 4:
				*(un32 *)svars[i].ptr = d;
				break;
			}
			break;
		}
	}

	printf("Save ver: %x\n", ver);
	printf("irl %d vrl %d srl %d\n", irl, vrl, srl);
	if (ver!=0x106) {
		free(buf);
		return;
	}
	/* obsolete as of version 0x104 */
	if (hramofs) memcpy(ram.hi+128, buf+hramofs, 127);
	
	if (hiofs) memcpy(ram.hi, buf+hiofs, sizeof ram.hi);
	if (palofs) memcpy(lcd.pal, buf+palofs, sizeof lcd.pal);
	if (oamofs) memcpy(lcd.oam.mem, buf+oamofs, sizeof lcd.oam);

	if (wavofs) memcpy(snd.wave, buf+wavofs, sizeof snd.wave);
	else memcpy(snd.wave, ram.hi+0x30, 16); /* patch data from older files */

	free(buf);

	//fseek(f, iramblock<<12, SEEK_SET);
	//fread(ram.ibank, 4096, irl, f);
	r=appfsRead(f, iramblock<<12, ram.ibank, 4096*irl);
	printf("save: load iram @%x len %x\n", iramblock<<12, 4096*irl);
	if (r!=ESP_OK) die("reading iramblock");
	
	//fseek(f, vramblock<<12, SEEK_SET);
	//fread(lcd.vbank, 4096, vrl, f);
	r=appfsRead(f, vramblock<<12, lcd.vbank, 4096*vrl);
	printf("save: load vram @%x len %x\n", vramblock<<12, 4096*vrl);
	if (r!=ESP_OK) die("reading vramblock");
	
	//fseek(f, sramblock<<12, SEEK_SET);
	//fread(ram.sbank, 4096, srl, f);
	for (int i=0; i<srl; i++) {
		select_rambank(i/2);
		byte *p=ram.sbank;
		if (i&1) p+=4096;
		r=appfsRead(f, (sramblock+i)<<12, p, 4096);
		printf("save: load sram @%x blk %x\n", (sramblock+i)<<12, i);
		if (r!=ESP_OK) die("reading sramblock");
	}
}

void savestate(appfs_handle_t f)
{
	int i;
	esp_err_t r;
	byte *buf=malloc(4096);
	un32 (*header)[2] = (un32 (*)[2])buf;
	un32 d = 0;
	int irl = hw.cgb ? 8 : 2;
	int vrl = hw.cgb ? 4 : 2;
	int srl = mbc.ramsize << 1;

	ver = 0x106;
	iramblock = 1;
	vramblock = 1+irl;
	sramblock = 1+irl+vrl;
	wavofs = 4096 - 784;
	hiofs = 4096 - 768;
	palofs = 4096 - 512;
	oamofs = 4096 - 256;
	memset(buf, 0, 4096);

	for (i = 0; svars[i].len > 0; i++)
	{
		header[i][0] = *(un32 *)svars[i].key;
		switch (svars[i].len)
		{
		case 1:
			d = *(byte *)svars[i].ptr;
			break;
		case 2:
			d = *(un16 *)svars[i].ptr;
			break;
		case 4:
			d = *(un32 *)svars[i].ptr;
			break;
		}
//		printf("Save: %s=0x%x\n", svars[i].key, d);

		header[i][1] = LIL(d);
	}
	header[i][0] = header[i][1] = 0;

	memcpy(buf+hiofs, ram.hi, sizeof ram.hi);
	memcpy(buf+palofs, lcd.pal, sizeof lcd.pal);
	memcpy(buf+oamofs, lcd.oam.mem, sizeof lcd.oam);
	memcpy(buf+wavofs, snd.wave, sizeof snd.wave);

	printf("irl %d vrl %d srl %d\n", irl, vrl, srl);

	if (appfsErase(f, 0, 1<<16)!=ESP_OK) printf("ERASE FAIL!\n");
	printf("Erasing %d bytes.\n", (sramblock<<12)+4096*srl);

	//fseek(f, 0, SEEK_SET);
	//fwrite(buf, 4096, 1, f);
	r=appfsWrite(f, 0, buf, 4096);
	printf("save: write header @%x len %x\n", 0, 4096);

	free(buf);
	
	//fseek(f, iramblock<<12, SEEK_SET);
	//fwrite(ram.ibank, 4096, irl, f);
	r=appfsWrite(f, iramblock<<12, ram.ibank, 4096*irl);
	printf("save: load iram @%x len %x\n", iramblock<<12, 4096*irl);
	if (r!=ESP_OK) die("writing viramblock");
	
	//fseek(f, vramblock<<12, SEEK_SET);
	//fwrite(lcd.vbank, 4096, vrl, f);
	r=appfsWrite(f, vramblock<<12, lcd.vbank, 4096*vrl);
	printf("save: load vram @%x len %x\n", vramblock<<12, 4096*vrl);
	if (r!=ESP_OK) die("writing vramblock");
	
	//fseek(f, sramblock<<12, SEEK_SET);
	//fwrite(ram.sbank, 4096, srl, f);
	for (i=0; i<srl; i++) {
		select_rambank(i/2);
		byte *p=ram.sbank;
		if (i&1) p+=4096;
		r=appfsWrite(f, (sramblock+i)<<12, p, 4096*srl);
		printf("save: sram @%x blk %x\n", (sramblock+i)<<12, i);
		if (r!=ESP_OK) die("writing sramblock");
	}
}



















