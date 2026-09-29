// room_dartower: obj_darktower_darkness (the screen goes dark for a second every 10 s),
// obj_darktower_fog, obj_darktower_jumpscare, obj_darktower_ball (SERVER_DTBALL_STATE),
// obj_darktower_tailsdoll (SERVER_DTTAILSDOLL_STATE), the stalactites obj_darktower_sisi
// (SERVER_DTASS_STATE / CLIENT_DTASS_ACTIVATE) with their dust (obj_darktower_kapla) and
// shards (obj_darktower_sisipart).
#include <math.h>

#include "../audio.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

#define GUI_SX (TOP_W / 480.0f)   // the GML GUI is 480x270
#define GUI_SY (TOP_H / 270.0f)
#define MAX_KAPLA 160
#define MAX_PARTS 64

typedef struct {
    float x, y, yspd, frame;
    bool grounded;
} Kapla;

typedef struct {
    float x, y, xspd, yspd, dir;
    int frame;
} Part;

static struct {
    bool darkness, fog;        // the objects exist
    int dark_timer;            // obj_darktower_darkness._timer / _alpha / _fade
    float dark_alpha;
    bool dark_fade;
    Kapla kapla[MAX_KAPLA];
    int kapla_count;
    Part parts[MAX_PARTS];
    int part_count;
    MapObj *dust;              // runtime object drawing the dust behind the player
} dt;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static int irand(int a, int b) { return a + rand() % (b - a + 1); }

// net_sound_emit alone (the GML plays one sound locally and emits another)
static void net_sound_emit(int snd)
{
    const Player *p = &level.player;
    if (p->character == CHARACTER_EXE && p->invisTimer > 0 && snd != SND_SPRING) return;
    for (int i = 0; i < NET_SNDS_COUNT; i++)
        if (NET_SNDS[i] == snd) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_SOUND_EMIT);
            pk.buf[0] = 1;
            pkt_u16(&pk, net.id);
            pkt_u8(&pk, (u8)i);
            pkt_u8(&pk, 0);
            net_send(&pk, true);
            return;
        }
}

// ------------------------------------------------------------------ darkness / fog / jumpscare

static void darkness_create(MapObj *o)
{
    o->depth = -100;
    dt.darkness = true;
    dt.dark_timer = 0;
    dt.dark_alpha = 0;
    dt.dark_fade = false;
}

// Draw_64 timer (advanced once per frame)
static void darkness_step(MapObj *o)
{
    (void)o;
    dt.dark_timer++;
    if (!dt.dark_fade && dt.dark_timer > 60 * 10) {
        dt.dark_fade = true;
        dt.dark_timer = 0;
    }
    if (dt.dark_fade && dt.dark_timer > 60 * 1) {
        dt.dark_fade = false;
        dt.dark_timer = 0;
    }
    if (dt.dark_fade) {
        if (dt.dark_alpha < 1) dt.dark_alpha += 0.016f;
    } else if (dt.dark_alpha > 0) {
        dt.dark_alpha -= 0.016f;
    }
}

static void fog_create(MapObj *o)
{
    (void)o;
    dt.fog = true;
}

static void no_draw(MapObj *o) { (void)o; }

static void jumpscare_create(MapObj *o)
{
    o->depth = -105;
    o->visible = false;
}

static void jumpscare_alarm(MapObj *o, int n)
{
    // camera_set_view_size(480, 270): the zoom flicker is not ported
    if (n == 0) o->visible = false;
}

// ------------------------------------------------------------------ obj_darktower_ball

static void ball_step(MapObj *o)
{
    if (!level.has_player || !mapobj_meets_player(o)) return;
    Player *p = &level.player;
    player_hurt(p, 20, -p->image_xscale * 4, -6);
}

// ------------------------------------------------------------------ obj_darktower_tailsdoll

typedef struct {
    float pX, pY;
} Doll;

static void doll_create(MapObj *o)
{
    o->image_speed = 0;
    MAPOBJ_VARS(o, Doll)->pX = 0;
    MAPOBJ_VARS(o, Doll)->pY = 0;
}

static void doll_end_step(MapObj *o)
{
    float d = o->x - MAPOBJ_VARS(o, Doll)->pX;
    if (d != 0) o->xscale = d > 0 ? 1 : -1;
}

static MapObj *doll_at(float x, float y)
{
    MapObj *d = mapobj_find(OBJ_DARKTOWER_TAILSDOLL, 0);
    if (!d) d = mapobj_create(OBJ_DARKTOWER_TAILSDOLL, x, y);
    return d;
}

// ------------------------------------------------------------------ obj_darktower_sisi

typedef struct {
    bool damage, dying;
} Sisi;

static void sisi_create(MapObj *o)
{
    o->depth = 101;
    o->image_alpha = 0;
    o->nid = 0;
    Sisi *s = MAPOBJ_VARS(o, Sisi);
    s->damage = false;
    s->dying = false;
}

static void spawn_part(float x, float y, float xspd, float dir)
{
    if (dt.part_count >= MAX_PARTS) return;
    Part *p = &dt.parts[dt.part_count++];
    p->x = x;
    p->y = y;
    p->xspd = xspd;
    static const int ys[] = { 4, 5, 6, 7 };
    p->yspd = -(float)ys[rand() % 4];
    p->dir = dir;
    const SpriteInfo *s = sprite_info(SPR_DARKTOWER_STALOKTITI2);
    p->frame = rand() % ((s && s->frame_count > 0 ? s->frame_count : 1) + 1);  // irandom_range(0, image_number)
}

// _destroy
static void sisi_break(MapObj *o)
{
    Sisi *s = MAPOBJ_VARS(o, Sisi);
    if (s->dying) return;
    maps_sound_at(SND_SNOWBALL_BREAK, o->x, o->y);
    for (int i = 0; i < 2; i++)
        spawn_part(o->x + 40 + irand(-4, 4), o->y + irand(-10, 10), 1 - i / 10.0f, -1);
    for (int i = 0; i < 2; i++)
        spawn_part(o->x + 40 + irand(-4, 4), o->y + irand(-10, 10), 1 + i / 10.0f, 1);
    o->alarm[0] = 60;
    o->visible = false;
    s->dying = true;
}

static void sisi_alarm(MapObj *o, int n)
{
    if (n == 0) mapobj_destroy(o);
}

static void spawn_kapla(float x, float y)
{
    if (dt.kapla_count >= MAX_KAPLA) return;
    Kapla *k = &dt.kapla[dt.kapla_count++];
    static const float sp[] = { 1, 0.5f, 0.8f, 0.6f };
    k->x = x;
    k->y = y;
    k->yspd = sp[rand() % 4] * 5;
    k->grounded = false;
    k->frame = 0;
}

static void sisi_end_step(MapObj *o)
{
    Sisi *s = MAPOBJ_VARS(o, Sisi);
    if (o->image_alpha < 1) o->image_alpha += 0.016f;

    // if(current_time % 8 == 0): about one frame in eight. Only near the view (the dust is
    // not visible elsewhere and the Old 3DS has little CPU).
    float cx = level.cam_x + TOP_W / 2, cy = level.cam_y + TOP_H / 2;
    if (rand() % 8 == 0 && fabsf(o->x + 40 - cx) < TOP_W && fabsf(o->y - cy) < TOP_H + 200)
        spawn_kapla(o->x + 40 + irand(-14, 14), o->y + 40 + irand(-10, 10));

    if (world_rect(o->x + 19, o->y + 18, o->x + 59, o->y + 20, WC_FLOOR)) {
        // scr_camera_shake: omitted. The GML sends CLIENT_DTASS_ACTIVATE on every frame until
        // the instance is gone (the server ignores the repeats); it is sent once here.
        if (!s->dying) {
            NetPacket pk;
            pkt_begin(&pk, CLIENT_DTASS_ACTIVATE);
            pkt_u8(&pk, (u8)o->nid);
            net_send(&pk, true);
        }
        sisi_break(o);
    }

    if (s->damage && level.has_player && o->visible && mapobj_meets_player(o)) {
        Player *p = &level.player;
        player_hurt(p, 20, -p->image_xscale * 4, -6);
    }
}

// ------------------------------------------------------------------ obj_darktower_kapla / sisipart

static void particles_step(void)
{
    const SpriteInfo *ks = sprite_info(SPR_DARKTOWER_STALOKTITI1);
    int kframes = ks && ks->frame_count > 0 ? ks->frame_count : 1;
    float kstep = sprite_frame_step(SPR_DARKTOWER_STALOKTITI1);
    for (int i = 0; i < dt.kapla_count;) {
        Kapla *k = &dt.kapla[i];
        if (!k->grounded) {
            k->y += k->yspd;
            // while(place_meeting(x, y, obj_floor_parent)) { y = floor(y) - 1; grounded = true; }
            for (int guard = 0; guard < 64 && world_rect(k->x - 4, k->y - 3, k->x + 5, k->y + 4, WC_FLOOR); guard++) {
                k->y = floorf(k->y) - 1;
                k->grounded = true;
            }
            if (!k->grounded) k->frame = 0;
        }
        bool dead = k->frame >= kframes - 1 || k->y > level.room.height + 64;
        if (k->grounded) k->frame += kstep;
        if (dead) *k = dt.kapla[--dt.kapla_count];
        else i++;
    }

    for (int i = 0; i < dt.part_count;) {
        Part *p = &dt.parts[i];
        p->yspd += 0.2f;
        p->xspd += p->dir * .05f;
        p->x += p->xspd;
        p->y += p->yspd;
        bool dead = p->y > level.room.height + 64;
        // instance_place(x, y, obj_floor_parent): only a visible floor stops a shard
        WorldInst *w = world_rect(p->x - 9, p->y - 8, p->x + 10, p->y + 9, WC_FLOOR);
        if (w && w->src) {
            const ObjectInfo *info = object_info(w->object);
            if (info && (info->flags & OBJF_VISIBLE)) dead = true;
        }
        if (dead) *p = dt.parts[--dt.part_count];
        else i++;
    }
}

// Dust at the stalactites' depth (101: behind the player)
static void dust_draw(MapObj *o)
{
    (void)o;
    for (int i = 0; i < dt.kapla_count; i++) {
        float x = dt.kapla[i].x - level.cam_x, y = dt.kapla[i].y - level.cam_y;
        if (x < -8 || y < -8 || x > TOP_W + 8 || y > TOP_H + 8) continue;
        sprite_draw(SPR_DARKTOWER_STALOKTITI1, dt.kapla[i].frame, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

// ------------------------------------------------------------------ level

static void init(void)
{
    // Holder for the dust particles (drawn with the runtime objects behind the player)
    dt.dust = mapobj_create(OBJ_DARKTOWER_KAPLA, 0, 0);
    if (dt.dust) {
        dt.dust->depth = 101;
        dt.dust->visible = true;
    }
}

static void step(void) { particles_step(); }

// obj_darktower_sisipart: depth -20 (in front of the player)
static void draw_front(void)
{
    for (int i = 0; i < dt.part_count; i++) {
        float x = dt.parts[i].x - level.cam_x, y = dt.parts[i].y - level.cam_y;
        if (x < -20 || y < -20 || x > TOP_W + 20 || y > TOP_H + 20) continue;
        sprite_draw(SPR_DARKTOWER_STALOKTITI2, dt.parts[i].frame, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

static void draw_gui(void)
{
    float t = current_time_ms();
    // obj_darktower_fog (depth 0)
    if (dt.fog)
        sprite_draw(SPR_DARKTOWER_FOG, 0, (-495 / 2.0f + sinf(t / 4000) * 200) * GUI_SX, 0, GUI_SX, GUI_SY, 0,
                    0xFFFFFFFF, 1);
    // obj_darktower_darkness (depth -100): spr_black over the screen
    if (dt.darkness && dt.dark_alpha > 0)
        C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, fminf(dt.dark_alpha, 1)));
    // obj_darktower_jumpscare (depth -105), only while visible
    MapObj *js = mapobj_find(OBJ_DARKTOWER_JUMPSCARE, 0);
    if (js && js->visible) {
        C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32(90, 0, 0, 90));  // spr_redring_fore at 0.4, flat
        if ((int)t % irand(20, 24) <= 10) {
            float xx = -4 + (rand() % 801) / 100.0f;  // random_range(-4, 4)
            // The camera zoom flicker (camera_set_view_pos / size) is not ported.
            sprite_draw(SPR_DARKTOWER_JUMPSCARE, xx, xx * GUI_SX, 0, GUI_SX, GUI_SY, 0, 0xFFFFFFFF, 1);
        }
    }
}

static MapObj *sisi_by_nid(int nid)
{
    for (int i = 0;; i++) {
        MapObj *s = mapobj_find(OBJ_DARKTOWER_SISI, i);
        if (!s || s->nid == nid) return s;
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    switch (type) {
    case SERVER_DTBALL_STATE: {
        if (pass) return true;
        float state = rd_f32(r);
        for (int i = 0;; i++) {
            MapObj *b = mapobj_find(OBJ_DARKTOWER_BALL, i);
            if (!b) break;
            b->y = b->ystart + state * mapobj_prop(b, "dir", 1) * mapobj_prop(b, "dist", 100);
        }
        return true;
    }
    case SERVER_DTTAILSDOLL_STATE: {
        if (pass) return true;
        if (!reliable) {  // net_udpprocess: position update
            float x = rd_u16(r), y = rd_u16(r);
            int tar = rd_u8(r);
            MapObj *d = doll_at(x, y);
            if (!d) return true;
            MAPOBJ_VARS(d, Doll)->pX = d->x;
            MAPOBJ_VARS(d, Doll)->pY = d->y;
            d->x = x;
            d->y = y;
            d->image_index = tar;
            return true;
        }
        int t = rd_u8(r);
        if (t == 0) {
            float x = rd_u16(r), y = rd_u16(r);
            int tar = rd_u8(r);
            MapObj *d = doll_at(x, y);
            if (!d) return true;
            d->x = x;
            d->y = y;
            d->image_index = tar;
        } else if (t == 1 && level.has_player) {
            Player *p = &level.player;
            if (p->hp <= 0) return true;
            audio_play(SND_TAILSBALL_JUMPSCARE);
            net_sound_emit(SND_TAILSBALL_JUMPSCARE2);
            MapObj *js = mapobj_find(OBJ_DARKTOWER_JUMPSCARE, 0);
            if (js) {
                js->visible = true;
                js->alarm[0] = 60;
            }
            player_slow(p, 3);
        } else if (t == 2) {
            if (mapobj_number(OBJ_DARKTOWER_TAILSDOLL)) audio_play(SND_TAILSBALL);
        } else if (t == 3) {
            audio_play(SND_TAILSBALL_CHASE);
        }
        return true;
    }
    case SERVER_DTASS_STATE: {
        if (pass) return true;
        if (!reliable) {  // net_udpprocess: a falling stalactite
            int id = rd_u8(r);
            float x = rd_u16(r), y = rd_u16(r);
            MapObj *s = sisi_by_nid(id);
            if (s) {
                MAPOBJ_VARS(s, Sisi)->damage = true;
                s->x = x;
                s->y = y;
            }
            return true;
        }
        int t = rd_u8(r);
        int id = rd_u8(r);
        if (t == 0) {
            float x = rd_u16(r), y = rd_u16(r);
            MapObj *s = mapobj_create(OBJ_DARKTOWER_SISI, x, y);
            if (s) {
                s->nid = id;
                s->visible = true;
            }
        } else if (t == 1) {
            MapObj *s = sisi_by_nid(id);
            if (s) sisi_break(s);
        } else {
            MapObj *s = sisi_by_nid(id);
            if (s) maps_sound_at(SND_SISI_NOTICE, s->x, s->y);
        }
        return true;
    }
    default:
        return false;
    }
}

static void leave(void) { memset(&dt, 0, sizeof dt); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_DARKTOWER_DARKNESS, .create = darkness_create, .step = darkness_step, .draw = no_draw },
    { .object = OBJ_DARKTOWER_FOG, .create = fog_create, .draw = no_draw },
    { .object = OBJ_DARKTOWER_JUMPSCARE, .create = jumpscare_create, .draw = no_draw, .alarm = jumpscare_alarm },
    { .object = OBJ_DARKTOWER_BALL, .step = ball_step },
    { .object = OBJ_DARKTOWER_TAILSDOLL, .create = doll_create, .end_step = doll_end_step },
    { .object = OBJ_DARKTOWER_SISI, .create = sisi_create, .end_step = sisi_end_step, .alarm = sisi_alarm },
    { .object = OBJ_DARKTOWER_KAPLA, .draw = dust_draw },
    { .object = -1 },
};

const MapModule MAP_DARKTOWER = {
    .room = ROOM_DARTOWER,
    .objects = OBJECTS,
    .init = init,
    .step = step,
    .draw_front = draw_front,
    .draw_gui = draw_gui,
    .packet = packet,
    .leave = leave,
};
