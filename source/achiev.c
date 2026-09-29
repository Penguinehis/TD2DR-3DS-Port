#include "achiev.h"

#include <sys/stat.h>

#include "audio.h"
#include "gen/rooms.h"
#include "gen/sounds.h"
#include "gen/sprites.h"
#include "level.h"
#include "levels.h"
#include "player.h"
#include "sprite.h"

#define SAVE_DIR "sdmc:/3ds/sonic3ds"
#define SAVE_PATH SAVE_DIR "/achievements.bin"
#define SAVE_MAGIC 0x32484341  // "ACH2"

AchievSave achiev;
AchievRound achiev_round;

static int room_id = -1;
static int box_timer;      // obj_achivements alarm[2]: popup visible
static float box_x;

// ------------------------------------------------------------------ save

void achiev_save(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);
    FILE *f = fopen(SAVE_PATH ".tmp", "wb");
    if (!f) return;
    u32 magic = SAVE_MAGIC;
    fwrite(&magic, 4, 1, f);
    fwrite(&achiev, sizeof achiev, 1, f);
    fclose(f);
    remove(SAVE_PATH);
    rename(SAVE_PATH ".tmp", SAVE_PATH);
}

void achiev_init(void)
{
    memset(&achiev, 0, sizeof achiev);
    FILE *f = fopen(SAVE_PATH, "rb");
    if (!f) return;
    u32 magic = 0;
    AchievSave tmp;
    if (fread(&magic, 4, 1, f) == 1 && magic == SAVE_MAGIC && fread(&tmp, sizeof tmp, 1, f) == 1) achiev = tmp;
    fclose(f);
}

void achiev_round_start(int room)
{
    room_id = room;
    memset(&achiev_round, 0, sizeof achiev_round);
    achiev_round.rHealPlayer = -1;
}

static bool counts(void) { return room_id != ROOM_FARTZONE; }

void achiev_give(int index, int coins)
{
    if (index < 0 || index >= ACHIEV_COUNT || achiev.achieved[index] || !counts()) return;
    achiev.achieved[index] = true;
    achiev.mercoins += coins;
    if (achiev.changed_count < ACHIEV_COUNT) achiev.changed[achiev.changed_count++] = (u8)index;
    // show()
    audio_play(SND_ACHIVEMENT);
    box_timer = 60 * 4;
    achiev_save();
}

void achiev_clear_new(void)
{
    if (!achiev.changed_count) return;
    achiev.changed_count = 0;
    achiev_save();
}

// ------------------------------------------------------------------ events

void achiev_mercoin_bonus(u8 type, NetReader *r)
{
    AchievRound *R = &achiev_round;
    switch (type) {
    case 0:  // our kill as the EXE
        achiev.mercoins += 2;
        achiev.exeKills++;
        if (achiev.exeKills >= 3) achiev_give(12, 25);
        if (achiev.exeKills >= 10) achiev_give(13, 50);
        if (achiev.exeKills >= 50) achiev_give(14, 250);
        if (achiev.exeKills >= 100) achiev_give(15, 500);
        if (R->rUsedClone) achiev_give(37, 100);
        if (R->rExeInvis) achiev_give(34, 100);
        break;
    case 1:  // we stunned the EXE
        if (++R->stunnedExe >= 5) achiev_give(9, 100);
        achiev.mercoins += 1;
        break;
    case 2: {  // our Tails shot hit
        u8 charge = rd_u8(r);
        if (charge >= 6 && charge <= 10) achiev_give(22, 75);
        if (charge >= 60) achiev_give(23, 100);
        if (charge <= 10 && ++R->stunnedExe >= 5) achiev_give(9, 100);
        audio_play(SND_TAILS_HIT);
        achiev.mercoins++;
        break;
    }
    case 3:
        if (++R->rExeAmyHits >= 5) achiev_give(29, 100);
        break;
    case 4:  // we killed as a demon
        R->rDemonKills++;
        if (R->demonLast) achiev_give(8, 125);
        if (R->rDemonKills >= 2 && level.has_player && level.player.character == 5) achiev_give(31, 75);  // Cream
        achiev_give(7, 25);
        break;
    case 5:
        if (++R->rSallyShield >= 2) achiev_give(33, 100);
        break;
    case 7:
        if (++R->rShockwave >= 2) achiev_give(36, 100);
        break;
    case 8:
        if (++R->rUpper >= 2) achiev_give(25, 100);
        break;
    case 9:
        if (++R->rUpper >= 2) achiev_give(24, 75);
        break;
    case 10:
        if (++R->rChaosDash >= 2) achiev_give(35, 100);
        break;
    case 11:
        R->rAmyStuns++;
        break;
    }
    achiev_save();
}

static void add_map(void)
{
    const char *name = room_id >= 0 && room_id < RM_COUNT ? ROOM_FILES[room_id] : "";
    for (int i = 0; i < achiev.map_count; i++)
        if (!strcmp(achiev.maps[i], name + (strrchr(name, '/') ? strrchr(name, '/') - name + 1 : 0))) return;
    if (achiev.map_count < 32) {
        const char *base = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
        snprintf(achiev.maps[achiev.map_count++], sizeof achiev.maps[0], "%s", base);
    }
}

static void win_totals(void)
{
    u32 total = achiev.survWins + achiev.exeWins;
    if (total >= 10) achiev_give(0, 50);
    if (total >= 50) achiev_give(1, 250);
    if (total >= 100) achiev_give(2, 500);
}

void achiev_exe_end(bool counted, int exe_character, int players)
{
    if (counts() && counted) {
        achiev.exeWins++;
        add_map();
        if (achiev.map_count >= 20) achiev_give(49, 100);
    }
    if (counted) {
        if (exe_character >= 0 && exe_character < 4) achiev.won_as[6 + exe_character] = true;
        if (achiev.won_as[6] && achiev.won_as[7] && achiev.won_as[8] && achiev.won_as[9]) achiev_give(19, 100);
        win_totals();
        if (players > 0) achiev.mercoins += (players - 1) * 2;
    }
    achiev_save();
}

void achiev_survivor_win(bool counted, int character, int exe_character)
{
    AchievRound *R = &achiev_round;
    if (counts() && counted) {
        achiev.survWins++;
        add_map();
        if (achiev.map_count >= 18) achiev_give(49, 100);
    }
    if (counted) {
        if (character >= 1 && character <= 6) achiev.won_as[character - 1] = true;
        if (room_id == ROOM_PRICELESSFREEDOM && R->rBlackRings == 0) achiev_give(47, 25);
        if (R->rShardsCollected >= 6) achiev_give(48, 25);
        if (character == 4 && R->rAmyStuns == 0) achiev_give(28, 75);
        if (!R->wasHurt) achiev_give(11, 50);
        switch (room_id) {  // win a map with its own character
        case ROOM_HIDEANDSEEK2: case ROOM_ANGELISLAND: if (character == 1) achiev_give(38, 25); break;
        case ROOM_YOUCANTRUN: if (character == 2) achiev_give(39, 25); break;
        case ROOM_DOTDOTDOT: if (character == 3) achiev_give(40, 25); break;
        case ROOM_NOTPERFECT: if (character == 4) achiev_give(41, 25); break;
        case ROOM_KINDANDFAIR: if (character == 5) achiev_give(42, 25); break;
        case ROOM_ACT9: if (character == 6) achiev_give(43, 25); break;
        case ROOM_TORTURECAVE: if (character == 1 && exe_character == 1) achiev_give(44, 25); break;
        case ROOM_LIMPCITY: if (character == 6 && exe_character == 3) achiev_give(46, 25); break;
        case ROOM_NASTYPARADISE: if (character == 5 && exe_character == 2) achiev_give(45, 25); break;
        }
        bool all = true;
        for (int i = 0; i < 6; i++) all &= achiev.won_as[i];
        if (all) achiev_give(18, 100);
        if (R->rPlayersEscaped >= 5) achiev_give(20, 50);
        win_totals();
    }
    achiev_save();
}

void achiev_escaped(int hp)
{
    if (hp <= 20 && player_time_min == 0 && player_time_sec <= 5) achiev_give(10, 100);
}

void achiev_revival_ringsub(void)
{
    AchievRound *R = &achiev_round;
    R->revivals++;
    if (R->revivalLast) achiev_give(5, 100);
    R->revivalLast = true;
    if (R->revivals >= 1) achiev_give(3, 25);
    if (R->revivals >= 2) achiev_give(4, 50);
    if (R->revivals >= 3) achiev_give(6, 200);
}

void achiev_heal_restore(void)
{
    if (++achiev_round.healthRestore >= 3) achiev_give(16, 100);
}

void achiev_healed_player(int master_id, int character)
{
    AchievRound *R = &achiev_round;
    if (R->rHealPlayer == master_id) return;
    R->rHeals++;
    if (R->rHeals >= 2) achiev_give(17, 100);
    if (R->rHeals >= 3 && character == 5) achiev_give(30, 100);
    R->rHealPlayer = master_id;
}

void achiev_rings(int rings)
{
    if (rings >= 25) achiev_give(21, 25);
}

void achiev_tracker(bool exe_target)
{
    AchievRound *R = &achiev_round;
    if (exe_target) R->rEggTrack++;
    else R->rExeEggTrack++;
    if (R->rEggTrack >= 4) achiev_give(26, 100);
    if (R->rExeEggTrack >= 3) achiev_give(27, 75);
}

void achiev_sally_shield(void)
{
    if (++achiev_round.rSallyShield >= 2) achiev_give(32, 75);
}

// ------------------------------------------------------------------ shop

int shop_icon_price(int i) { return i == 0 ? 0 : i >= 18 ? 75 : i >= 9 ? 50 : 25; }
int shop_taunt_price(int i) { static const int p[8] = { 150, 175, 175, 150, 175, 150, 175, 150 }; return p[i & 7]; }
int shop_pet_price(int i) { static const int p[7] = { 250, 275, 250, 300, 275, 250, 250 }; return i < 0 ? 0 : p[i % 7]; }

void achiev_taunts_for(int exe_character, bool *taunt1, bool *taunt2)
{
    int b = (exe_character & 3) * 2;
    if (exe_character == 0) {  // obj_exe: taunt1 = "texe1", taunt2 = "texe0"
        *taunt1 = achiev.taunts[b + 1];
        *taunt2 = achiev.taunts[b];
    } else {
        *taunt1 = achiev.taunts[b];
        *taunt2 = achiev.taunts[b + 1];
    }
}

// ------------------------------------------------------------------ popup

void achiev_draw_popup(void)
{
    // obj_achivements Draw_64: slides in to x 194, stays 4 s, slides back
    if (box_timer > 0) {
        box_timer--;
        if (box_x < 194) box_x += 6;
    } else if (box_x > 0) {
        box_x -= 6;
    }
    if (box_x > 0) sprite_draw(SPR_ACHIVEMENTBOX, 0, box_x, 4, 1, 1, 0, 0xFFFFFFFF, 1);
}
