#pragma once
// Player settings, saved to sdmc:/3ds/sonic3ds/settings.ini.

#include "common.h"

// Remappable in-game actions (menus keep A = confirm, B = back)
enum { BIND_JUMP, BIND_SPECIAL, BIND_ABILITY, BIND_EMOTE1, BIND_EMOTE2, BIND_EMOTE3, BIND_CHAT, BIND_COUNT };
extern const char *BIND_NAMES[BIND_COUNT];
extern const u32 BIND_DEFAULTS[BIND_COUNT];
// Buttons that can be bound, and their names
#define BIND_BUTTONS (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L | KEY_R | KEY_ZL | KEY_ZR)
const char *button_name(u32 keys);

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
    bool cam_lookahead;
    bool show_hud;          // ability cooldowns and the hp meter over the head (global.showHud)     // the camera leads 25% of the screen in the facing direction
    u32 bind[BIND_COUNT];   // KEY_* mask per action
    int music_volume;       // 0..10
    int sfx_volume;         // 0..10
} Settings;

extern Settings settings;

void settings_load(void);
void settings_save(void);
// Hash unique to this console (the PC client sends its device udid).
u64 settings_console_id(void);
