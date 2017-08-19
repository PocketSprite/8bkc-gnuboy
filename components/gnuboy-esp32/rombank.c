#include "nvs.h"
#include "hw.h"
#include "esp_spi_flash.h"
#include "esp_partition.h"
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "appfs.h"

#define NO_MAPS 64

typedef struct {
	int bank;
	uint8_t *data;
	spi_flash_mmap_handle_t handle;
	uint32_t use;
} MappedPage;

static int ctr=0;
static MappedPage page[NO_MAPS];
static nvs_handle nvs;



static uint8_t *bootrom=NULL;
//static uint8_t *rombank0=NULL; //ToDo: Fix this

#define CARTROM_NAME "sml.gb"


//Game Boy ROM banks are 16KiB. We map it into the 64K MMU banks of the ESP32.
uint8_t *getRomBank(int bank) {
	int x;
	int espBank=(bank/4);
	int offset=(bank&3)*(16*1024);
	if (bootrom && bank==0) return bootrom;
//	if (bank==0 && rombank0) return rombank0;
	ctr++;
	for (x=0; x<NO_MAPS; x++) {
		if (page[x].bank==espBank && page[x].use) {
			page[x].use=ctr;
			return page[x].data+offset;
		}
	}
	//No bank found. Map new one.
	int oldest=0;
	for (x=0; x<NO_MAPS; x++) {
		if (page[x].use < page[oldest].use) oldest=x;
	}
	if (page[oldest].use) {
		printf("Unloading ESP bank %d\n", page[oldest].bank);
		spi_flash_munmap(page[oldest].handle);
	}

	appfs_handle_t fd=appfsOpen(CARTROM_NAME);
	esp_err_t err=appfsMmap(fd, (espBank*(1<<16)), (1<<16), (const void**)&page[oldest].data, SPI_FLASH_MMAP_DATA, &page[oldest].handle);
	if (err!=ESP_OK) printf("Couldn't map cartrom part!\n");

	page[oldest].use=ctr;
	page[oldest].bank=espBank;
	printf("Loading ESP bank %d into slot %d, mempos %p\n", espBank, oldest, page[oldest].data);
	return page[oldest].data+offset;
}

extern unsigned char *gbbootromdata;

uint8_t bootromLoaded=0; //for save state

void rombankLoadBootrom() {
	if (bootrom) free(bootrom);
	uint8_t *oldBank=getRomBank(0);
	bootrom=malloc(2304);
	if (bootrom==NULL) {
		die("Can't allocate 2304 bytes for boot ROM!\n");
	}
	memcpy(bootrom,gbbootromdata, 2304);
	memcpy(bootrom+256, oldBank+256, 256);
	bootromLoaded=1;
}

void romBankUnloadBootrom() {
	printf("Unloading boot rom.\n");
	free(bootrom);
	bootrom=NULL;
	bootromLoaded=0;
}

void rombankStateLoaded() {
	if (!bootromLoaded && bootrom) {
		romBankUnloadBootrom();
	} else if (bootromLoaded && !bootrom) {
		rombankLoadBootrom();
	}
}
