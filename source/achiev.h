#pragma once
// Achievements and mercoins (obj_achivements). Saved to sdmc:/3ds/sonic3ds/achievements.bin.
// The 50 achievements are the rows of spr_achivements (their text is in the picture).

#include "common.h"
#include "net.h"

#define ACHIEV_COUNT 50

typedef struct {
    u64 mercoins;
    u32 survWins, exeWins, exeKills, survStuns;
    bool won_as[10];            // tails knux eggman amy cream sally exe chaos exetior exeller
    bool achieved[ACHIEV_COUNT];
    u8 changed[ACHIEV_COUNT];   // unlocked since the list was last viewed ("new" marks)
    int changed_count;
    char maps[32][24];          // levels won on
    int map_count;
    // obj_unlockables trades bought with mercoins
    bool icons[25];             // "icon<i>"
    bool taunts[8];             // "t<exe|chaos|exetior|exeller><0|1>" = index 2 * exe + n
    bool pets[7];               // pet 0..6
} AchievSave;

// Shop prices (obj_menu_icon / obj_menu_taunt / obj_menu_pet)
int shop_icon_price(int i);
int shop_taunt_price(int i);
int shop_pet_price(int i);
// EXE taunt unlocks for an EXE character (taunt1 / taunt2 in each EXE's Create_0)
void achiev_taunts_for(int exe_character, bool *taunt1, bool *taunt2);

// Per-match counters (obj_achivements round data, reset at room start)
typedef struct {
    int revivals, stunnedExe, healthRestore, rHeals, rHealPlayer, rPlayersEscaped, rDemonKills;
    int rEggTrack, rExeEggTrack, rAmyStuns, rExeAmyHits, rSallyShield, rBlackRings, rShardsCollected;
    int rShockwave, rChaosDash, rUpper;
    bool revivalLast, demonLast, wasHurt, rUsedClone, rExeInvis;
} AchievRound;

extern AchievSave achiev;
extern AchievRound achiev_round;

void achiev_init(void);
void achiev_save(void);
void achiev_round_start(int room);

// Unlock one (mercoins as the GML gives them). Ignored in the practice map and when done.
void achiev_give(int index, int coins);
void achiev_clear_new(void);        // the achievements list was opened

// Match events
void achiev_mercoin_bonus(u8 type, NetReader *r);   // CLIENT_MERCOIN_BONUS addressed to us
void achiev_exe_end(bool counted, int exe_character, int players);          // EXE wins / time over, we are the EXE
void achiev_survivor_win(bool counted, int character, int exe_character);   // we escaped
void achiev_escaped(int hp);                   // SERVER_PLAYER_ESCAPED for us
void achiev_revival_ringsub(void);             // SERVER_REVIVAL_RINGSUB
void achiev_heal_restore(void);                // our 10-ring heal
void achiev_healed_player(int master_id, int character);   // we healed someone
void achiev_rings(int rings);                  // the "25 rings" check (scr_move_basic)
void achiev_tracker(bool exe_target);          // our Eggman tracker caught someone
void achiev_sally_shield(void);                // a shield absorbed a hit with <= 20 hp

// Popup (spr_achivementbox sliding in) on the top screen
void achiev_draw_popup(void);
