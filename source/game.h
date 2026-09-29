#pragma once
// Online match: port of the core of net_state_game, obj_level, obj_player_puppet, obj_ring,
// obj_bigring and the player-data sender in obj_netclient (Step_2). Level gimmicks come in M6.

#include "common.h"

void game_init(void);            // registers the network handler
void game_begin(void);           // SERVER_LOBBY_GAME_START: load the map and spawn
void game_end(void);             // leaving the match (back to lobby / disconnect)
bool game_active(void);
// Music the match wants now (level track, chase track after the exit ring spawns, results).
int game_music(void);

// For entities: the first puppet playing a character (its id and whether it is a demon),
// the EXE puppet's animation state (-1 if none) and global.ringFrame.
bool game_puppet_of(int character, u16 *id, bool *demon);
int game_exe_state(void);
float game_ring_frame(void);
// obj_exetior_indicator / obj_exeller_indicator showTimer: the EXE's tracking arrow shows for
// this many frames (after a black ring or a clone teleport).
void game_exe_indicator_show(int frames);
// The first puppet playing a character: its hp and revivalTimes.
bool game_puppet_status(int character, int *hp, int *revival);
// The nearest visible obj_player_puppet closer than max_dist (false if none / offline).
bool game_nearest_puppet(float x, float y, float max_dist, float *px, float *py);

void game_update(u32 held, u32 down);
void game_draw_top(void);
void game_draw_bottom(void);
