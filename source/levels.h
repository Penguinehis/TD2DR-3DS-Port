#pragma once
// global.levels from scr_globals: the server's map index -> room.

#include "common.h"

#define LEVEL_COUNT 21

typedef struct {
    const char *name;
    int room;
    int music, chase;  // SND_* (global.levels music / chaseMusic)
} LevelDef;

extern const LevelDef LEVELS[LEVEL_COUNT];
