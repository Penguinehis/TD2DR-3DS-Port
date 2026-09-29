#pragma once
// Background file reads: the SD card blocks for a long time on real hardware, so textures and
// sounds needed in the middle of a match are read on a worker thread instead of freezing
// the game loop.

#include "common.h"

enum { LOADER_PENDING, LOADER_READY, LOADER_FAILED, LOADER_MISSING };

void loader_init(void);
void loader_exit(void);
// Ask for a file. LOADER_READY hands over the contents (free() them); LOADER_PENDING: ask
// again next frame; LOADER_MISSING: no such file; LOADER_FAILED: read error / out of memory.
int loader_fetch(const char *path, void **data, size_t *size);
