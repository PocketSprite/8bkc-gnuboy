#ifndef ROMBANK_H
#define ROMBANK_H

#include <stdint.h>

extern uint8_t bootromLoaded;

uint8_t *getRomBank(int bank);
void romBankUnloadBootrom();
void rombankLoadBootrom();
void rombankStateLoaded();

void rombankLoad(char *rom);
void rombankUnload();


#endif