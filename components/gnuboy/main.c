#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <stdarg.h>

#include "sys.h"
#include "emu.h"
#include "loader.h"

#define VERSION "1.0.4"

void die(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	printf("Dying.\n");
	asm("break.n 1");
}

void startEmuHook();

int gnuboymain(int argc, char *argv[])
{
	int i;
	char *opt, *arg, *cmd, *s, *rom = "na";


	/* If we have special perms, drop them ASAP! */
	vid_preinit();

	/* FIXME - make interface modules responsible for atexit() */
	vid_init();
	pcm_init();

	sys_sanitize(rom);
	
	loader_init(rom);
	
	emu_reset();
	startEmuHook();
	emu_run();

	vid_close();
	pcm_close();

	return 0;
}











