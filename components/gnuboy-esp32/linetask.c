#include <stdlib.h>
#include <stdio.h>

#include "rom/ets_sys.h"
#include "fb.h"
#include "lcd.h"
#include <string.h>
#include "menu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_task_wdt.h"

static SemaphoreHandle_t lineSem, doneSem;
static int started=0;

void lcd_refreshline_start() {
	if (started) return;
	xSemaphoreGive(lineSem);
	started=1;
}

void lcd_refreshline_end() {
	if (!started) return;
	xSemaphoreTake(doneSem, portMAX_DELAY);
	started=0;
}

void lcd_refreshline();

void lineTask() {
	lineSem=xSemaphoreCreateBinary();
	doneSem=xSemaphoreCreateBinary();
	while(1) {
		xSemaphoreTake(lineSem, portMAX_DELAY);
		lcd_refreshline();
		xSemaphoreGive(doneSem);
	}
}

