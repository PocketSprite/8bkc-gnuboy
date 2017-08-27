#ifndef SAVE_H
#define SAVE_H

#include <stdio.h>
#include "appfs.h"

void savestate(appfs_handle_t f);
void loadstate(appfs_handle_t f);

#endif

