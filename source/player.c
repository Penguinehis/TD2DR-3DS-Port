// Local player movement and collision shared by every playable character, ported line by
// line from the GML: scripts/scr_move_basic, scripts/scr_collision_basic,
// scripts/scr_collision_objects, scripts/scr_player_hurt, scripts/scr_player_slow and the
// Step -> End Step -> image advance -> Animation End -> Pre-Draw order of the player objects.
// Character branches are hooks in source/chars/*.c (CharDef). Sounds are TODO(M7); level
// gimmicks referenced by the collision scripts (ice blocks, KAF monitors) are TODO(M6).
#include "player.h"
#include "achiev.h"

#include <math.h>

#include "audio.h"
#include "chars/chars.h"
#include "net.h"
#include "gen/anims.h"
#include "gen/rooms.h"
#include "gen/sprites.h"
#include "sprite.h"

#define NEAR_MAX 128

void (*player_on_death)(Player *p);
bool player_short_hurt;
bool player_controls = true;
bool player_controls_lock, player_swap_dirs;
int player_room_id = -1;
int player_time_min = 9, player_time_sec = 0;

float sgnf(float v) { return (v > 0) - (v < 0); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

#define HOOK(p, fn, ...) do { if ((p)->def && (p)->def->fn) (p)->def->fn(__VA_ARGS__); } while (0)
#define HOOK_B(p, fn, ...) ((p)->def && (p)->def->fn && (p)->def->fn(__VA_ARGS__))

static const CharDef *def_for(int character, int exe_character)
{
    switch (character) {
    case CHARACTER_TAILS: return &CHAR_TAILS_DEF;
    case CHARACTER_KNUX: return &CHAR_KNUX_DEF;
    case CHARACTER_EGGMAN: return &CHAR_EGGMAN_DEF;
    case CHARACTER_AMY: return &CHAR_AMY_DEF;
    case CHARACTER_CREAM: return &CHAR_CREAM_DEF;
    case CHARACTER_SALLY: return &CHAR_SALLY_DEF;
    case CHARACTER_EXE:
        switch (exe_character) {
        case EXE_CHAOS: return &CHAR_CHAOS_DEF;
        case EXE_EXETIOR: return &CHAR_EXETIOR_DEF;
        case EXE_EXELLER: return &CHAR_EXELLER_DEF;
        default: return &CHAR_EXE_DEF;
        }
    }
    return &CHAR_TAILS_DEF;
}

int player_anim_sprite(const Player *p)
{
    if (p->character == CHARACTER_EXE) {
        int e = p->exe_character;
        if (e < 0 || e > 3 || p->state < 0 || p->state >= ANIMS_EXE_COUNT[e]) return -1;
        return ANIMS_EXE[e][p->state];
    }
    int c = p->character;
    if (c < 1 || c > 6 || p->state < 0) return -1;
    if (p->revivalTimes >= 2) return p->state < ANIMS_DEMON_COUNT[c] ? ANIMS_DEMON[c][p->state] : -1;
    return p->state < ANIMS_SURVIVOR_COUNT[c] ? ANIMS_SURVIVOR[c][p->state] : -1;
}

int player_frames(const Player *p)
{
    const SpriteInfo *s = sprite_info(p->sprite);
    return s && s->frame_count > 0 ? s->frame_count : 1;
}

bool player_place_floor(const Player *p, float dx, float dy)
{
    const SpriteInfo *s = sprite_info(p->sprite);
    if (!s) return world_point(p->x + dx, p->y + dy, WC_FLOOR) != NULL;
    float x = p->x + dx, y = p->y + dy;
    float l = x + (s->bbox_left - s->xorigin) * p->image_xscale;
    float r = x + (s->bbox_right + 1 - s->xorigin) * p->image_xscale;
    return world_rect(fminf(l, r), y + s->bbox_top - s->yorigin, fmaxf(l, r) - 1, y + s->bbox_bottom - s->yorigin,
                      WC_FLOOR) != NULL;
}

// ------------------------------------------------------------------ create

void player_init(Player *p, int character, int exe_character, float x, float y)
{
    memset(p, 0, sizeof *p);
    p->character = character;
    p->exe_character = character == CHARACTER_EXE ? exe_character : 0;
    p->def = def_for(character, exe_character);
    p->x = x;
    p->y = y;
    // Shared Create_0 defaults (identical in every player object)
    p->flyTimer = -420;
    p->hp = 100;
    p->deadTimer = 0;  // Create_0 sets 31 then 0
    p->edgeDir = 1;
    p->taunt1 = p->taunt2 = true;
    p->canMove = p->canLookDown = p->canLookUp = p->canSpinDash = p->canSpin = true;
    p->maxYSpeed = 12;
    p->yAccel = 0.21875f;
    p->jumpForce = 7;
    p->acc = p->def->base_acc;
    p->maxHSpeed = p->def->base_maxspeed;
    p->image_xscale = 1;
    p->image_speed = 1;
    HOOK(p, init, p);
    if (character == CHARACTER_EXE) achiev_taunts_for(p->exe_character, &p->taunt1, &p->taunt2);
    int spr = player_anim_sprite(p);
    p->sprite = spr >= 0 ? spr : SPR_TAILS_IDLE;
}

// ------------------------------------------------------------------ hurt

static int hurt_sound = SND_HURT;

void player_hurt(Player *p, int damage, float xpw, float ypw)
{
    player_hurt_ex(p, damage, xpw, ypw, false);
}

void player_hurt_snd(Player *p, int damage, float xpw, float ypw, int snd)
{
    hurt_sound = snd;
    player_hurt_ex(p, damage, xpw, ypw, false);
    hurt_sound = SND_HURT;
}

void player_hurt_ex(Player *p, int damage, float xpw, float ypw, bool ignore)
{
    if (p->isDead) return;
    if (p->hurttime > 0) {
        if (ignore) {
            p->isGrounded = p->isSpinning = p->isLookingDown = p->isLookingUp = false;
            p->xspd = xpw;
            p->yspd = ypw;
        }
        return;
    }
    if (p->character != CHARACTER_EXE) {
        if (damage > 0 && HOOK_B(p, hurt_blocked, p, damage)) {  // Sally's shield
            if (p->revivalTimes < 2 && p->hp <= 20) achiev_sally_shield();
            return;
        }
        if (p->revivalTimes < 2) {
            achiev_round.wasHurt = true;
            p->hp -= damage;
            if (p->hp <= 0) {
                // scr_player_instakill
                p->hp = 0;
                p->deadTimer = 31;
                p->rings = 0;
                p->xspd = p->image_xscale * 2;
                p->isGrounded = p->isSpinning = p->isLookingDown = p->isLookingUp = false;
                p->isDead = true;
                achiev_round.rShardsCollected = 0;
                player_sound(p, SND_DEAD);
                if (player_on_death) player_on_death(p);
                return;
            }
        }
    } else if (HOOK_B(p, hurt_blocked, p, damage)) {
        return;  // invisible, won or lost
    }
    if (p->rings > 0 && damage > 0) {
        p->rings = 0;
        player_sound(p, SND_RINGLOSE);  // TODO(M5b): spr_ringlose effect
    } else if (damage > 0) {
        player_sound(p, hurt_sound);
    }
    if (damage > 0) {
        p->isHurt = true;
        p->isJumping = false;
    }
    p->isGrounded = p->isSpinning = p->isLookingDown = p->isLookingUp = false;
    p->xspd = xpw;
    p->yspd = ypw;
    p->hurttime = damage != 0 ? (player_short_hurt ? 60 : 3 * 60) : 0;
}

void player_demonize(Player *p)
{
    HOOK(p, on_demonize, p);
    p->hp = 100;
    p->isDead = false;
    p->revivalTimes = 2;
    p->hurttime = 60 * 2;
}

void player_sound_local(int snd) { audio_play(snd); }

bool player_voice_playing(void)
{
    for (int i = 46; i < NET_SNDS_COUNT; i++)
        if (NET_SNDS[i] >= 0 && audio_is_playing(NET_SNDS[i])) return true;
    return false;
}

void player_sound(const Player *p, int snd)
{
    audio_play(snd);
    if (p->character == CHARACTER_EXE && p->invisTimer > 0 && snd != SND_SPRING) return;
    int idx = -1;
    for (int i = 0; i < NET_SNDS_COUNT; i++)
        if (NET_SNDS[i] == snd) { idx = i; break; }
    if (idx < 0) return;
    NetPacket pk;
    pkt_begin(&pk, CLIENT_SOUND_EMIT);
    pk.buf[0] = 1;
    pkt_u16(&pk, net.id);
    pkt_u8(&pk, (u8)idx);
    pkt_u8(&pk, 0);
    net_send(&pk, true);
}

bool player_hurt_blocked(Player *p)
{
    return HOOK_B(p, hurt_blocked, p, 20);
}

void player_slow(Player *p, float seconds)
{
    // Every EXE uses obj_exe's EXE_ACC / EXE_MAXSPEED here (scr_player_slow)
    float acc = p->character == CHARACTER_EXE ? 0.056875f : p->def->base_acc;
    float max = p->character == CHARACTER_EXE ? 12 : p->def->base_maxspeed;
    p->isSlow = true;
    p->acc = acc * (100 - 40) / 100;
    p->maxHSpeed = max * (100 - 40) / 100;
    p->alarm4 = (int)(60 * seconds);
}

u8 player_net_flags(const Player *p)
{
    u8 flags = p->effectTime > 0 ? NETF_EFFECT : 0;
    if (p->hurttime > 0) flags |= NETF_HURT;
    if (p->character != CHARACTER_EXE && p->redRingTimer > 0) flags |= NETF_REDRING;
    if (p->def && p->def->net_flags) flags |= p->def->net_flags(p);
    return flags;
}

// ------------------------------------------------------------------ collision (scr_collision_basic)

static void collision_check_top(Player *p, WorldInst **near, int nn)
{
    Sensor *s1 = &p->sTL, *s2 = &p->sTR;
    s1->x = ceilf(p->x) - 7; s1->y = ceilf(p->y) - 18;
    s2->x = ceilf(p->x) + 7; s2->y = ceilf(p->y) - 18;
    int lim = (int)p->maxYSpeed + 1;

    int dist1 = 0;
    while (!list_point(near, nn, s1->x, s1->y) && dist1 < lim) { s1->y--; dist1++; }
    int dist2 = 0;
    while (!list_point(near, nn, s2->x, s2->y) && dist2 < lim) { s2->y--; dist2++; }

    Sensor *hit = NULL;
    int dist = 0;
    if (dist1 != lim && dist2 != lim) {
        if (s1->y >= s2->y) { hit = s1; dist = dist1; } else { hit = s2; dist = dist2; }
    } else if (dist1 != lim) { hit = s1; dist = dist1; }
    else if (dist2 != lim) { hit = s2; dist = dist2; }

    if (hit && -dist >= p->yspd && p->yspd < 0 && !world_point(hit->x, hit->y, WC_JUMPTHROUGH)) {
        p->y = ceilf(hit->y) + 18;
        p->yspd = 0;
    }
}

static void collision_check(Player *p, WorldInst **near, int nn)
{
    Sensor *s1 = &p->sL, *s2 = &p->sR;
    s1->coll = s2->coll = false;
    int lim = (int)p->maxHSpeed + 1;
    int yy_end = p->isGrounded ? 0 : (int)(15 - fabsf(p->xspd));

    for (int yy = -16; yy < yy_end; yy++) {
        s1->x = ceilf(p->x) - 8; s1->y = ceilf(p->y + yy);
        s2->x = ceilf(p->x) + 8; s2->y = ceilf(p->y + yy);

        int dist1 = 0;
        WorldInst *inst1;
        while (!(inst1 = list_point(near, nn, s1->x, s1->y)) && dist1 < lim) { s1->x--; dist1++; }
        int dist2 = 0;
        WorldInst *inst2;
        while (!(inst2 = list_point(near, nn, s2->x, s2->y)) && dist2 < lim) { s2->x++; dist2++; }

        if (-dist1 >= fminf(p->xspd, -1) && !world_point(s1->x, s1->y, WC_JUMPTHROUGH)) {
            s1->coll = true;
            if (p->xspd < 0) {
                p->x = ceilf(s1->x) + 8;
                p->xspd = 0;
                p->gspd = 0;
                p->isBoosting = false;
                if (inst1) HOOK(p, on_wall, p, inst1, -1);
                break;
            }
        }
        if (dist2 <= fmaxf(p->xspd, 1) && !world_point(s2->x, s2->y, WC_JUMPTHROUGH)) {
            s2->coll = true;
            if (p->xspd > 0) {
                p->x = ceilf(s2->x) - 8;
                p->xspd = 0;
                p->gspd = 0;
                p->isBoosting = false;
                if (inst2) HOOK(p, on_wall, p, inst2, 1);
                break;
            }
        }
    }
}

static void collision_check_angle(Player *p, WorldInst **near, int nn)
{
    Sensor *s1 = &p->sAL, *s2 = &p->sAR;
    s1->x = p->x - 7; s2->x = p->x + 7;
    s1->y = p->y - 16; s2->y = p->y - 16;
    const int max_dist = 66;
    int d1 = 0, d2 = 0;
    while (!list_point(near, nn, s1->x, s1->y) && d1 < max_dist) { s1->y++; d1++; }
    while (!list_point(near, nn, s2->x, s2->y) && d2 < max_dist) { s2->y++; d2++; }
    if (d1 != max_dist && d2 != max_dist && !HOOK_B(p, flat_angle, p))
        // degtorad(point_direction(-8, d1, 8, d2)); y grows downward
        p->angle = atan2f(-(float)(d2 - d1), 16.0f);
    else
        p->angle = 0;
}

// Floor landing for one sensor when airborne.
static void try_land(Player *p, Sensor *s, int dist_for_platform)
{
    float winner = s->y - 18;
    bool on_platform = world_point(s->x, s->y, WC_JUMPTHROUGH) != NULL;
    if (on_platform ? (p->yspd > 0 && dist_for_platform >= 15) : (p->yspd > 0)) {
        p->y = winner;
        p->isGrounded = true;
        p->yspd = 0;
        p->gspd = p->xspd;
    }
}

static void collision_check_bottom(Player *p, WorldInst **near, int nn)
{
    Sensor *s1 = &p->sBL, *s2 = &p->sBR;
    s1->x = ceilf(p->x) - 7;
    s2->x = ceilf(p->x) + 7;
    s1->coll = s2->coll = false;

    if (!p->isGrounded) {
        p->angle = 0;
        s1->y = ceilf(p->y) - 16;
        s2->y = ceilf(p->y) - 16;
        int dist1 = 0;
        while (!list_point(near, nn, s1->x, s1->y) && dist1 < 35) { s1->y++; dist1++; }
        int dist2 = 0;
        while (!list_point(near, nn, s2->x, s2->y) && dist2 < 35) { s2->y++; dist2++; }

        // Note: the GML checks dist2 in the dist1-only branch too; kept as is.
        if (dist1 != 35 && dist2 != 35) {
            if (s1->y < s2->y) try_land(p, s2, dist2);
            else try_land(p, s1, dist1);
        } else if (dist1 != 35) {
            try_land(p, s1, dist2);
        } else if (dist2 != 35) {
            try_land(p, s2, dist2);
        }
    }

    if (p->isGrounded) {
        HOOK(p, on_grounded, p);
        s1->y = ceilf(p->y) - 16;
        s2->y = ceilf(p->y) - 16;
        int max_dist = 48;
        if (world_point(p->sL.x, p->sL.y, WC_ANGLE_ALLOW) || world_point(p->sR.x, p->sR.y, WC_ANGLE_ALLOW))
            max_dist = 38;
        int d1 = 0, d2 = 0;
        while (!list_point(near, nn, s1->x, s1->y) && d1 < max_dist) { s1->y++; d1++; }
        while (!list_point(near, nn, s2->x, s2->y) && d2 < max_dist) { s2->y++; d2++; }

        if (d1 != max_dist || d2 != max_dist) {
            float winner;
            if (s1->y == s2->y) s1->coll = s2->coll = true;
            if (s1->y > s2->y) { s2->coll = true; winner = s2->y - 18; }
            else { s1->coll = true; winner = s1->y - 18; }
            p->y = winner;
            collision_check_angle(p, near, nn);
            HOOK(p, on_land_angle, p);
        } else {
            p->angle = 0;
            p->isGrounded = false;
        }
    }
}

static void spring_launch(Player *p, WorldInst *spring, float xspd, float yspd)
{
    // scr_collision_objects: in Hide and Seek 2 the EXE hears survivors using springs
    if ((spring->cats & WC_SPRING) && player_room_id == ROOM_HIDEANDSEEK2 && p->revivalTimes < 2) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_SPRING_USE);
        pk.buf[0] = 1;
        pkt_u16(&pk, net.id);
        pkt_u16(&pk, (u16)(int)p->x);
        pkt_u16(&pk, (u16)(int)p->y);
        net_send(&pk, true);
    }
    if (p->isHurt) p->isHurt = false;
    HOOK(p, on_spring, p);
    p->xspd = xspd;
    p->yspd = yspd;
    p->isBoosting = p->isSpinning = p->isGrounded = p->isJumping = false;
    spring->frame = 1;
    spring->spring_anim = 30;
    spring->spring_cooldown = 60 * 4;
    player_sound(p, SND_SPRING);
}

static void collision_objects_after(Player *p, const Keys *k)
{
    if (p->isDead) return;

    // Springs
    WorldInst *spring = world_point(p->sBL.x, p->sBL.y, WC_SPRING);
    if (!spring) spring = world_point(p->sBR.x, p->sBR.y, WC_SPRING);
    if (spring && spring->spring_cooldown <= 0 && p->yspd > 0 && p->shockedTimer <= 0)
        spring_launch(p, spring, spring->spring_x, spring->spring_y);

    WorldInst *hd = world_point(p->sBL.x, p->sBL.y, WC_HD_SPRING);
    if (!hd) hd = world_point(p->sBR.x, p->sBR.y, WC_HD_SPRING);
    if (hd && hd->spring_cooldown <= 0 && p->yspd > 0 && p->shockedTimer <= 0)
        spring_launch(p, hd, 0, (k->a || k->up) ? -12 : -10);

    // Damage and spikes
    if (world_rect(p->x - 12, p->y + p->yspd, p->x + 12, p->y + 20, WC_DAMAGE)) {
        p->angle = 0;
        player_hurt_snd(p, 20, -p->image_xscale * 4, -6, SND_SPIKE);
    }
    if (world_rect(p->x - 6, p->y + 19, p->x + 6, p->y + 20, WC_SPIKE)) {
        p->angle = 0;
        player_hurt_snd(p, 20, -p->image_xscale * 4, -6, SND_SPIKE);
    }

    if (world_point(p->sL.x, p->sL.y, WC_ANGLE_NULL) || world_point(p->sR.x, p->sR.y, WC_ANGLE_NULL))
        p->angle = 0;

    // Hide: place_meeting(x, y, obj_hiding_parent) uses the player's sprite bbox.
    const SpriteInfo *s = sprite_info(p->sprite);
    bool touching = false;
    if (s) {
        float l = p->x + (s->bbox_left - s->xorigin) * p->image_xscale;
        float r = p->x + (s->bbox_right + 1 - s->xorigin) * p->image_xscale;
        touching = world_rect(fminf(l, r), p->y + s->bbox_top - s->yorigin, fmaxf(l, r) - 1,
                              p->y + s->bbox_bottom - s->yorigin, WC_HIDING) != NULL;
    }
    p->isHiding = touching && p->isLookingDown && !p->isLookingUp;
    if (HOOK_B(p, hide_blocked, p)) p->isHiding = false;
}

static void collision_basic(Player *p, const Keys *k, int room_w, int room_h)
{
    (void)room_w;
    p->xspd = clampf(p->xspd, -p->maxHSpeed, p->maxHSpeed);
    p->yspd = clampf(p->yspd, -p->maxYSpeed, p->maxYSpeed);

    if (p->y - 30 >= room_h) {
        WorldInst *near = world_nearest(p->x, p->y, WC_ABYSS_TARGET);
        if (!near) near = world_nearest(p->x, p->y, WC_DEATH_TP);
        if (near) {
            p->x = near->x;
            p->y = near->y;
            p->xspd = p->gspd = p->yspd = 0;
            HOOK(p, on_abyss, p);
            if (p->character == CHARACTER_EXE || p->revivalTimes >= 2) p->shockedTimer += 30;
            player_hurt(p, 20, 0, 0);
        }
    }

    if (!p->isGrounded) p->yspd += p->yAccel;
    // scr_collision_objects_before only handles obj_nap_iceblock (TODO(M6)).

    // collision_circle_list(x, y, 80, obj_floor_parent): approximated by AABB overlap.
    WorldInst *near[NEAR_MAX];
    int nn = world_query_rect(p->x - 80, p->y - 80, p->x + 80, p->y + 80, WC_FLOOR, near, NEAR_MAX);

    collision_check(p, near, nn);
    p->x += p->xspd;
    collision_check_top(p, near, nn);
    p->y += p->yspd;
    collision_check_bottom(p, near, nn);
    collision_objects_after(p, k);
}

// ------------------------------------------------------------------ movement (scr_move_basic)

void player_move_basic(Player *p, const Keys *k)
{
    p->justJumped = false;
    p->isLookingDown = false;
    p->isLookingUp = false;

    if (p->isDead) {
        p->redRingTimer = 0;
        p->emotion = p->isZipline = p->isAttacking = p->isBoosting = p->isHiding = false;
        HOOK(p, on_dead, p);
        p->gspd -= fminf(fabsf(p->gspd), p->acc) * sgnf(p->gspd);
        p->xspd -= fminf(fabsf(p->xspd), p->acc) * sgnf(p->xspd);
        p->xspd -= 0.225f * sinf(p->angle);
        return;
    }

    p->bounceTimer--;
    if (p->bounceTimer > 0 && p->isGrounded) p->bounceTimer = -1;
    if (p->bounceTimer == 0) {
        if (p->character != CHARACTER_EXE) { p->isJumping = false; p->isSpinning = false; }
        if (p->isGrounded) p->bounceTimer = -1;
    }

    if (p->isHurt) {
        HOOK(p, on_hurt, p);
        p->gspd = 0;
        if (p->xspd > -1 && p->xspd < 1 && p->isGrounded) p->isHurt = false;
        if (p->xspd > 0) p->xspd -= 0.06f; else p->xspd += 0.06f;
        p->yspd += 0.08f;
        p->isBoosting = false;
        p->emotion = false;
        return;
    }

    if (p->isBoosting) {
        if (p->effectTime <= 0) p->effectTime = 16;
        HOOK(p, on_boost, p);
        if (p->isGrounded) {
            p->gspd = 12 * p->image_xscale;
            p->gspd -= 0.125f * sinf(p->angle);
            p->xspd = p->gspd * cosf(p->angle);
            p->yspd = p->gspd * -sinf(p->angle);
        }
        return;
    }

    // Balancing on ledges
    if (p->isGrounded && fabsf(p->xspd) <= 0 && !(p->sBL.coll && p->sBR.coll) && (p->sBL.coll || p->sBR.coll)) {
        const Sensor *s = p->sBL.coll ? &p->sBL : &p->sBR;
        bool result = false;
        int dir = 1;
        if (s == &p->sBR) {
            for (int i = -8; i < 8 && !result; i++) {
                bool met = false;
                for (int j = 0; j < 16 && !met; j++) met = world_point(s->x + i, s->y + j, WC_FLOOR) != NULL;
                if (met) continue;
                result = true;
                dir = (int)sgnf((float)i);
            }
        } else {
            for (int i = 8; i > -8 && !result; i--) {
                bool met = false;
                for (int j = 0; j < 16 && !met; j++) met = world_point(s->x + i, s->y + j, WC_FLOOR) != NULL;
                if (met) continue;
                result = true;
                dir = (int)sgnf((float)i);
            }
        }
        p->isOnEdge = result;
        p->edgeDir = dir >= 0 ? 1 : -1;
    } else if (!(p->isGrounded && fabsf(p->xspd) <= 0 && !(p->sBL.coll && p->sBR.coll))) {
        p->isOnEdge = false;
    }

    // Emotions (taunts). EXE taunt sounds and the taunt1/taunt2 unlocks: TODO(M7).
    bool amy_sally = p->character == CHARACTER_AMY || p->character == CHARACTER_SALLY;
    if (p->state != ST_IDLE && p->state != ST_EMOTION1 && p->state != ST_EMOTION2 && p->state != ST_EMOTION3 &&
        (!amy_sally || p->state != 15) && p->emotion)
        p->emotion = false;
    if (!p->emotion && !p->isOnEdge && p->state == ST_IDLE) {
        bool exe = p->character == CHARACTER_EXE;
        int st = k->em1_p ? ((!exe || p->taunt1) ? ST_EMOTION1 : -1)
               : k->em2_p ? ((!exe || p->taunt2) ? ST_EMOTION2 : -1)
               : k->em3_p ? ST_EMOTION3 : -1;
        if (st >= 0) {
            p->image_index = 0;
            p->state = st;
            p->emotion = true;
        }
    }

    if (p->isGrounded) {
        p->isJumping = false;
        if (p->canLookDown && k->down) {
            if (p->gspd == 0) { p->isLookingDown = true; p->gspd = 0; }
            else if (fabsf(p->gspd) >= 2 && !p->isSpinning && p->canSpin) {
                player_sound(p, SND_SPIN);
                p->isSpinning = true;
            }
        }
        if (p->canLookUp && k->up && p->gspd == 0) { p->isLookingUp = true; p->gspd = 0; }

        if (!p->isSpinning) {
            if (!p->isLookingDown && !p->isLookingUp) {
                if (k->left) {
                    if (p->gspd < 0) p->image_xscale = -1;
                    if (p->gspd > 0) {
                        p->gspd -= 0.5f;
                        if (p->gspd <= 0) p->gspd = -0.5f;
                    } else if (p->gspd > -p->maxHSpeed) {
                        p->gspd -= p->acc;
                        if (p->gspd <= -p->maxHSpeed) p->gspd = -p->maxHSpeed;
                    }
                } else if (k->right) {
                    if (p->gspd > 0) p->image_xscale = 1;
                    if (p->gspd < 0) {
                        p->gspd += 0.5f;
                        if (p->gspd >= 0) p->gspd = 0.5f;
                    } else if (p->gspd < p->maxHSpeed) {
                        p->gspd += p->acc;
                        if (p->gspd >= p->maxHSpeed) p->gspd = p->maxHSpeed;
                    }
                } else {
                    p->gspd -= fminf(fabsf(p->gspd), p->acc) * sgnf(p->gspd);
                }
            }
        } else {
            if (k->left && p->gspd > 0) {
                p->gspd -= 0.125f;
                if (p->gspd <= 0) p->isSpinning = false;
            } else if (k->right && p->gspd < 0) {
                p->gspd += 0.125f;
                if (p->gspd >= 0) p->isSpinning = false;
            }
            p->gspd -= fminf(fabsf(p->gspd), 0.0234375f) * sgnf(p->gspd);
        }

        float slope = p->isSpinning ? 0.225f : 0.125f;
        p->gspd -= slope * sinf(p->angle);
        p->xspd = p->gspd * cosf(p->angle);
        p->yspd = p->gspd * -sinf(p->angle);
        if (p->gspd == 0) p->isSpinning = false;
    }

    HOOK(p, special, p, k);

    if (!p->isGrounded) {
        p->angle = 0;
        if (k->left) { p->image_xscale = -1; p->xspd -= p->acc * 2; }
        else if (k->right) { p->image_xscale = 1; p->xspd += p->acc * 2; }
        if (p->yspd < 0 && p->yspd > -4 && fabsf(p->xspd) > 0) {
            float d = ceilf(p->xspd / 0.125f);
            p->xspd -= d / 256;
        }
    } else if (k->a_p && !p->isJumping) {
        if (HOOK_B(p, jump_blocked, p)) return;
        p->isGrounded = false;
        p->isSpinning = false;
        p->isJumping = true;
        p->justJumped = true;
        p->image_index = 0;
        p->xspd -= p->jumpForce * sinf(p->angle);
        p->yspd = -p->jumpForce * cosf(p->angle);
        player_sound(p, SND_JUMP);
    }
}

// ------------------------------------------------------------------ step pipeline

void player_stun_tick(Player *p)
{
    p->isBoosting = p->isLookingDown = p->isLookingUp = p->isAttacking = p->isJumping = false;
    p->gspd -= fminf(fabsf(p->gspd), p->acc) * sgnf(p->gspd);
    p->xspd -= fminf(fabsf(p->xspd), p->acc) * sgnf(p->xspd);
    p->shockedTimer--;
    p->hurttime = 60 * 2;
}

void player_step(Player *p, const Keys *k_in, int room_w, int room_h)
{
    static const Keys none;
    const Keys *k = player_controls ? k_in : &none;
    p->emHeld[0] = k->em1;
    p->emHeld[1] = k->em2;
    p->emHeld[2] = k->em3;

    // Step (Step_0): movement, or the stun countdown
    if (p->def && p->def->step_move) p->def->step_move(p, k);
    else if (p->shockedTimer <= 0) player_move_basic(p, k);
    else player_stun_tick(p);
    collision_basic(p, k, room_w, room_h);
    if (p->redRingTimer > 0) p->redRingTimer--;
    if (p->hurttime > 0) p->hurttime--;
    HOOK(p, step, p, k);

    if (p->alarm4 > 0 && --p->alarm4 == 0) {
        p->acc = p->def->base_acc;
        p->maxHSpeed = p->def->base_maxspeed;
        p->isSlow = false;
        HOOK(p, alarm4, p);
    }

    // End Step
    HOOK(p, end_step, p);

    // Built-in image_index advance, then Animation End, then Pre-Draw.
    const SpriteInfo *s = sprite_info(p->sprite);
    if (s && s->frame_count > 0) {
        p->image_index += p->image_speed * sprite_frame_step(p->sprite);
        if (p->image_index >= s->frame_count) {
            p->image_index -= s->frame_count;
            HOOK(p, animation_end, p);
        } else if (p->image_index < 0) {
            p->image_index += s->frame_count;
        }
    }
    HOOK(p, pre_draw, p);
    if (p->effectTime > 0) p->effectTime--;  // Draw_0 counts it down
}

void player_draw_body(const Player *p, float cam_x, float cam_y, float angle_deg)
{
    float alpha = (p->hurttime > 0 || p->isHiding) ? 0.5f : 1.0f;
    u32 blend = player_room_id == ROOM_ACT9 ? 0xFF000000 : 0xFFFFFFFF;  // scr_move_basic: c_black in Act 9
    sprite_draw(p->sprite, p->image_index, floorf(p->x) - cam_x, floorf(p->y) - cam_y, p->image_xscale, 1,
                angle_deg, blend, alpha);
}

void player_draw(const Player *p, float cam_x, float cam_y)
{
    // TODO(M8): effectTime afterimages, palette swap
    if (p->def && p->def->draw) p->def->draw(p, cam_x, cam_y);
    else player_draw_body(p, cam_x, cam_y, p->angle * 180.0f / (float)M_PI);
}
