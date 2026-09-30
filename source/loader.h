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

// A .t3x texture decompressed into linear memory, in the GPU's layout (no mipmaps)
typedef struct {
    void *mem;      // linear memory (linearFree it)
    u32 size;
    u16 width, height;
    u8 format;      // GPU_TEXCOLOR
} LoaderTex;
// Linear allocations happen on the game thread only (libctru's linear heap has no lock):
// the loader asks for the buffer through this callback, NULL when there is no room.
typedef void *(*LoaderAlloc)(u32 size);
// In the background: the file is read and decompressed on the worker, straight into the
// texture memory, so the game thread only allocates it. Same results as loader_fetch.
int loader_fetch_tex(const char *path, LoaderTex *out, LoaderAlloc alloc);
// loader_fetch_tex at once (level start).
int loader_load_tex_now(const char *path, LoaderTex *out, LoaderAlloc alloc);
// Forget the textures asked for so far (a new level): their memory is freed.
void loader_drop_textures(void);
