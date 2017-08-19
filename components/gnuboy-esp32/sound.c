
#include "defs.h"
#include "pcm.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s.h"
#include "freertos/task.h"
#include <sdkconfig.h>
#include "8bkc-hal.h"

struct pcm pcm;

//Keep this preferably very short. No idea why, but >64'ish impacts frame rate.
#define SNDBUFLEN 8


static uint8_t buf[SNDBUFLEN];

void pcm_setvolume(int i) {
	kchal_set_volume(i);
}

void pcm_init()
{
	pcm.buf = buf;
	pcm.len = SNDBUFLEN;
	pcm.stereo = 0;
	pcm.pos = 0;

	pcm.hz = 31250;
	kchal_sound_start(pcm.hz, 1024);
}

void pcm_close()
{
	memset(&pcm, 0, sizeof pcm);
}

//Hackish: send 0-samples
void pcm_mute() {
/* ToDo
	for (int x=0; x<128; x++) {
		for (int i=0; i<SNDBUFLEN; i++) sbuf[i]=((128)<<8)+((128)<<24);
		kchal_sound_push(0, (char*)sbuf, SNDBUFLEN*4, portMAX_DELAY);
	}
*/
}

int pcm_submit()
{
	kchal_sound_push(buf, SNDBUFLEN);
	pcm.pos = 0;
	return 1;
}





