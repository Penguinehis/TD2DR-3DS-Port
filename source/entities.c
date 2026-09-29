#include "entities.h"

#include <math.h>

#include "audio.h"
#include "achiev.h"
#include "chars/chars.h"
#include "game.h"
#include "gen/objects.h"
#include "gen/sounds.h"
#include "gen/rooms.h"
#include "gen/sprites.h"
#include "level.h"
#include "player.h"
#include "room.h"
#include "settings.h"
#include "sprite.h"
#include "world.h"

// global.net_effs (net_effects.gml): CLIENT_SPAWN_EFFECT sends an index into this table
static const int NET_EFFS[] = {
    SPR_RING_SPARKLE, SPR_RING_TELEPORT, SPR_BLOOD1, SPR_BLOOD2, SPR_BLOOD3, SPR_RINGLOSE,
    SPR_SHIELDBREAK, SPR_DUST, SPR_EGGPACK, SPR_ROSEHEART, SPR_EROSEHEART, SPR_SHOCKPARTICLE,
    SPR_SHIELDBREAK2, SPR_BLACKRING_SPARKLE, SPR_BLACKRING_SPARKLE_PURPLE, SPR_WATERSPLASH,
    SPR_SHARD_SPARKLE,
};
#define NET_EFFS_COUNT (int)(sizeof NET_EFFS / sizeof NET_EFFS[0])

#define MAX_EFFECTS 96
#define MAX_TRACKERS 8
#define MAX_BRINGS 16
#define MAX_BALLS 16
#define MAX_CLONES 4
#define MAX_PARTS 32

typedef struct {
    bool used, fade;
    int sprite;
    float x, y, frame, speed, alpha, fade_speed, hspeed, vspeed, xscale;
} Effect;

typedef struct {
    bool active, breaking, exe, owner;
    float x, y, frame;
    int sprite, dir, dmg, charge;
} Projectile;

typedef struct {
    bool used, activated, destroying;
    u16 nid;
    float x, y, frame;
} Tracker;

typedef struct {
    bool used, taken, purple;
    u16 nid;
    float x, y, alpha;
} BlackRing;

typedef struct {
    bool used, visible;
    int dir, cnt, life;
    float x, y, frame;
} StompBall;

typedef struct {
    bool used;
    u16 nid;
    int pindex, sprite;
    float x, y, xscale;
    char pname[32];
} Clone;

typedef struct {
    bool used;
    int sprite, timer;
    float x, y, xspd, yspd, angle, frame;
} RingPart;

static Effect effects[MAX_EFFECTS];
static Projectile proj;
static Tracker trackers[MAX_TRACKERS];
static BlackRing brings[MAX_BRINGS];
static StompBall balls[MAX_BALLS];
static Clone clones[MAX_CLONES];
static RingPart parts[MAX_PARTS];

static bool local_exe(void) { return net.exe_id == net.id; }

void (*ents_on_tracker_reveal)(u16 target);
static Player *me(void) { return level.has_player ? &level.player : NULL; }

void ents_clear(void)
{
    memset(effects, 0, sizeof effects);
    memset(&proj, 0, sizeof proj);
    memset(trackers, 0, sizeof trackers);
    memset(brings, 0, sizeof brings);
    memset(balls, 0, sizeof balls);
    memset(clones, 0, sizeof clones);
    memset(parts, 0, sizeof parts);
}

// ------------------------------------------------------------------ helpers

typedef struct { float l, t, r, b; } Box;

static bool box_of(int spr, float x, float y, float xscale, Box *o)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return false;
    float l = x + (s->bbox_left - s->xorigin) * xscale, r = x + (s->bbox_right + 1 - s->xorigin) * xscale;
    o->l = fminf(l, r);
    o->r = fmaxf(l, r);
    o->t = y + s->bbox_top - s->yorigin;
    o->b = y + s->bbox_bottom + 1 - s->yorigin;
    return true;
}

// place_meeting(x, y, global.player)
static bool meets_player(int spr, float x, float y, float xscale)
{
    Player *p = me();
    if (!p) return false;
    Box a, b;
    if (!box_of(spr, x, y, xscale, &a) || !box_of(p->sprite, floorf(p->x), floorf(p->y), p->image_xscale, &b)) return false;
    return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
}

static bool meets_floor(int spr, float x, float y, float xscale)
{
    Box a;
    if (!box_of(spr, x, y, xscale, &a)) return world_point(x, y, WC_FLOOR) != NULL;
    return world_rect(a.l, a.t, a.r - 1, a.b - 1, WC_FLOOR) != NULL;
}

static int frames_of(int spr)
{
    const SpriteInfo *s = sprite_info(spr);
    return s && s->frame_count > 0 ? s->frame_count : 1;
}

static int obj_sprite(int obj)
{
    const ObjectInfo *o = object_info(obj);
    return o ? o->sprite : -1;
}

static void send_stats_damage(u16 master, int dmg, int hp)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_STATS_REPORT);
    pkt_u8(&pk, 2);
    pkt_u16(&pk, master);
    pkt_u16(&pk, (u16)dmg);
    pkt_u16(&pk, (u16)hp);
    net_send(&pk, true);
}

static void send_mercoin(u16 master, u8 kind, int extra)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_MERCOIN_BONUS);
    pk.buf[0] = 1;
    pkt_u16(&pk, master);
    pkt_u8(&pk, kind);
    if (extra >= 0) pkt_u8(&pk, (u8)extra);
    net_send(&pk, true);
}

// ------------------------------------------------------------------ effects

void effect_quick(float x, float y, int sprite, float speed, bool fade, float xspd, float yspd, float xscale)
{
    if (sprite < 0 || !settings.gfx_effects) return;
    Effect *e = NULL;
    for (int i = 0; i < MAX_EFFECTS; i++)
        if (!effects[i].used) { e = &effects[i]; break; }
    if (!e) return;
    *e = (Effect){ .used = true, .fade = fade, .sprite = sprite, .x = x, .y = y, .speed = speed, .alpha = 1,
                   .fade_speed = speed, .hspeed = xspd, .vspeed = yspd, .xscale = xscale };
}

void net_quick_effect(float x, float y, int sprite, bool fade, int dir, int xspd, int yspd, float speed)
{
    effect_quick(x, y, sprite, speed, fade, xspd, yspd, dir);
    int idx = -1;
    for (int i = 0; i < NET_EFFS_COUNT; i++)
        if (NET_EFFS[i] == sprite) { idx = i; break; }
    if (idx < 0) return;
    NetPacket pk;
    pkt_begin(&pk, CLIENT_SPAWN_EFFECT);
    pk.buf[0] = 1;
    pkt_u16(&pk, (u16)(int)x);
    pkt_u16(&pk, (u16)(int)y);
    pkt_u8(&pk, fade);
    pkt_u8(&pk, (u8)(s8)dir);
    pkt_u8(&pk, (u8)idx);
    pkt_u8(&pk, (u8)(s8)xspd);
    pkt_u8(&pk, (u8)(s8)yspd);
    pkt_f32(&pk, speed);
    net_send(&pk, true);
}

static void effects_step(void)
{
    for (int i = 0; i < MAX_EFFECTS; i++) {
        Effect *e = &effects[i];
        if (!e->used) continue;
        e->x += e->hspeed;
        e->y += e->vspeed;
        if (e->fade) {
            // obj_quickeffect_fade
            e->alpha = fmaxf(0, e->alpha - e->fade_speed);
            if (e->alpha <= 0) e->used = false;
        } else {
            // obj_quickeffect: destroyed after the last frame
            e->frame += e->speed * sprite_frame_step(e->sprite);
            if (e->frame > frames_of(e->sprite) - 1) e->used = false;
        }
    }
}

// ------------------------------------------------------------------ Tails projectile

static void proj_destroy(void)
{
    // obj_tails_projectile._destroy
    if (proj.breaking) return;
    dbg_log("ents: projectile breaks at %.0f,%.0f", proj.x, proj.y);
    proj.breaking = true;
    static const int dead[6] = { SPR_TAIL_RAY_DEAD, SPR_TAIL_RAY_DEAD1, SPR_TAIL_RAY_DEAD2, SPR_TAIL_RAY_DEAD3,
                                 SPR_TAIL_RAY_DEAD4, SPR_TAIL_RAY_DEAD4 };
    proj.sprite = dead[proj.charge >= 0 && proj.charge <= 5 ? proj.charge : 0];
    proj.frame = 0;
    NetPacket pk;
    pkt_begin(&pk, CLIENT_TPROJECTILE_HIT);
    net_send(&pk, true);
}

static void proj_step(void)
{
    if (!proj.active) return;
    proj.frame += 1.2f * sprite_frame_step(proj.sprite);
    if (proj.breaking) {
        if (proj.frame >= frames_of(proj.sprite) - 1) proj.active = false;
        return;
    }
    if (proj.frame >= frames_of(proj.sprite)) proj.frame -= frames_of(proj.sprite);
    if (meets_floor(proj.sprite, proj.x, proj.y, proj.dir)) {  // Collision with obj_solid_parent
        proj_destroy();
        return;
    }
    Player *p = me();
    if (!p || !meets_player(proj.sprite, proj.x, proj.y, proj.dir) || p->isDead) return;
    u16 tails_id;
    bool tails_demon;
    bool have_tails = game_puppet_of(CHARACTER_TAILS, &tails_id, &tails_demon);

    if (proj.exe && !local_exe() && p->revivalTimes < 2) {
        // a demon Tails shot hits a survivor
        if (p->hurttime > 0) return;
        player_hurt(p, proj.dmg, -sgnf(proj.x - p->x) * 4, 2);
        if (have_tails) {
            send_mercoin(tails_id, 2, proj.dmg);
            send_stats_damage(tails_id, proj.dmg, p->hp);
            if (p->hp <= 0) send_mercoin(tails_id, 4, -1);
        }
        proj_destroy();
        return;
    }
    if (!proj.exe && (local_exe() || p->revivalTimes >= 2)) {
        // a survivor shot stuns the EXE or a demon
        if (local_exe() && p->invisTimer > 0) return;
        if (p->hurttime > 0) return;
        if (p->character == CHARACTER_SALLY && player_hurt_blocked(p)) {  // the shield breaks
            p->isHurt = true;
            proj_destroy();
            return;
        }
        p->shockedTimer = 60 * proj.dmg;
        p->xspd = -sgnf(proj.x - p->x) * 4;
        p->yspd = -2;
        p->isGrounded = false;
        player_sound(p, SND_EXE_STUN);
        if (have_tails) {
            if (local_exe()) send_mercoin(tails_id, 2, proj.dmg);
            NetPacket pk;
            pkt_begin(&pk, CLIENT_STATS_REPORT);
            pkt_u8(&pk, 1);
            pkt_u16(&pk, net.id);
            pkt_u16(&pk, tails_id);
            pkt_u8(&pk, (u8)proj.dmg);
            net_send(&pk, true);
        }
        proj_destroy();
    }
}

// ------------------------------------------------------------------ Eggman tracker

static void trackers_step(void)
{
    Player *p = me();
    for (int i = 0; i < MAX_TRACKERS; i++) {
        Tracker *t = &trackers[i];
        if (!t->used) continue;
        if (t->destroying) {
            t->frame += sprite_frame_step(SPR_EGGTRACK_DESTROY);
            if (t->frame >= frames_of(SPR_EGGTRACK_DESTROY) - 1) t->used = false;
            continue;
        }
        t->frame += sprite_frame_step(SPR_EGGTRACK);
        if (!p || t->activated || !meets_player(SPR_EGGTRACK, t->x, t->y, 1)) continue;
        if (p->character == CHARACTER_EGGMAN || p->isDead) continue;
        u16 egg_id;
        bool demon = false;
        game_puppet_of(CHARACTER_EGGMAN, &egg_id, &demon);
        bool me_demon = p->revivalTimes >= 2 || local_exe();
        if (demon && me_demon) continue;   // a demon Eggman's trap catches survivors
        if (!demon && !me_demon) continue; // a survivor Eggman's trap catches the EXE / demons
        if (p->character == CHARACTER_EXE) {
            // the EXEs: 30% slower for 3 s with their own base speeds
            p->isSlow = true;
            p->acc = p->def->base_acc * (100 - 30) / 100;
            p->maxHSpeed = p->def->base_maxspeed * (100 - 30) / 100;
            p->alarm4 = 60 * 3;
        } else {
            player_slow(p, 3);  // demons
        }
        player_sound(p, SND_EGG_TRACKER_ACTIVATE);
        NetPacket pk;
        pkt_begin(&pk, CLIENT_ETRACKER_ACTIVATED);
        pkt_u16(&pk, t->nid);
        net_send(&pk, true);
        t->activated = true;
    }
}

// ------------------------------------------------------------------ black rings

static void brings_step(void)
{
    Player *p = me();
    for (int i = 0; i < MAX_BRINGS; i++) {
        BlackRing *b = &brings[i];
        if (!b->used) continue;
        if (b->alpha < 1) b->alpha += 1 / 30.0f;
        if (b->taken || b->alpha < 1 || !p) continue;
        if (!meets_player(SPR_BLACKRING, b->x, b->y, 1)) continue;
        if (local_exe() || p->revivalTimes >= 2 || p->hp <= 0) continue;
        achiev_round.rBlackRings++;
        NetPacket pk;
        pkt_begin(&pk, CLIENT_BRING_COLLECTED);
        pkt_u16(&pk, b->nid);
        net_send(&pk, true);
        b->taken = true;
    }
}

bool ents_own_projectile_meets(float l, float t, float r, float b)
{
    if (!proj.active || proj.breaking || !proj.owner) return false;
    Box a;
    if (!box_of(proj.sprite, proj.x, proj.y, proj.dir, &a)) return false;
    return a.l < r && l < a.r && a.t < b && t < a.b;
}

bool ents_projectile_meets(float l, float t, float r, float b)
{
    if (!proj.active || proj.breaking) return false;
    Box a;
    if (!box_of(proj.sprite, proj.x, proj.y, proj.dir, &a)) return false;
    return a.l < r && l < a.r && a.t < b && t < a.b;
}

bool ents_stompball_meets(float l, float t, float r, float b)
{
    int spr = obj_sprite(OBJ_EXETIOR_STOMPBALLS);
    for (int i = 0; i < MAX_BALLS; i++) {
        const StompBall *s = &balls[i];
        if (!s->used || !s->visible) continue;
        Box a;
        if (box_of(spr, s->x, s->y, -s->dir, &a) && a.l < r && l < a.r && a.t < b && t < a.b) return true;
    }
    return false;
}

bool ents_blackring_near(float x, float y, float dist)
{
    for (int i = 0; i < MAX_BRINGS; i++)
        if (brings[i].used && fabsf(brings[i].x - x) < dist + 16 && fabsf(brings[i].y - y) < dist + 16 &&
            sqrtf((brings[i].x - x) * (brings[i].x - x) + (brings[i].y - y) * (brings[i].y - y)) < dist + 16)
            return true;
    return false;
}

// ------------------------------------------------------------------ Exetior stomp balls

static void spawn_ball(float x, float y, int dir, int cnt)
{
    for (int i = 0; i < MAX_BALLS; i++)
        if (!balls[i].used) {
            // lives while snd_exetior_shockwave plays
            balls[i] = (StompBall){ .used = true, .visible = true, .dir = dir, .cnt = cnt, .x = x, .y = y,
                                    .life = (int)(SOUND_DEFS[SND_EXETIOR_SHOCKWAVE].duration * 60) };
            if (cnt == 0) audio_play(SND_EXETIOR_SHOCKWAVE);  // TODO(M8): positional gain
            return;
        }
}

static void balls_step(void)
{
    int spr = obj_sprite(OBJ_EXETIOR_STOMPBALLS);
    Player *p = me();
    for (int i = 0; i < MAX_BALLS; i++) {
        StompBall *b = &balls[i];
        if (!b->used) continue;
        if (--b->life <= 0 || !meets_floor(spr, b->x, b->y, -b->dir) || b->cnt > 1) {
            b->used = false;
            continue;
        }
        if (!b->visible) continue;
        b->frame += sprite_frame_step(spr);
        if (b->frame >= frames_of(spr) - 1) {
            if (b->cnt <= 1) {
                int cnt = ++b->cnt;
                spawn_ball(b->x + (b->dir > 0 ? 25 : -25), b->y, b->dir, cnt);
            }
            b->visible = false;
            continue;
        }
        if (!p || !meets_player(spr, b->x, b->y, -b->dir)) continue;
        if (local_exe() || p->revivalTimes >= 2 || p->hp <= 0 || p->hurttime > 0) continue;
        player_hurt(p, 20, b->dir * 3, -6);
        u16 exe_id;
        bool d;
        if (game_puppet_of(CHARACTER_EXE, &exe_id, &d)) {
            if (p->hp <= 0) send_mercoin(exe_id, 0, -1);  // scr_exe_checkwin
            send_mercoin(exe_id, 7, -1);
            send_stats_damage(exe_id, 20, p->hp);
            NetPacket pk;
            pkt_begin(&pk, CLIENT_PLAYER_HURT);
            pk.buf[0] = 1;
            pkt_u16(&pk, exe_id);
            pkt_u16(&pk, (u16)(int)b->x);
            pkt_u16(&pk, (u16)(int)b->y);
            pkt_u16(&pk, 0);
            net_send(&pk, true);
        }
    }
}

// ------------------------------------------------------------------ Exeller clones

static void clones_step(void)
{
    for (int i = 0; i < MAX_CLONES; i++) {
        Clone *c = &clones[i];
        if (!c->used) continue;
        c->sprite = world_point(c->x, c->y + 18, WC_FLOOR) ? SPR_EXELLER_CLONE : SPR_EXELLER_CLONE2;
        // An idle Exeller makes his clones stand idle too
        bool exe_idle = local_exe() ? (me() && me()->state == ST_IDLE) : game_exe_state() == ST_IDLE;
        if (exe_idle && c->sprite == SPR_EXELLER_CLONE) c->sprite = SPR_EXELLER_IDLE;
        // TODO(M8): the EXE's arrows pointing at survivors near each clone (obj_exeller_indicator_*)
    }
}

// ------------------------------------------------------------------ ring shards (obj_ringpart)

static void spawn_parts(float x, float y, float xspd)
{
    if (!settings.gfx_effects) return;
    static const int sprs[4] = { SPR_RINGPART, SPR_RINGPART2, SPR_RINGPART3, SPR_RINGPART4 };
    static const float ox[4] = { 12, 0, 4, 12 }, oy[4] = { 4, 12, 4, 12 };
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < MAX_PARTS; i++)
            if (!parts[i].used) {
                float xs = (k == 0 || k == 3 ? 1 : -1) + xspd;
                float ys = (k == 0 || k == 2) ? -(2 + rand() % 3) : 1 + rand() % 2;
                parts[i] = (RingPart){ .used = true, .sprite = sprs[k], .timer = (7 + rand() % 3) * 60,
                                       .x = x + ox[k], .y = y + oy[k], .xspd = xs, .yspd = ys };
                break;
            }
}

static void parts_step(void)
{
    for (int i = 0; i < MAX_PARTS; i++) {
        RingPart *r = &parts[i];
        if (!r->used) continue;
        if (--r->timer <= 0) { r->used = false; continue; }
        r->x += r->xspd;
        r->y += r->yspd;
        r->angle += r->xspd;
        r->frame += (r->yspd + r->xspd) * 0.1f;
        if (world_point(r->x + 4 + r->xspd, r->y - 1, WC_FLOOR) || world_point(r->x - 4 + r->xspd, r->y - 1, WC_FLOOR))
            r->xspd = 0;
        for (int guard = 0; guard < 64 && world_point(r->x, r->y + 4, WC_FLOOR); guard++) {
            r->yspd = 0;
            r->y--;
        }
        if (world_point(r->x, r->y + 5, WC_FLOOR)) {
            r->xspd -= fminf(fabsf(r->xspd), 0.0512f) * sgnf(r->xspd);
            continue;
        }
        r->yspd += 0.32f;
    }
}

// ------------------------------------------------------------------ packets

static bool exe_is_menace(void) { return false; }  // TODO(M8): palettes ("menace" black ring colour)

bool ents_packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    Player *p = me();
    switch (type) {
    case SERVER_TPROJECTILE_STATE: {
        if (pass) return true;
        u8 st = rd_u8(r);
        if (!reliable) {
            // net_udpprocess: position updates
            if (st != 1 || !proj.active || proj.breaking) return true;
            proj.x = rd_u16(r);
            proj.y = rd_u16(r);
            return true;
        }
        if (st == 0) {
            if (proj.active) return true;  // "don't spawn again"
            proj = (Projectile){ .active = true };
            proj.x = rd_u16(r);
            proj.y = rd_u16(r);
            proj.owner = rd_u16(r) == net.id;
            proj.dir = (s8)rd_u8(r);
            proj.dmg = rd_u8(r);
            proj.exe = rd_u8(r);
            proj.charge = rd_u8(r);
            proj.sprite = obj_sprite(OBJ_TAILS_PROJECTILE);
            proj.frame = proj.charge;
            dbg_log("ents: projectile at %.0f,%.0f dir %d dmg %d charge %d", proj.x, proj.y, proj.dir, proj.dmg, proj.charge);
        } else if (st == 2 && proj.active) {
            proj_destroy();
        }
        return true;
    }
    case SERVER_ETRACKER_STATE: {
        if (pass) return true;
        u8 st = rd_u8(r);
        if (st == 0) {
            u16 id = rd_u16(r);
            float x = rd_u16(r), y = rd_u16(r);
            for (int i = 0; i < MAX_TRACKERS; i++)
                if (!trackers[i].used) {
                    trackers[i] = (Tracker){ .used = true, .nid = id, .x = x, .y = y };
                    break;
                }
        } else {
            u16 id = rd_u16(r);
            u16 pid = rd_u16(r);
            if (p && p->character == CHARACTER_EGGMAN) achiev_tracker(p->revivalTimes <= 2 && pid == net.exe_id);
            for (int i = 0; i < MAX_TRACKERS; i++)
                if (trackers[i].used && trackers[i].nid == id) {
                    trackers[i].destroying = trackers[i].activated = true;
                    trackers[i].frame = 0;
                }
            // obj_surv_indicator: the Eggman's side sees who stepped on it for 3 s
            if (p) {
                u16 egg_id;
                bool demon = false;
                bool me_egg = p->character == CHARACTER_EGGMAN;
                if (me_egg) demon = p->revivalTimes >= 2;
                else game_puppet_of(CHARACTER_EGGMAN, &egg_id, &demon);
                bool me_demon = p->revivalTimes >= 2 || local_exe();
                if (demon == me_demon && ents_on_tracker_reveal) ents_on_tracker_reveal(pid);
            }
        }
        return true;
    }
    case SERVER_BRING_STATE: {
        if (pass) return true;
        u8 st = rd_u8(r);
        u16 id = rd_u16(r);
        if (st == 0) {
            RoomInstance *sp = room_instance_find(&level.room, OBJ_BLACKRING_SPAWNER, id - 1);
            if (!sp) return true;
            for (int i = 0; i < MAX_BRINGS; i++)
                if (!brings[i].used) {
                    brings[i] = (BlackRing){ .used = true, .nid = id, .x = sp->x, .y = sp->y };
                    break;
                }
        } else if (st == 1) {
            for (int i = 0; i < MAX_BRINGS; i++)
                if (brings[i].used && brings[i].nid == id) {
                    effect_quick(brings[i].x, brings[i].y,
                                 exe_is_menace() ? SPR_BLACKRING_SPARKLE_PURPLE : SPR_BLACKRING_SPARKLE, 0.5f, false,
                                 0, 0, 1);
                    brings[i].used = false;
                }
        }
        return true;
    }
    case SERVER_ERECTOR_BRING_SPAWN: {
        if (pass) return true;
        u16 gid = rd_u16(r);
        float x = rd_u16(r), y = rd_u16(r);
        for (int i = 0; i < MAX_BRINGS; i++)
            if (!brings[i].used) {
                brings[i] = (BlackRing){ .used = true, .nid = gid, .x = x, .y = y, .purple = exe_is_menace() };
                break;
            }
        return true;
    }
    case SERVER_BRING_COLLECTED: {
        if (pass || !p) return true;
        if (p->rings >= 5) {
            p->rings -= 5;
            player_sound(p, SND_RINGABSORB);
            return true;
        }
        if (p->rings < 10) {
            p->hp -= 20;
            NetPacket pk;
            pkt_begin(&pk, CLIENT_STATS_REPORT);
            pkt_u8(&pk, 3);
            pkt_u8(&pk, 20);
            net_send(&pk, true);
            if (p->hp <= 0) {
                p->hp = 1;
                p->hurttime = 0;
                player_hurt(p, 1, 0, 0);  // scr_player_instakill through the shared death path
            }
        } else {
            p->rings -= 10;
        }
        player_sound(p, SND_BLACKRING);
        return true;
    }
    case CLIENT_ERECTOR_BALLS: {
        float x = rd_f32(r), y = rd_f32(r);
        spawn_ball(x - 25, y + 19, -1, 0);
        spawn_ball(x + 25, y + 19, 1, 0);
        return true;
    }
    case SERVER_EXELLERCLONE_STATE: {
        if (pass) return true;
        u8 st = rd_u8(r);
        if (st == 0) {
            u16 id = rd_u16(r), pid = rd_u16(r);
            float x = rd_u16(r), y = rd_u16(r);
            s8 dir = (s8)rd_u8(r);
            Clone *c = NULL;
            for (int i = 0; i < MAX_CLONES; i++)
                if (!clones[i].used) { c = &clones[i]; break; }
            if (!c) return true;
            *c = (Clone){ .used = true, .nid = id, .x = x, .y = y, .xscale = dir, .sprite = SPR_EXELLER_CLONE };
            NetPlayer *np = net_player(pid);
            snprintf(c->pname, sizeof c->pname, "%s", np ? np->nickname : "");
            if (local_exe() && p) {
                ExellerShared *ex = exeller_shared(p);
                if (ex) {
                    c->pindex = ex->clones[0] != -1 ? 1 : 0;
                    ex->clones[c->pindex] = id;
                    ex->cloneCount++;
                }
                player_sound(p, SND_EXELLER_CLONE);
            }
            net_quick_effect(x, y + 32, SPR_RING_TELEPORT, false, 1, 0, 0, 2);
        } else {
            u16 id = rd_u16(r);
            for (int i = 0; i < MAX_CLONES; i++) {
                Clone *c = &clones[i];
                if (!c->used || c->nid != id) continue;
                if (local_exe() && p) {
                    p->x = c->x;
                    p->y = c->y;
                    ExellerShared *ex = exeller_shared(p);
                    if (ex) {
                        ex->clones[c->pindex] = -1;
                        ex->cloneTimer = 30 * 60;  // EXELLER_CLONE_RECHARGE
                        ex->cloneCount--;
                    }
                    player_sound(p, SND_EXE_APPEAR);
                    net_quick_effect(c->x, c->y + 32, SPR_RING_TELEPORT, false, 1, 0, 0, 2);
                    game_exe_indicator_show(7 * 60);  // obj_exetior_indicator.showTimer
                    achiev_round.rUsedClone = true;
                }
                c->used = false;
            }
        }
        return true;
    }
    case CLIENT_SPAWN_EFFECT: {
        float x = rd_u16(r), y = rd_u16(r);
        bool fade = rd_u8(r);
        s8 dir = (s8)rd_u8(r);
        u8 spr = rd_u8(r);
        s8 xs = (s8)rd_u8(r), ys = (s8)rd_u8(r);
        float spd = rd_f32(r);
        if (spr < NET_EFFS_COUNT) effect_quick(x, y, NET_EFFS[spr], spd, fade, xs, ys, dir);
        return true;
    }
    case CLIENT_RING_BROKE: {
        float x = rd_u16(r), y = rd_u16(r), xs = rd_f16(r);
        spawn_parts(x, y, isfinite(xs) ? xs : 0);
        return true;
    }
    default:
        return false;
    }
}

// ------------------------------------------------------------------ step / draw

void ents_step(void)
{
    effects_step();
    proj_step();
    trackers_step();
    brings_step();
    balls_step();
    clones_step();
    parts_step();
}

static void draw_at(int spr, float frame, float x, float y, float xs, float angle, float alpha)
{
    x -= level.cam_x;
    y -= level.cam_y;
    if (x < -128 || y < -128 || x > TOP_W + 128 || y > TOP_H + 128) return;
    sprite_draw(spr, frame, x, y, xs, 1, angle, 0xFFFFFFFF, alpha);
}

void ents_draw(void)
{
    for (int i = 0; i < MAX_TRACKERS; i++)
        if (trackers[i].used)
            draw_at(trackers[i].destroying ? SPR_EGGTRACK_DESTROY : SPR_EGGTRACK, trackers[i].frame, trackers[i].x,
                    trackers[i].y, 1, 0, 1);
    for (int i = 0; i < MAX_CLONES; i++)
        if (clones[i].used) {
            draw_at(clones[i].sprite, level.time * sprite_frame_step(clones[i].sprite), floorf(clones[i].x),
                    floorf(clones[i].y), clones[i].xscale, 0, 1);
            if (local_exe())
                draw_at(SPR_EXELLER_CLONEARROW, clones[i].pindex, floorf(clones[i].x),
                        floorf(clones[i].y) - 35 - sinf(level.time * 16.7f / 200), 1, 0, 1);
        }
    int ball_spr = obj_sprite(OBJ_EXETIOR_STOMPBALLS);
    for (int i = 0; i < MAX_BALLS; i++)
        if (balls[i].used && balls[i].visible) draw_at(ball_spr, balls[i].frame, balls[i].x, balls[i].y, -balls[i].dir, 0, 1);
    if (proj.active) draw_at(proj.sprite, proj.frame, proj.x, proj.y, proj.dir, 0, 1);
    for (int i = 0; i < MAX_PARTS; i++)
        if (parts[i].used)
            draw_at(parts[i].sprite, parts[i].frame, parts[i].x, parts[i].y, 1, parts[i].angle,
                    parts[i].timer < 60 ? parts[i].timer / 60.0f : 1);
}

void ents_draw_front(void)
{
    for (int i = 0; i < MAX_BRINGS; i++) {
        if (!brings[i].used || brings[i].taken) continue;
        int spr = brings[i].purple ? SPR_BLACKRING_PURPLE : SPR_BLACKRING;
        draw_at(spr, game_ring_frame(), brings[i].x, brings[i].y, 1, 0, brings[i].alpha);
        if (level.room_id == ROOM_MAJONGFOREST) {
            // obj_blackring Draw_0: a copy on the other side of the wrapped edge
            const SpriteInfo *si = sprite_info(spr);
            float w = level.room.width, sw = si ? si->width : 0, x = brings[i].x;
            float wx = x < 512 ? x + (w - 512) + sw : x >= w - 512 ? x - (w - 512) - sw : 0;
            draw_at(spr, game_ring_frame(), wx, brings[i].y, 1, 0, brings[i].alpha);
        }
    }
    for (int i = 0; i < MAX_EFFECTS; i++) {
        const Effect *e = &effects[i];
        if (e->used) draw_at(e->sprite, e->frame, e->x, e->y, e->xscale, 0, e->alpha);
    }
}
