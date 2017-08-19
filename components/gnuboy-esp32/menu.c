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

static nvs_handle nvs;

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
			if (i&0x80000000) ovl[(dy+y)*80+(dx+x+8)]=i;
		}
	}
}


void gbfemtoMenuInit() {
	esp_err_t r;
	int contrast=128, volume=128;
	r=nvs_open("gb", NVS_READWRITE, &nvs);
	if (r!=ESP_OK) ets_printf("nvs_open fail\n");
	nvs_get_i32(nvs, "vol", &volume);
	nvs_get_i32(nvs, "bgt", &contrast);
	pcm_setvolume(volume);
	kchal_set_contrast(contrast);
}
#define SCROLLSPD 4

#define SCN_VOLUME 0
#define SCN_BRIGHT 1
#define SCN_CHROM 2
#define SCN_PWRDWN 3

void pcm_mute();

//Show in-game menu reachable by pressing the power button
void gbfemtoShowMenu() {
	int io, newIo, oldIo=0;
	int menuItem=0;
	int prevItem=0;
	int scroll=0;
	uint32_t *overlay=vidGetOverlayBuf();
	while(1) {
		esp_task_wdt_feed();
		vTaskDelay(20);
		memset(overlay, 0, 96*64*4);
		newIo=kchal_get_keys();
		//Filter out only newly pressed buttons
		io=(oldIo^newIo)&newIo;
		oldIo=newIo;
		if (!emu_running) return;
		if (io&PAD_UP && !scroll) {
			menuItem++;
			if (menuItem>=4) menuItem=0;
			scroll=-SCROLLSPD;
		}
		if (io&KC_BTN_DOWN && !scroll) {
			menuItem--;
			if (menuItem<0) menuItem=3;
			scroll=SCROLLSPD;
		}
		if ((io&KC_BTN_LEFT) || (io&KC_BTN_RIGHT)) {
			int v=128;
			if (menuItem==SCN_VOLUME) nvs_get_i32(nvs, "vol", &v);
			if (menuItem==SCN_BRIGHT) nvs_get_i32(nvs, "bgt", &v);
			if (io&PAD_LEFT) v-=4;
			if (io&PAD_RIGHT) v+=4;
			if (v<0) v=0;
			if (v>255) v=255;
			if (menuItem==SCN_VOLUME) {
				nvs_set_i32(nvs, "vol", v);
				pcm_setvolume(v);
			}
			if (menuItem==SCN_BRIGHT) {
				nvs_set_i32(nvs, "bgt", v);
				kchal_set_contrast(v);
			}
		}
		if ((io&KC_BTN_A) || (io&KC_BTN_B)) {
			if (menuItem==SCN_PWRDWN) kchal_power_down();
//			if (menuItem==SCN_CHROM) romSelect();
		}

		if (io&KC_BTN_START) return;

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
		
		if (scroll==0 && (menuItem==SCN_VOLUME || menuItem==SCN_BRIGHT)) {
			int v;
			if (menuItem==SCN_VOLUME) nvs_get_i32(nvs, "vol", &v);
			if (menuItem==SCN_BRIGHT) nvs_get_i32(nvs, "bgt", &v);
			if (v<0) v=0;
			if (v>255) v=255;
			renderGfx(overlay, 14, 25+16, 14, 129, (v*60)/256, 3);
		}
		
		vidRenderOverlay();
		//Send out dummy sound
		pcm_mute();
	}
}

