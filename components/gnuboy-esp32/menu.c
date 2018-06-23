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
#define SCN_EXIT 5

#define SCN_COUNT 6

void pcm_mute();

void gbfemtoShowSnapshotting(uint32_t *overlay) {
	memset(overlay, 0, 80*64*4);
	renderGfx(overlay, 0, 14, 0, 268, 80, 40);
	vidRenderOverlay();
}

//Show in-game menu reachable by pressing the power button
int gbfemtoShowMenu() {
	int io, newIo, oldIo=0;
	int menuItem=0;
	int prevItem=0;
	int scroll=0;
	int doRefresh=1;
	int powerReleased=0;
	int oldArrowsTick=-1;
	uint32_t *overlay=vidGetOverlayBuf();
	kchal_sound_mute(1);
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
		if ((newIo&KC_BTN_LEFT) || (newIo&KC_BTN_RIGHT)) {
			int v=128;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (menuItem==SCN_BRIGHT) v=kchal_get_brightness();
			if (newIo&PAD_LEFT) v-=2;
			if (newIo&PAD_RIGHT) v+=2;
			if (v<0) v=0;
			if (v>255) v=255;
			if (menuItem==SCN_VOLUME) {
				kchal_set_volume(v);
				doRefresh=1;
			}
			if (menuItem==SCN_BRIGHT) {
				kchal_set_brightness(v);
				doRefresh=1;
			}
		}
		if ((io&KC_BTN_A) || (io&KC_BTN_B)) {
			if (menuItem==SCN_PWRDWN) {
				gbfemtoShowSnapshotting(overlay);
				return EMU_RUN_POWERDOWN;
			}
			if (menuItem==SCN_CHROM) {
				gbfemtoShowSnapshotting(overlay);
				return EMU_RUN_NEWROM;
			}
			if (menuItem==SCN_RESET) {
				kchal_sound_mute(0);
				return EMU_RUN_RESET;
			}
			if (menuItem==SCN_EXIT) {
				gbfemtoShowSnapshotting(overlay);
				return EMU_RUN_EXIT;
			}
		}
		if (io&KC_BTN_POWER_LONG) {
			gbfemtoShowSnapshotting(overlay);
			return EMU_RUN_POWERDOWN;
		}

		if (!(io&KC_BTN_POWER)) powerReleased=1;
		if (io&KC_BTN_START || (powerReleased && (io&KC_BTN_POWER))) {
			kchal_sound_mute(0);
			return EMU_RUN_CONT;
		}

		if (scroll>0) scroll+=SCROLLSPD;
		if (scroll<0) scroll-=SCROLLSPD;
		if (scroll>64 || scroll<-64) {
			prevItem=menuItem;
			scroll=0;
			doRefresh=1; //show last scroll thing
		}
		if (prevItem!=menuItem) renderGfx(overlay, 0, 16+scroll, 0,32*prevItem,80,32);
		if (scroll) {
			doRefresh=1;
			renderGfx(overlay, 0, 16+scroll+((scroll>0)?-64:64), 0,32*menuItem,80,32);
			oldArrowsTick=-1; //to force arrow redraw
		} else {
			renderGfx(overlay, 0, 16, 0,32*menuItem,80,32);
			//Render arrows
			int t=xTaskGetTickCount()/(400/portTICK_PERIOD_MS);
			t=(t&1);
			if (t!=oldArrowsTick) {
				doRefresh=1;
				renderGfx(overlay, 36, 0, t?0:8, 308, 8, 8);
				renderGfx(overlay, 36, 56, t?16:24, 308, 8, 8);
				oldArrowsTick=t;
			}
		}
		
		//Handle volume/brightness bars
		if (scroll==0 && (menuItem==SCN_VOLUME || menuItem==SCN_BRIGHT)) {
			int v=0;
			if (menuItem==SCN_VOLUME) v=kchal_get_volume();
			if (menuItem==SCN_BRIGHT) v=kchal_get_brightness();
			if (v<0) v=0;
			if (v>255) v=255;
			renderGfx(overlay, 14, 25+16, 14, 193, (v*60)/256, 4);
		}
		
		if (doRefresh) {
			vidRenderOverlay();
		}
		doRefresh=0;
		vTaskDelay(20/portTICK_PERIOD_MS);
	}
}

