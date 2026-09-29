#pragma once
// Front end: title menu, and the online screens (connecting, waiting room, lobby, map vote,
// character select, errors). Gameplay screens live in main.c / game.c.

#include "common.h"

typedef enum {
    APP_TITLE,
    APP_ONLINE,   // net screens; switches to gameplay when the match starts
    APP_OFFLINE,  // offline practice / level viewer
    APP_QUIT,
} AppMode;

extern AppMode app_mode;

void menu_enter_title(void);
void menu_connect(const char *address);
// Offline practice in a room. choice: 1..6 survivors, 7..10 the EXEs, -1 level viewer.
void menu_start_practice(int room, int choice);

// Title and online screens. Returns true if it drew the top screen itself.
void menu_update(u32 held, u32 down);
void menu_draw_top(void);
void menu_draw_bottom(void);
// Music for the title / online screens (-1 none, -2 keep the current track).
int menu_music(void);
