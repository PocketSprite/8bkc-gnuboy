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

#include "8bkc-hal.h"
#include "appfs.h"

//#include "gngbsys.h"

unsigned char *gbbootromdata=NULL;

int gnuboymain(int argc, char *argv[]);
void gnuboyTask(void *pvParameters)
{
	gnuboymain(0, NULL);
	
	//Save state
	const esp_partition_t* part;
	part=esp_partition_find_first(40, 3, NULL);
	if (part==0) {
		printf("Couldn't find state part!\n");
	} else {
		savestate(part);
		printf("State saved.\n");
	}
	kchal_power_down();
	
}

void gnuboy_esp32_videohandler();
void lineTask();

void videoTask(void *pvparameters) {
	gnuboy_esp32_videohandler();
}

int frames; //used in lcd

void monTask() {
	while(1) {
		vTaskDelay(1000/portTICK_PERIOD_MS);
		printf("Fps: %d\n", frames);
		printf("Free mem: %d\n", xPortGetFreeHeapSize());
		frames=0;
	}

//	vTaskDelete(NULL);
}


void quitEmu() {
	emu_running=0;
}
#include "cpuregs.h"

void startEmuHook() {
	const esp_partition_t* part;
	if (kchal_get_keys()&KC_BTN_START) return;
	part=esp_partition_find_first(40, 3, NULL);
	if (part==0) {
		printf("Couldn't find state part!\n");
	} else {
		loadstate(part);
		rombankStateLoaded();
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
//	esp_log_level_set("appfs", ESP_LOG_DEBUG);
	kchal_init();
	nvs_flash_init();
	
	if (appfsExists(BOOTROM_NAME)) {
		appfs_handle_t fd=appfsOpen(BOOTROM_NAME);
		err=appfsMmap(fd, 0, 2304, (const void**)&gbbootromdata, SPI_FLASH_MMAP_DATA, &hbootrom);
		if (err==ESP_OK) {
			printf("Bootrom loaded.\n");
		} else {
			printf("Couldn't map bootrom appfs file!\n");
		}
	} else {
		printf("No bootrom found!\n");
	}
	rombankLoadBootrom();
	printf("Initialized. bootROM@%p\n", gbbootromdata);
	xTaskCreatePinnedToCore(&videoTask, "videoTask", 1024, NULL, 5, NULL, 1);
	xTaskCreatePinnedToCore(&lineTask, "lineTask", 1024, NULL, 6, NULL, 1);
	xTaskCreatePinnedToCore(&gnuboyTask, "gngbTask", 1024*4, NULL, 5, NULL, 0);
	xTaskCreatePinnedToCore(&monTask, "monTask", 1024*2, NULL, 7, NULL, 0);
}


