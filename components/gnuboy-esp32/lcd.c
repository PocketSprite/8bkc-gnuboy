
#include <stdlib.h>
#include <stdio.h>

#include "rom/ets_sys.h"
#include "fb.h"
#include "lcd.h"
#include <string.h>
#include "8bkc-hal.h"
#include "menu.h"

#include "esp_task_wdt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"



static uint16_t *frontbuff=NULL, *backbuff=NULL;
static volatile uint16_t *toRender=NULL;
static volatile uint16_t *overlay=NULL;
struct fb fb;
static SemaphoreHandle_t renderSem;

static bool doShutdown=false;

void vid_preinit()
{
}

void gnuboy_esp32_videohandler();
void lineTask();

void videoTask(void *pvparameters) {
	gnuboy_esp32_videohandler();
}


void vid_init()
{
	doShutdown=false;
	frontbuff=malloc(160*144*2);
	backbuff=malloc(160*144*2);
	fb.w = 160;
	fb.h = 144;
	fb.pelsize = 2;
	fb.pitch = 160*2;
	fb.ptr = (unsigned char*)frontbuff;
	fb.enabled = 1;
	fb.dirty = 0;

	fb.indexed = 0;
	fb.cc[0].r = fb.cc[2].r = 3;
	fb.cc[1].r = 2;
	fb.cc[0].l = 11;
	fb.cc[1].l = 5;
	fb.cc[2].l = 0;
	
	gbfemtoMenuInit();
	memset(frontbuff, 0, 160*144*2);
	memset(backbuff, 0, 160*144*2);

	renderSem=xSemaphoreCreateBinary();
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024*2, NULL, 5, NULL, 1);
	ets_printf("Video inited.\n");
}


void vid_close()
{
	doShutdown=true;
	xSemaphoreGive(renderSem);
	vTaskDelay(100); //wait till video thread shuts down... pretty dirty
	free(frontbuff);
	free(backbuff);
	vQueueDelete(renderSem);
}

void vid_settitle(char *title)
{
}

void vid_setpal(int i, int r, int g, int b)
{
}

extern int patcachemiss, patcachehit, frames;


void vid_begin()
{
//	vram_dirty(); //use this to find out a viable size for patcache
	frames++;
	patcachemiss=0;
	patcachehit=0;
	esp_task_wdt_feed();
}

void vid_end()
{
	overlay=NULL;
	toRender=(uint16_t*)fb.ptr;
	xSemaphoreGive(renderSem);
	if (fb.ptr == (unsigned char*)frontbuff ) {
		fb.ptr = (unsigned char*)backbuff;
	} else {
		fb.ptr = (unsigned char*)frontbuff;
	}
//	printf("Pcm %d pch %d\n", patcachemiss, patcachehit);
}

uint32_t *vidGetOverlayBuf() {
	return (uint32_t*)fb.ptr;
}

void vidRenderOverlay() {
	overlay=(uint16_t*)fb.ptr;
	if (fb.ptr == (unsigned char*)frontbuff ) toRender=(uint16_t*)backbuff; else toRender=(uint16_t*)frontbuff;
	xSemaphoreGive(renderSem);
}

void kb_init()
{
}

void kb_close()
{
}

void kb_poll()
{
}

void ev_poll()
{
	kb_poll();
}


uint16_t oledfb[80*64];

//Averages four pixels into one
int getAvgPix(uint16_t* bufs, int pitch, int x, int y) {
	int col;
	if (x<0 || x>=160) return 0;
	//16-bit: E79C
	//15-bit: 739C
	col=(bufs[x+(y*(pitch>>1))]&0xE79C)>>2;
	col+=(bufs[(x+1)+(y*(pitch>>1))]&0xE79C)>>2;
	col+=(bufs[x+((y+1)*(pitch>>1))]&0xE79C)>>2;
	col+=(bufs[(x+1)+((y+1)*(pitch>>1))]&0xE79C)>>2;
	return col&0xffff;
}

//Averages four pixels into one, but does subpixel rendering to give a slightly higher
//X resolution at the cost of color fringing.
//Bitmasks:
//RRRR.RGGG.GGGB.BBBB
//1111.0111.1101.1110 = F7DE
//0000.1000.0010.0001 = 0821
//1111.1000.0000.0000 = F800
//0000.0111.1110.0000 = 07E0
//0000.0000.0001.1111 = 001F
//so (RGB565val&0xF7DE)>>1 halves the R, G, B color components.
int getAvgPixSubpixrendering(uint16_t* bufs, int pitch, int x, int y) {
	uint32_t *pixduo=(uint32_t*)bufs;
	if (x<0 || x>=160) return 0;
	//Grab top and bottom two pixels.
	uint32_t c1=pixduo[(x/2)+(y*(pitch>>2))];
	uint32_t c2=pixduo[(x/2)+((y+1)*(pitch>>2))];
	//Average the two.
	uint32_t c=((c1&0xF7DEF7DE)+(c2&0xF7DEF7DE))>>1;
	//The averaging action essentially killed the least significant bit of all colors; if
	//both were one the resulting color should be one more. Compensate for that here.
	c+=(c1&c1)&0x08210821;

	//Take the various components from the pixels and return the composite.
	uint32_t red_comp=c&0xF800;
	uint32_t green_comp=c&0x07E0;
	green_comp+=(c>>16)&0x07E0;
	green_comp=(green_comp/2)&0x7E0;
	uint32_t blue_comp=(c>>16)&0x001F;
	return red_comp+green_comp+blue_comp;
}

//Averages 6 pixels into one (area of w=2, h=3), but does subpixel rendering to give a slightly higher
//X resolution at the cost of color fringing. This is slightly more elaborate as we cannot just use additions,
//shifts and bitmasks.
#define RED(i) (((i)>>11) & 0x1F)
#define GREEN(i) (((i)>>5) & 0x3F)
#define BLUE(i) (((i)>>0) & 0x1F)
int getAvgPixSubpixrenderingThreeLines(uint16_t* bufs, int pitch, int x, int y) {
	int r=0, g=0, b=0;
	for (int line=0; line<3; line++) {
		r+=RED(bufs[x+((y+line)*(pitch>>1))]);
		g+=GREEN(bufs[x+((y+line)*(pitch>>1))]);
		g+=GREEN(bufs[x+1+((y+line)*(pitch>>1))]);
		b+=BLUE(bufs[x+1+((y+line)*(pitch>>1))]);
	}
	r=r/3;
	g=g/6;
	b=b/3;
	return (r<<11)+(g<<5)+(b);
}



int addOverlayPixel(uint16_t p, uint32_t ov) {
	int or, og, ob, a;
	int br, bg, bb;
	int r,g,b;
	br=((p>>11)&0x1f)<<3;
	bg=((p>>5)&0x3f)<<2;
	bb=((p>>0)&0x1f)<<3;

	a=(ov>>24)&0xff;
	//hack: Always show background darker
	a=(a/2)+128;

	ob=(ov>>16)&0xff;
	og=(ov>>8)&0xff;
	or=(ov>>0)&0xff;

	r=(br*(256-a))+(or*a);
	g=(bg*(256-a))+(og*a);
	b=(bb*(256-a))+(ob*a);

	return ((r>>(3+8))<<11)+((g>>(2+8))<<5)+((b>>(3+8))<<0);
}

//This thread runs on core 1.
void gnuboy_esp32_videohandler() {
	int x, y;
	uint16_t *oledfbptr;
	uint16_t c;
	uint32_t *ovl;
	volatile uint16_t *rendering;
	printf("Video thread running\n");
	memset(oledfb, 0, sizeof(oledfb));
	while(!doShutdown) {
		int ry; //Y on screen
		//if (toRender==NULL) 
		xSemaphoreTake(renderSem, portMAX_DELAY);
		rendering=toRender;
		ovl=(uint32_t*)overlay;
		oledfbptr=oledfb;
		ry=0;
		for (y=0; y<64; y++) {
			int doThreeLines=((y%4)==0);
			for (x=0; x<80; x++) {
				if (!doThreeLines) {
					c=getAvgPixSubpixrendering((uint16_t*)rendering, 160*2, (x*2), ry);
				} else {
					c=getAvgPixSubpixrenderingThreeLines((uint16_t*)rendering, 160*2, (x*2), ry);
				}
				if (ovl) c=addOverlayPixel(c, *ovl++);
				*oledfbptr++=(c>>8)+((c&0xff)<<8);
			}
			ry+=2;
			if (doThreeLines) ry++;
		}
		kchal_send_fb(oledfb);
	}
	vTaskDelete(NULL);
}





