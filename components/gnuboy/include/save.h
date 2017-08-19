#ifndef SAVE_H
#define SAVE_H

#include <stdio.h>
#include "esp_partition.h"

void savestate(esp_partition_t *f);
void loadstate(esp_partition_t *f);

#endif

