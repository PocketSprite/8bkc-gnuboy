#include <stdlib.h>
#include <stdio.h>

#include "rom/ets_sys.h"
#include "fb.h"
#include "lcd.h"
#include <string.h>
#include "sound.h"
#include "nvs.h"
#include "8bkc-hal.h"
#include "emu.h"
#include "hw.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"

#include "emu.h"

const char graphics[]={
#include "graphics.inc"
};


void renderGfx(uint32_t *ovl, int dx, int dy, int sx, int sy, int sw, int sh) {
	uint32_t *gfx=(uint32_t*)graphics;
	int x, y, i;
	if (dx<0) {
		sx-=dx;
		sw+=dx;
		dx=0;
	}
	if ((dx+sw)>80) {
		sw-=((dx+sw)-80);
		dx=80-sw;
	}
	if (dy<0) {
		sy-=dy;
		sh+=dy;
		dy=0;
	}
	if ((dy+sh)>64) {
		sh-=((dy+sh)-64);
		dy=64-sh;
	}

	for (y=0; y<sh; y++) {
		for (x=0; x<sw; x++) {
			i=gfx[(sy+y)*80+(sx+x)];
			if (i&0x80000000) ovl[(dy+y)*80+(dx+x)]=i;
		}
	}
}


void gbfemtoMenuInit() {
}

#define SCROLLSPD 4

#define SCN_VOLUME 0
#define SCN_BRIGHT 1
#define SCN_CHROM 2
#define SCN_PWRDWN 3
#define SCN_RESET 4

#define SCN_COUNT 5

void pcm_mute();

//Show in-game menu reachable by pressing the power button
int gbfemtoShowMenu() {
	int io, newIo, oldIo=0;
	int menuItem=0;
	int prevItem=0;
	int scroll=0;
	uint32_t *overlay=vidGetOverlayBuf();
	while(1) {
		esp_task_wdt_feed();
		memset(overlay, 0, 80*64*4);
		newIo=kchal_get_keys();
		//Filter out only newly pressed buttons
		io=(oldIo^newIo)&newIo;
		oldIo=newIo;
		if (io&PAD_UP && !scroll) {
			menuItem++;
			if (menuItem>=SCN_COUNT) menuItem=0;
			scroll=-SCROLLSPD;
		}
		if (io&KC_BTN_DOWN && !scroll) {
			menuItem--;
			if (menuItem<0) menuItem=SCN_COUNT-1;
			scroll=SCROLLSPD;
		}
		if ((io&KC_BTN_LEFT) || (io&KC_BTN_RIGHT)) {
			int v=128;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (menuItem==SCN_BRIGHT) v=kchal_get_contrast();
			if (io&PAD_LEFT) v-=4;
			if (io&PAD_RIGHT) v+=4;
			if (v<0) v=0;
			if (v>255) v=255;
			if (menuItem==SCN_VOLUME) {
				kchal_set_volume(v);
			}
			if (menuItem==SCN_BRIGHT) {
				kchal_set_contrast(v);
			}
		}
		if ((io&KC_BTN_A) || (io&KC_BTN_B)) {
			if (menuItem==SCN_PWRDWN) {
				return EMU_RUN_POWERDOWN;
			}
			if (menuItem==SCN_CHROM) {
				return EMU_RUN_NEWROM;
			}
			if (menuItem==SCN_RESET) {
				return EMU_RUN_RESET;
			}
		}

		if (io&KC_BTN_START) return EMU_RUN_CONT;

		if (scroll>0) scroll+=SCROLLSPD;
		if (scroll<0) scroll-=SCROLLSPD;
		if (scroll>64 || scroll<-64) {
			prevItem=menuItem;
			scroll=0;
		}
		if (prevItem!=menuItem) renderGfx(overlay, 0, 16+scroll, 0,32*prevItem,80,32);
		if (scroll) {
			renderGfx(overlay, 0, 16+scroll+((scroll>0)?-64:64), 0,32*menuItem,80,32);
		} else {
			renderGfx(overlay, 0, 16, 0,32*menuItem,80,32);
		}
		
		//Handle volume/brightness bars
		if (scroll==0 && (menuItem==SCN_VOLUME || menuItem==SCN_BRIGHT)) {
			int v=0;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (menuItem==SCN_BRIGHT) v=kchal_get_contrast();
			if (v<0) v=0;
			if (v>255) v=255;
			renderGfx(overlay, 14, 25+16, 14, 129, (v*60)/256, 3);
		}
		
		vidRenderOverlay();
		//Send out dummy sound
		pcm_mute();
		vTaskDelay(20/portTICK_PERIOD_MS);
	}
}

