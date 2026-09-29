// room_volcanovalley: obj_vv_vase (+ obj_vv_vasepiece) with SERVER_VVVASE_STATE /
// CLIENT_VVVASE_BREAK, obj_vv_lavacolumn with SERVER_VVLCOLUMN_STATE. The barrels
// (obj_vv_barrel / obj_vv_barrel2, moved to the "Bochki" layer) only draw themselves and keep
// the default drawing. RoomCreationCode: music (game.c), parallax and scr_level_splitl (room
// export); the Android layer_clear_fx calls are omitted.
#include <math.h>
#include <strings.h>

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

#define AMY_HJUMP (ST_BALANCING + 2)    // chars/amy.c
#define SALLY_SLIDE (ST_BALANCING + 2)  // chars/sally.c

static struct {
    int lava_handle;   // the looping snd_lava of the nearest column
    int lava_level;    // its gain step (0 = silent)
} vv;

static int irnd(int a, int b) { return a + rand() % (b - a + 1); }

static bool bbox(const MapObj *o, float *l, float *t, float *r, float *b)
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

// Emitter gain of scr_audio_play_3d (same falloff as maps_sound_at)
static float gain_at(float x, float y)
{
    float dx = x - (level.cam_x + TOP_W / 2), dy = y - (level.cam_y + TOP_H / 2);
    float d = sqrtf(dx * dx + dy * dy);
    float gain = 1.0f - fminf(d, 700) / 700;
    if (d > 250) gain *= fmaxf(0, 1 - (d - 250) / 250);
    return gain;
}

// ------------------------------------------------------------------ obj_vv_vase

typedef struct {
    bool sent;  // CLIENT_VVVASE_BREAK sent (the GML resends every frame until the reply)
} Vase;

static void vase_step(MapObj *o)
{
    if (!level.has_player) return;
    if (!o->visible) {
        if (!audio_is_playing(SND_VASEBREAK)) mapobj_destroy(o);
        return;
    }
    Vase *v = MAPOBJ_VARS(o, Vase);
    if (v->sent || !mapobj_meets_player(o)) return;
    const Player *p = &level.player;
    int ch = p->character;
    bool attacking = false;
    if (p->isAttacking && (ch == CHARACTER_AMY || ch == CHARACTER_KNUX || ch == CHARACTER_EGGMAN || ch == CHARACTER_SALLY))
        attacking = true;
    if (ch == CHARACTER_AMY && p->state == AMY_HJUMP) attacking = true;
    if (p->isSpinning) attacking = true;
    if (!p->isGrounded && p->yspd > 0) attacking = true;
    if (ch == CHARACTER_SALLY && p->state == SALLY_SLIDE) attacking = true;
    if (p->revivalTimes >= 2 || ch == CHARACTER_EXE) attacking = false;
    if (!attacking) return;
    // camera shake omitted
    NetPacket pk;
    pkt_begin(&pk, CLIENT_VVVASE_BREAK);
    pkt_u8(&pk, (u8)o->nid);
    net_send(&pk, true);
    v->sent = true;
}

// ------------------------------------------------------------------ obj_vv_vasepiece

typedef struct {
    float xspd, yspd;
    bool stopped;
} Piece;

static void piece_create(MapObj *o)
{
    o->image_index = irnd(0, mapobj_frames(o));  // irandom(image_number)
    o->image_speed = 0;
    o->depth = 249;
}

static void piece_step(MapObj *o)
{
    Piece *v = MAPOBJ_VARS(o, Piece);
    if (v->stopped) return;
    o->x += v->xspd;
    o->y += v->yspd;
    float l, t, r, b;
    if (bbox(o, &l, &t, &r, &b)) {
        if (world_point(r - 1 + v->xspd, o->y - 1, WC_FLOOR)) v->xspd = 0;
        if (world_point(l + v->xspd, o->y - 1, WC_FLOOR)) v->xspd = 0;
    }
    if (world_point(o->x, o->y + 4, WC_FLOOR)) {
        v->xspd -= (v->xspd > 0 ? 1 : v->xspd < 0 ? -1 : 0) * 0.1f;
        if (fabsf(v->xspd) < 0.05f) v->xspd = 0;  // the GML float never reaches exactly 0
        v->yspd = 0;
        if (v->xspd == 0) v->stopped = true;
        return;
    }
    v->yspd += 0.32f;
    o->angle += v->xspd;
    if (o->y > (float)level.room.height + 64) mapobj_destroy(o);
}

// ------------------------------------------------------------------ obj_vv_lavacolumn

typedef struct {
    int timer, state, prevState;
} Lava;

static void lava_create(MapObj *o)
{
    o->depth = 249;
    const char *high = mapobj_prop_str(o, "isHigh");  // "True" in the room file
    if (high && (!strcasecmp(high, "true") || atof(high) != 0)) o->sprite = SPR_VV_LAVACOLUMN2;
}

static void lava_step(MapObj *o)
{
    Lava *v = MAPOBJ_VARS(o, Lava);
    if (v->timer++ >= 18) {
        if (v->state == 2 || v->state == 3) maps_sound_at(SND_LAVAAPPEAR, o->x, o->y);
        v->timer = 0;
    }
    if (!level.has_player) return;
    Player *p = &level.player;
    if (mapobj_meets_player(o)) player_hurt_snd(p, 20, (p->x > o->x ? 1 : p->x < o->x ? -1 : 0) * 4, -2, SND_LAVAHIT);
}

// ------------------------------------------------------------------ level

// Each column loops snd_lava on its emitter. Without a way to change a playing sound's gain,
// one loop follows the nearest column and restarts when its gain step changes.
static void step(void)
{
    float best = 0;
    for (int i = 0;; i++) {
        MapObj *c = mapobj_find(OBJ_VV_LAVACOLUMN, i);
        if (!c) break;
        best = fmaxf(best, gain_at(c->x, c->y));
    }
    int lvl = best < 0.02f ? 0 : 1;
    if (lvl && vv.lava_handle > 0) audio_set_gain(vv.lava_handle, best);
    if (lvl == vv.lava_level) return;
    if (vv.lava_handle > 0) audio_stop(vv.lava_handle);
    vv.lava_handle = lvl > 0 ? audio_play_ex(SND_LAVA, best, true) : -1;
    vv.lava_level = lvl;
}

static void vase_break(MapObj *o)
{
    int n = irnd(6, 8);
    for (int i = 0; i < n; i++) {
        MapObj *piece = mapobj_create(OBJ_VV_VASEPIECE, o->x, o->y);
        if (!piece) break;
        Piece *v = MAPOBJ_VARS(piece, Piece);
        v->xspd = irnd(2, 4) * (irnd(0, 2) >= 1 ? -1 : 1);
        v->yspd = irnd(-4, -2);
    }
    maps_sound_at(SND_VASEBREAK, o->x, o->y);
    o->visible = false;
}

static void vase_packet(NetReader *r)
{
    int id = rd_u8(r), kind = rd_u8(r);
    u16 pid = rd_u16(r);
    bool can_heal = rd_u8(r) != 0;
    if (!mapobj_number(OBJ_VV_VASE)) return;
    MapObj *vase = mapobj_by_nid(OBJ_VV_VASE, id);
    if (vase && vase->visible) vase_break(vase);
    if (!level.has_player) return;
    Player *p = &level.player;
    if (p->isDead || pid != net.id) return;
    if (kind >= 0 && kind <= 3) {
        player_sound(p, SND_RING);
        net_quick_effect(p->x, p->y, SPR_RING_SPARKLE, false, 1, 0, 0, 1);
        p->rings += kind + 1;
    }
    if (p->redRingTimer >= 60) {
        int cnt = (player_time_min <= 0 && player_time_sec < 60) ? 120 : 30;
        p->redRingTimer = p->redRingTimer - cnt > 60 ? p->redRingTimer - cnt : 60;
    }
    if (p->rings >= 10 && p->hp < 100 && can_heal) {
        p->rings -= 10;
        p->hp += 20;
        player_sound(p, SND_HEAL);
        NetPacket pk;
        pkt_begin(&pk, CLIENT_STATS_REPORT);
        pkt_u8(&pk, 0);
        net_send(&pk, true);
    }
}

static void lava_packet(NetReader *r)
{
    int id = rd_u8(r), state = rd_u8(r);
    float y = rd_f32(r);
    MapObj *c = mapobj_by_nid(OBJ_VV_LAVACOLUMN, id);
    if (!c) return;
    Lava *v = MAPOBJ_VARS(c, Lava);
    v->state = state;
    v->prevState = state;
    mapobj_move(c, c->x, y);
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    switch (type) {
    case SERVER_VVVASE_STATE:
        if (!pass) vase_packet(r);
        return true;
    case SERVER_VVLCOLUMN_STATE:
        if (!pass) lava_packet(r);
        return true;
    default:
        return false;
    }
}

static void leave(void)
{
    if (vv.lava_handle > 0) audio_stop(vv.lava_handle);
    memset(&vv, 0, sizeof vv);
}

static void init(void)
{
    vv.lava_handle = -1;
    vv.lava_level = 0;
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_VV_VASE, .step = vase_step },
    { .object = OBJ_VV_VASEPIECE, .create = piece_create, .step = piece_step },
    { .object = OBJ_VV_LAVACOLUMN, .create = lava_create, .step = lava_step, .dynamic = true },
    { .object = -1 },
};

const MapModule MAP_VOLCANOVALLEY = {
    .room = ROOM_VOLCANOVALLEY,
    .objects = OBJECTS,
    .init = init,
    .step = step,
    .packet = packet,
    .leave = leave,
};
