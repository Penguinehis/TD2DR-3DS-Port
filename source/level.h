#pragma once
// A loaded room with the local player and obj_camera: update, draw and the free-camera
// viewer used for rooms without a spawn point.

#include "common.h"
#include "player.h"
#include "room.h"

typedef struct Level {
    Room room;
    int room_id;
    u32 time;              // frames since load (drives room-level animation)
    float cam_x, cam_y;
    float cam_dist;        // obj_camera look offset
    int cam_look_timer;
    float cam_lead;        // look-ahead towards the facing side (settings.cam_lookahead)
    Player player;
    bool has_player;
    bool show_hidden;      // draw invisible objects as boxes
    bool pet_hidden;       // our pet is not drawn (escaped)
    float view_w, view_h;
    bool gui_clamp;        // menu rooms shown 1:1: objects are kept inside the screen  // visible room area for culling (400x240; menus use the whole 480x270)
} Level;

extern Level level;

const char *room_name(int id);

// Load a room. With spawn_character >= 0 the player spawns at the first spawn point.
void level_load(int room_id, int spawn_character);
// How far to move a sprite drawn at screen (x, y) so it stays inside the view (level.gui_clamp)
void level_clamp_offset(int spr, float x, float y, float xs, float ys, float *dx, float *dy);
void level_unload(void);

// Keys from the HID state: D-Pad and Circle Pad move, A jump, B special, Y C-button,
// L / R / ZL-ZR emotions 1 / 2 / 3.
Keys level_keys(u32 held, u32 down);

// Place the local player (as obj_spawnpoint does) and centre the camera on it.
void level_spawn(int character, int exe_character, float x, float y);

void level_update(const Keys *k, u32 held);
// Called by level_draw at the player's depth, before the local player (puppets, rings).
extern void (*level_draw_objects)(void);
void level_draw(void);
int level_count_instances(void);
