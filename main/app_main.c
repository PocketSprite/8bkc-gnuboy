#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "rombank.h"
#include "save.h"
#include "hw.h"
#include <string.h>

#include "emu.h"
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
#include "esp_log.h"
#include "loader.h"

#include "8bkc-hal.h"
#include "8bkc-ugui.h"
#include "ugui.h"
#include "8bkcgui-widgets.h"
#include "appfs.h"
#include "powerbtn_menu.h"

#include "nvs.h"
#include "nvs_flash.h"

char statefile[128];

static void debug_screen() {
	kcugui_cls();
	UG_FontSelect(&FONT_6X8);
	UG_SetForecolor(C_WHITE);
	UG_PutString(0, 0, "INFO");
	UG_SetForecolor(C_YELLOW);
	UG_PutString(0, 16, "GnuBoy");
	UG_PutString(0, 24, "Gitrev");
	UG_SetForecolor(C_WHITE);
	UG_PutString(0, 32, GITREV);
	UG_SetForecolor(C_YELLOW);
	UG_PutString(0, 40, "Compiled");
	UG_SetForecolor(C_WHITE);
	UG_PutString(0, 48, COMPILEDATE);
	kcugui_flush();

	while (kchal_get_keys()&KC_BTN_SELECT) vTaskDelay(100/portTICK_RATE_MS);
	while (!(kchal_get_keys()&KC_BTN_SELECT)) vTaskDelay(100/portTICK_RATE_MS);
}

static int fccallback(int button, char **glob, char **desc, void *usrptr) {
	if (button & KC_BTN_POWER) {
		int r=powerbtn_menu_show(kcugui_get_fb());
		//No need to save state or anything as we're not in a game.
		if (r==POWERBTN_MENU_EXIT) kchal_exit_to_chooser();
		if (r==POWERBTN_MENU_POWERDOWN) kchal_power_down();
	}
	if (button & KC_BTN_SELECT) debug_screen();
	return 0;
}

#define STATE_TMP_FILE "__gnuboy_statefile.tmp"

void gnuboyTask(void *pvParameters) {
	char rom[128]="";
	nvs_handle nvsh=kchal_get_app_nvsh();;
	//Let other threads start
	vTaskDelay(200/portTICK_PERIOD_MS);

	int ret;
	int loadState=1;

	unsigned int size=sizeof(rom);
	esp_err_t r=nvs_get_str(nvsh, "rom", rom, &size);

	while(1) {
		int emuRan=0;
		if (strlen(rom)>0 && appfsExists(rom)) {
			//Figure out name for statefile
			strcpy(statefile, rom);
			char *dot=strrchr(statefile, '.');
			if (dot==NULL) dot=statefile+strlen(statefile);
			strcpy(dot, ".state");
			printf("State file: %s\n", statefile);
			//Kill rom str so when emu crashes, we don't try to load again
			nvs_set_str(nvsh, "rom", "");
			//Run emu
			kchal_sound_mute(0);
			ret=gnuboymain(rom, loadState);
			//Note: ret will never be EMU_RUN_RESET (or EMU_RUN_CONT)  as that is handled inside the 
			//emulator code itself (ref emu_run)
			emuRan=1;
		} else {
			ret=EMU_RUN_NEWROM;
		}

		//ToDo: Saving state on reset is janky, but atm it's the best way, given the entire emu is unloaded/loaded.
		if (ret==EMU_RUN_NEWROM || ret==EMU_RUN_POWERDOWN || ret==EMU_RUN_EXIT) {
			//Saving state only makes sense when emu actually ran...
			if (emuRan) {
				//Save state
				appfs_handle_t fd;
				int len=(1<<16);
				if (mbc.ramsize > 1) len=(2<<16); //need more data for larger sram

				r=appfsCreateFile(STATE_TMP_FILE, len, &fd);
				if (r!=ESP_OK) {
					//Not enough room... delete old state file and retry
					appfsDeleteFile(statefile);
					r=appfsCreateFile(STATE_TMP_FILE, len, &fd);
				}

				if (r!=ESP_OK) {
					printf("Couldn't create save state %s: %d\n", statefile, r);
				} else {
					savestate(fd);
					appfsClose(fd);
					appfsRename(STATE_TMP_FILE, statefile);
				}
			}
		}
		if (emuRan) loader_unload(); //free SRAM etc.

		if (ret==EMU_RUN_NEWROM) {
			kcugui_init();
			appfs_handle_t f=kcugui_filechooser("*.gb,*.gbc", "SELECT ROM", fccallback, NULL, 0);
			const char *rrom;
			appfsEntryInfo(f, &rrom, NULL);
			strncpy(rom, rrom, sizeof(rom));
			printf("Selected ROM %s\n", rom);
			kcugui_deinit();
			loadState=1;
		} else if (ret==EMU_RUN_POWERDOWN) {
			nvs_set_str(nvsh, "rom", rom);
			break;
		} else if (ret==EMU_RUN_EXIT) {
			printf("Exiting to chooser...\n");
			kchal_exit_to_chooser();
		}
	}
	kchal_power_down();
}

void lineTask();

int frames; //used in lcd

void monTask() {
	while(1) {
		vTaskDelay(1000/portTICK_PERIOD_MS);
		printf("Fps: %d\n", frames);
		printf("Free mem: dram %d\n", xPortGetFreeHeapSize());
//		heap_caps_print_heap_info(0);
		frames=0;
	}
//	vTaskDelete(NULL);
}


#include "cpuregs.h"

void startEmuHook() {
	const esp_partition_t* part;
	if (kchal_get_keys()&KC_BTN_START) return;
	if (appfsExists(statefile)) {
		loadstate(appfsOpen(statefile));
		rombankStateLoaded();
	} else {
		printf("Couldn't find state part!\n");
	}
	vram_dirty();
	pal_dirty();
	sound_dirty();
	mem_updatemap();
	printf("Save state boot rom loaded: %d\n", bootromLoaded);
}

#define BOOTROM_NAME "gbcrom.bin"

void app_main()
{
	spi_flash_mmap_handle_t hbootrom;
	esp_err_t err;
	esp_log_level_set("*", ESP_LOG_INFO);
	esp_log_level_set("appfs", ESP_LOG_DEBUG);
	kchal_init();
	nvs_flash_init();
	
	hw.gbbootromdata=NULL;
	if (appfsExists(BOOTROM_NAME)) {
		appfs_handle_t fd=appfsOpen(BOOTROM_NAME);
		err=appfsMmap(fd, 0, 2304, (const void**)&hw.gbbootromdata, SPI_FLASH_MMAP_DATA, &hbootrom);
		if (err==ESP_OK) {
			printf("Initialized. bootROM@%p\n", hw.gbbootromdata);
		} else {
			printf("Couldn't map bootrom appfs file!\n");
		}
	} else {
		printf("No bootrom found!\n");
	}
	xTaskCreatePinnedToCore(&lineTask, "lineTask", 1024, NULL, 6, NULL, 1);
	xTaskCreatePinnedToCore(&gnuboyTask, "gnuboyTask", 1024*6, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&monTask, "monTask", 1024*2, NULL, 7, NULL, 0);
}


