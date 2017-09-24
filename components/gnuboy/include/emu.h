#ifndef EMU_H
#define EMU_H

#define EMU_RUN_CONT 0
#define EMU_RUN_NEWROM 1
#define EMU_RUN_RESET 2
#define EMU_RUN_POWERDOWN 3
#define EMU_RUN_EXIT 4


int emu_run();
void emu_reset();

int gnuboymain(char *rom, int loadState);


#endif


