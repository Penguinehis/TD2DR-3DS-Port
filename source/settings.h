#pragma once
// Player settings, saved to sdmc:/3ds/sonic3ds/settings.ini.

#include "common.h"

typedef struct {
    char nickname[32];
    char server[64];   // "host" or "host:port"
    int lobby_icon;
    int pet;
    // Graphics (turn off to keep 60 fps with many players on Old 3DS)
    bool gfx_backgrounds;   // background layers
    bool gfx_parallax;      // backgrounds scroll with the camera (off: fixed)
    bool gfx_effects;       // dust, sparkles, quick effects, ring shards
    bool gfx_weather;       // rain, snow and other particle fields
    bool gfx_overlays;      // screen vignette, fog and flash overlays
    bool show_perf;         // frame timings on the bottom screen
    int music_volume;       // 0..10
    int sfx_volume;         // 0..10
} Settings;

extern Settings settings;

void settings_load(void);
void settings_save(void);
// Hash unique to this console (the PC client sends its device udid).
u64 settings_console_id(void);
