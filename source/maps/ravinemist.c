// room_ravinemist: obj_ravinemist_controller (shard counter UI), the slugs obj_rmzsonic
// (SERVER_RMZSLIME_STATE, SERVER_RMZSLIME_RINGBONUS, CLIENT_RMZSLIME_HIT), the shards
// obj_ravintmist_shard (SERVER_RMZSHARD_STATE, CLIENT_RMZSHARD_COLLECT / LAND) and
// obj_surv_ringindicator (the arrow to the exit ring once 6 shards are in).
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../entities.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"
#include "../achiev.h"

// Character states defined in chars/amy.c and chars/sally.c
#define AMY_HJUMP (ST_BALANCING + 2)
#define SALLY_SLIDE (ST_BALANCING + 2)

static struct {
    bool controller;          // instance_exists(obj_ravinemist_controller)
    int shard_count, prev_shard_count;
    int shards;               // global.player.shards
    bool bigring;             // obj_bigring exists (peeked from SERVER_GAME_SPAWN_RING)
    float bigring_x, bigring_y;
} rmz;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }

// Axis-aligned box of an object's mask in the room.
static bool box(const MapObj *o, float *l, float *t, float *r, float *b)
{
    const SpriteInfo *s = sprite_info(o->mask >= 0 ? o->mask : o->sprite);
    if (!s) return false;
    float x0 = o->x + (s->bbox_left - s->xorigin) * o->xscale, x1 = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
    float y0 = o->y + (s->bbox_top - s->yorigin) * o->yscale, y1 = o->y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    *l = fminf(x0, x1);
    *r = fmaxf(x0, x1);
    *t = fminf(y0, y1);
    *b = fmaxf(y0, y1);
    return true;
}

static bool objs_meet(const MapObj *a, const MapObj *b)
{
    float al, at, ar, ab, bl, bt, br, bb;
    if (!box(a, &al, &at, &ar, &ab) || !box(b, &bl, &bt, &br, &bb)) return false;
    return al < br && bl < ar && at < bb && bt < ab;
}

// ------------------------------------------------------------------ obj_ravinemist_controller

static void controller_create(MapObj *o)
{
    o->depth = -200;
    rmz.controller = true;
    rmz.shard_count = rmz.prev_shard_count = 0;
}

static void controller_draw_gui(MapObj *o)
{
    (void)o;
    // GUI (154, 231) on 480x270: kept at the same distance from the centre / bottom edge
    sprite_draw(SPR_RAVINEMIST_UI, rmz.shard_count, 154 - 40, 231 - 30, 1, 1, 0, 0xFFFFFFFF, 1);
}

static void controller_destroy(MapObj *o)
{
    (void)o;
    rmz.controller = false;
}

// ------------------------------------------------------------------ obj_rmzsonic

typedef struct {
    int state;
    bool dead, isDead;
    int timer;
} Slug;

static void slug_create(MapObj *o)
{
    Slug *v = MAPOBJ_VARS(o, Slug);
    o->nid = 0;
    v->state = 0;
    o->image_alpha = 0;
    v->dead = v->isDead = false;
    v->timer = 10;
}

// destroy(): the death animation
static void slug_kill(MapObj *o)
{
    Slug *v = MAPOBJ_VARS(o, Slug);
    v->state = 10;
    o->image_index = 0;
    o->sprite = SPR_RAVINEMIST_SONICDEAD;
    maps_sound_at(SND_SLIME, o->x, o->y);
    v->isDead = true;
}

static void send_slime_hit(const MapObj *o, bool projectile)
{
    NetPacket pk;
    pkt_begin(&pk, CLIENT_RMZSLIME_HIT);
    pkt_u16(&pk, (u16)o->nid);
    pkt_u8(&pk, projectile);
    net_send(&pk, true);
}

static void slug_step(MapObj *o)
{
    Slug *v = MAPOBJ_VARS(o, Slug);
    if (v->isDead) {
        if (o->image_index >= mapobj_frames(o) - 1) mapobj_destroy(o);
        return;
    }
    static const int SPRITES[3] = { SPR_RAVINEMIST_SONIC, SPR_RAVINEMIST_SONIC2, SPR_RAVINEMIST_SONIC3 };
    if (v->state >= 0 && v->state <= 5) {
        o->sprite = SPRITES[v->state / 2];
        o->xscale = (v->state & 1) ? -1 : 1;
    }
    if (o->image_alpha < 1) {
        o->image_alpha += 0.016f;
        return;
    }
    if (!level.has_player) return;
    // obj_exetior_stompballs / obj_tails_projectile kill the slug (projectile = true)
    float bl, bt, br, bb;
    if (!v->dead && mapobj_bbox(o, &bl, &bt, &br, &bb) &&
        (ents_stompball_meets(bl, bt, br, bb) || ents_projectile_meets(bl, bt, br, bb))) {
        send_slime_hit(o, true);
        v->dead = true;
        return;
    }
    Player *p = &level.player;
    int ch = p->character;
    if (p->hp > 0 && !v->dead && mapobj_meets_player(o)) {
        bool attacking = false;
        if (p->isAttacking && (ch == CHARACTER_EXE || ch == CHARACTER_AMY || ch == CHARACTER_KNUX ||
                               ch == CHARACTER_EGGMAN || ch == CHARACTER_SALLY))
            attacking = true;  // + scr_camera_shake (omitted)
        if (ch == CHARACTER_AMY && p->state == AMY_HJUMP) attacking = true;
        if (p->revivalTimes < 2) {
            if ((p->isJumping && ch != CHARACTER_EXE) || (p->state == ST_FALL && ch == CHARACTER_AMY)) {
                player_hurt(p, 0, 0, -5);  // snd_none, spr_blood2
                attacking = true;
            }
            if (p->isSpinning && (ch == CHARACTER_CREAM || ch == CHARACTER_KNUX || ch == CHARACTER_TAILS))
                attacking = true;
            if (p->state == SALLY_SLIDE && ch == CHARACTER_SALLY) attacking = true;
        }
        if (!attacking) {
            if (ch != CHARACTER_EXE && p->revivalTimes < 2) {
                if (v->timer++ >= 10) {
                    if (p->rings > 0) {
                        player_sound(p, SND_RINGABSORB);
                        p->rings--;
                    } else {
                        player_hurt(p, 20, -p->image_xscale * 4, -6);
                    }
                    v->timer = 0;
                }
            } else {
                v->timer = 10;
            }
            return;
        }
        send_slime_hit(o, false);
        v->state = 10;
        v->dead = true;
    } else {
        v->timer = 10;
    }
}

// ------------------------------------------------------------------ obj_ravintmist_shard

typedef struct {
    int gid;
    bool grv, ground, coll;
    float yspd, cY;
} Shard;

static void shard_create(MapObj *o)
{
    Shard *v = MAPOBJ_VARS(o, Shard);
    v->gid = 0;
    v->grv = v->ground = v->coll = false;
    v->yspd = 0;
    v->cY = -1;
    o->depth = 5;  // global.player.depth + 5
}

static void shard_step(MapObj *o)
{
    Shard *v = MAPOBJ_VARS(o, Shard);
    if (v->ground) {
        if (v->cY == -1) v->cY = o->y;
        o->y = v->cY + sinf(current_time_ms() / 200) * 3;
    }
    if (level.has_player && level.player.character != CHARACTER_EXE && level.player.hp > 0 &&
        level.player.revivalTimes < 2 && !v->coll && mapobj_meets_player(o)) {
        NetPacket pk;
        pkt_begin(&pk, CLIENT_RMZSHARD_COLLECT);
        pkt_u16(&pk, (u16)v->gid);
        net_send(&pk, true);
        v->coll = true;
    }
    if (!v->grv || v->ground) return;

    while (world_point(o->x - 7, o->y + 7, WC_FLOOR)) o->x++;
    while (world_point(o->x + 7, o->y + 7, WC_FLOOR)) o->x--;
    if (!world_rect(o->x - 7, o->y + 14, o->x + 7, o->y + 21, WC_FLOOR)) {
        if (v->yspd < 6) v->yspd += 0.16f;
        o->y += v->yspd;
    } else {
        for (int i = 0; i < 7; i++)
            if (world_rect(o->x - 7, o->y + (20 - i), o->x + 7, o->y + 21, WC_FLOOR)) o->y--;
        NetPacket pk;
        pkt_begin(&pk, CLIENT_RMZSHARD_LAND);
        pk.buf[0] = 1;  // passthrough
        pkt_u16(&pk, (u16)v->gid);
        pkt_u16(&pk, (u16)o->x);
        pkt_u16(&pk, (u16)o->y);
        net_send(&pk, true);
        v->ground = true;
    }
}

// obj_abyss Step (second half): a shard in a pit goes back to the nearest abyss target.
static void shards_abyss(void)
{
    for (int i = 0;; i++) {
        MapObj *a = mapobj_find(OBJ_ABYSS, i);
        if (!a) break;
        for (int j = 0;; j++) {
            MapObj *s = mapobj_find(OBJ_RAVINTMIST_SHARD, j);
            if (!s) break;
            if (s->destroyed || !objs_meet(a, s)) continue;
            WorldInst *t = world_nearest(a->x, a->y, WC_ABYSS_TARGET);
            if (!t) continue;
            const SpriteInfo *si = sprite_info(s->sprite);
            float h = si ? si->height * s->yscale : 0;
            s->x = t->x - h / 2;
            s->y = t->y - h / 2;
            break;
        }
    }
}

static MapObj *shard_by_gid(int gid)
{
    for (int i = 0;; i++) {
        MapObj *s = mapobj_find(OBJ_RAVINTMIST_SHARD, i);
        if (!s) return NULL;
        if (!s->destroyed && MAPOBJ_VARS(s, Shard)->gid == gid) return s;
    }
}

// ------------------------------------------------------------------ level

static void step(void)
{
    // scr_player_hurt (instakill) and SERVER_PLAYER_DEATH_STATE clear global.player.shards
    if (!level.has_player || level.player.isDead) rmz.shards = 0;
    shards_abyss();
}

static float point_dir(float x0, float y0, float x1, float y1)
{
    return atan2f(-(y1 - y0), x1 - x0) * 180.0f / (float)M_PI;
}

static void draw_gui(void)
{
    // obj_surv_ringindicator (survivors only; created by obj_spawnpoint in this room)
    if (!level.has_player || !rmz.bigring) return;
    const Player *p = &level.player;
    if (p->character == CHARACTER_EXE || p->revivalTimes >= 2 || p->hp <= 0 || rmz.shards <= 0) return;
    if (!rmz.controller || rmz.shard_count < 6) return;
    float angle = point_dir(p->x, p->y, rmz.bigring_x, rmz.bigring_y);
    sprite_draw(SPR_SINDICATOR2, 0, ceilf(p->x - level.cam_x), ceilf(p->y - level.cam_y), 1, 1, angle, 0xFFFFFFFF, 1);
}

static void ring_bonus(bool red, bool can_heal)
{
    if (!level.has_player) return;
    Player *p = &level.player;
    if (p->isDead || p->revivalTimes >= 2 || p->character == CHARACTER_EXE) return;
    // The GML puts the sparkle at obj_netclient's x/y; the player is where it was meant to be.
    if (red && p->redRingTimer <= 0) {
        // TODO(shared): mus_mindfuck + obj_redring_screen (as SERVER_RING_COLLECTED in game.c;
        // mus_mindfuck is a music track here, audio_play would replace the level music)
        player_sound(p, SND_REDRING);
        net_quick_effect(p->x, p->y - 10, SPR_RING_SPARKLE, false, 1, 0, 0, 0.5f);
        p->redRingTimer = 60 * 10;
    } else if (!red) {
        net_quick_effect(p->x, p->y - 10, SPR_RING_SPARKLE, false, 1, 0, 0, 0.5f);
        p->rings++;
        if (p->redRingTimer >= 60) {
            int cnt = (player_time_min <= 0 && player_time_sec < 60) ? 120 : 30;
            p->redRingTimer = p->redRingTimer - cnt > 60 ? p->redRingTimer - cnt : 60;
        }
        if (p->rings >= 10 && p->hp < 100 && can_heal) {
            p->rings = 0;
            p->hp += 20;
            player_sound(p, SND_HEAL);
            NetPacket pk;
            pkt_begin(&pk, CLIENT_STATS_REPORT);
            pkt_u8(&pk, 0);
            net_send(&pk, true);
        } else {
            player_sound(p, SND_RING);
        }
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    switch (type) {
    case SERVER_RMZSLIME_STATE: {
        if (pass) return true;
        u8 t = rd_u8(r);
        if (t == 0 && reliable) {  // spawn
            u16 uid = rd_u16(r);
            float x = rd_u16(r), y = rd_u16(r);
            u8 state = rd_u8(r);
            MapObj *o = mapobj_create(OBJ_RMZSONIC, x, y);
            if (o) {
                MAPOBJ_VARS(o, Slug)->state = state;
                o->nid = uid;
            }
        } else if (t == 1 && !reliable) {  // position (net_udpprocess)
            u16 uid = rd_u16(r);
            float x = rd_u16(r), y = rd_u16(r);
            u8 state = rd_u8(r);
            MapObj *o = mapobj_by_nid(OBJ_RMZSONIC, uid);
            if (o && !o->destroyed) {
                o->x = x;
                o->y = y;
                MAPOBJ_VARS(o, Slug)->state = state;
            }
        } else if (t == 2 && reliable) {  // despawn
            // The GML reads a u8 here; the server writes the id as u16.
            u16 uid = rd_u16(r);
            MapObj *o = mapobj_by_nid(OBJ_RMZSONIC, uid);
            if (o && !o->destroyed && !MAPOBJ_VARS(o, Slug)->isDead) slug_kill(o);
        }
        return true;
    }
    case SERVER_RMZSLIME_RINGBONUS: {
        if (pass) return true;
        bool red = rd_u8(r);
        bool can_heal = rd_u8(r);
        ring_bonus(red, can_heal);
        return true;
    }
    case CLIENT_RMZSHARD_LAND: {
        // Sent with passthrough by the other clients, which the GML ignores (each client lands
        // its own copy of the shard).
        if (pass) return true;
        u16 gid = rd_u16(r);
        float x = rd_u16(r), y = rd_u16(r);
        MapObj *s = shard_by_gid(gid);
        if (s) {
            s->x = x;
            s->y = y;
        }
        return true;
    }
    case SERVER_RMZSHARD_STATE: {
        if (pass) return true;
        u8 t = rd_u8(r);
        if (t == 0 || t == 1) {
            u16 id = rd_u16(r);
            float x = rd_u16(r), y = rd_u16(r);
            MapObj *o = mapobj_create(OBJ_RAVINTMIST_SHARD, x, y);
            if (o) {
                Shard *v = MAPOBJ_VARS(o, Shard);
                v->gid = id;
                v->grv = t == 1;
                v->ground = t == 0;
            }
        } else if (t == 2) {
            u16 id = rd_u16(r);
            u16 pid = rd_u16(r);
            MapObj *s = shard_by_gid(id);
            if (s) {
                net_quick_effect(s->x, s->y, SPR_SHARD_SPARKLE, false, 1, 0, 0, 0.5f);
                mapobj_destroy(s);
            }
            // obj_player_puppet.shards (HUD) is not tracked here; see the report.
            if (pid == net.id && level.has_player) {
                rmz.shards++;
                achiev_round.rShardsCollected++;
            }
        } else if (t == 3) {
            if (!rmz.controller) return true;
            u8 cnt = rd_u8(r);
            rmz.prev_shard_count = rmz.shard_count;
            rmz.shard_count = cnt;
            if (rmz.shard_count > rmz.prev_shard_count) audio_play(SND_SHARD);
        }
        return true;
    }
    case SERVER_GAME_SPAWN_RING: {
        // Peek (game.c handles it): where obj_bigring appears, for the ring indicator.
        if (pass) return false;
        NetReader c = *r;
        bool ready = rd_u8(&c);
        if (!ready && !rmz.bigring) {
            u8 ind = rd_u8(&c);
            int n = room_instance_number(&level.room, OBJ_RINGSPAWN);
            RoomInstance *sp = n ? room_instance_find(&level.room, OBJ_RINGSPAWN, ind % n) : NULL;
            if (sp && !c.bad) {
                rmz.bigring = true;
                rmz.bigring_x = sp->x;
                rmz.bigring_y = sp->y;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

static void leave(void) { memset(&rmz, 0, sizeof rmz); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_RAVINEMIST_CONTROLLER, .create = controller_create, .draw_gui = controller_draw_gui,
      .destroy = controller_destroy },
    { .object = OBJ_RMZSONIC, .create = slug_create, .step = slug_step },
    { .object = OBJ_RAVINTMIST_SHARD, .create = shard_create, .step = shard_step },
    { .object = -1 },
};

const MapModule MAP_RAVINEMIST = {
    .room = ROOM_RAVINEMIST,
    .objects = OBJECTS,
    .step = step,
    .draw_gui = draw_gui,
    .packet = packet,
    .leave = leave,
};
