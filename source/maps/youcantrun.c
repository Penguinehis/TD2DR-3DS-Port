// room_youcantrun: obj_ycr_smokearea (poison gas areas), obj_ycr_smoke (the gas clouds),
// SERVER_YCRSMOKE_READY / SERVER_YCRSMOKE_STATE. The moving spikes are in common.c.
#include <math.h>

#include "../audio.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

static float current_time_ms(void) { return level.time * 1000.0f / 60; }
static float rnd(float a, float b) { return a + (b - a) * (rand() % 10000) / 10000.0f; }

// Axis-aligned box of an object's sprite in the room (bbox_left/top/right+1/bottom+1).
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

// ------------------------------------------------------------------ obj_ycr_smokearea

typedef struct {
    int timer;
    bool overlaps, activated;
} SmokeArea;

static void area_create(MapObj *o)
{
    SmokeArea *v = MAPOBJ_VARS(o, SmokeArea);
    v->timer = 0;
    v->overlaps = v->activated = false;
    if (o->nid < 0) o->nid = 0;
}

static void area_step(MapObj *o)
{
    SmokeArea *v = MAPOBJ_VARS(o, SmokeArea);
    if (v->overlaps && level.has_player) {
        Player *p = &level.player;
        if (v->timer >= 60) {
            if (p->rings > 0) {
                player_sound(p, SND_RINGABSORB);
                p->rings--;
            } else {
                player_hurt(p, 20, -p->image_xscale * 4, -6);
            }
            v->timer = 0;
        }
        // The GML also plays mus_mindfuck (looped) and creates obj_redring_screen here: the red
        // ring screen belongs to the shared red ring code (game.c TODO(M8)), which keys on
        // redRingTimer like the red ring pickup.
        if (v->timer >= 0 && p->redRingTimer <= 0) p->redRingTimer = 60 * 3;
        v->timer++;
    } else {
        v->timer = -60;
    }
    v->overlaps = false;
}

// ------------------------------------------------------------------ obj_ycr_smoke

typedef struct {
    bool mDestroy;
    float xP, xOff;
    MapObj *area;
} Smoke;

static void smoke_create(MapObj *o)
{
    Smoke *v = MAPOBJ_VARS(o, Smoke);
    v->mDestroy = false;
    v->xP = o->x;
    v->xOff = rnd(-20, 20);
    v->area = NULL;
    o->depth = -21;
    o->image_alpha = 0;
    o->image_index = rand() % (mapobj_frames(o) + 1);
}

static void smoke_step(MapObj *o)
{
    Smoke *v = MAPOBJ_VARS(o, Smoke);
    o->x = v->xP + sinf(current_time_ms() / 300 + v->xOff) * 4;
    if (v->mDestroy) {
        o->image_alpha -= 0.01f;
        if (o->image_alpha <= 0) mapobj_destroy(o);
        return;
    }
    if (o->image_alpha < 1) o->image_alpha += 0.0025f;
    if (!level.has_player) return;
    const Player *p = &level.player;
    if (p->isDead || p->character == CHARACTER_EXE || p->revivalTimes >= 2) return;
    if (v->area && mapobj_meets_player(o)) MAPOBJ_VARS(v->area, SmokeArea)->overlaps = true;
}

static void smoke_draw(MapObj *o)
{
    // Off-screen clouds are skipped (a smoke sprite is 228x178).
    float x = o->x - level.cam_x, y = o->y - level.cam_y;
    if (x > TOP_W || y > TOP_H || x < -240 || y < -190) return;
    mapobj_draw_self(o);
}

// ------------------------------------------------------------------ packets

static void spawn_smoke(MapObj *a)
{
    float l, t, r, b;
    if (!box(a, &l, &t, &r, &b)) return;
    maps_sound_at(SND_SMOKE, l + (r - l) / 2, t + (b - t) / 2);
    for (float i = 0; i < r - l; i += 228)
        for (float j = 0; j < b - t; j += 178) {
            MapObj *s = mapobj_create(OBJ_YCR_SMOKE, l + i, t + j);
            if (s) MAPOBJ_VARS(s, Smoke)->area = a;
        }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    switch (type) {
    case SERVER_YCRSMOKE_READY: {
        if (pass) return true;
        int nid = rd_u8(r);
        MapObj *a = mapobj_by_nid(OBJ_YCR_SMOKEAREA, nid);
        if (a) MAPOBJ_VARS(a, SmokeArea)->activated = true;
        return true;
    }
    case SERVER_YCRSMOKE_STATE: {
        if (pass) return true;
        bool activated = rd_u8(r);
        int nid = rd_u8(r);
        if (!mapobj_number(OBJ_YCR_SMOKEAREA)) return true;
        if (activated) {
            MapObj *a = mapobj_by_nid(OBJ_YCR_SMOKEAREA, nid);
            if (a) spawn_smoke(a);
        } else {
            for (int i = 0;; i++) {
                MapObj *a = mapobj_find(OBJ_YCR_SMOKEAREA, i);
                if (!a) break;
                MAPOBJ_VARS(a, SmokeArea)->activated = false;
            }
            for (int i = 0;; i++) {
                MapObj *s = mapobj_find(OBJ_YCR_SMOKE, i);
                if (!s) break;
                MAPOBJ_VARS(s, Smoke)->mDestroy = true;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

static const ObjDef OBJECTS[] = {
    { .object = OBJ_YCR_SMOKEAREA, .create = area_create, .step = area_step },
    { .object = OBJ_YCR_SMOKE, .create = smoke_create, .step = smoke_step, .draw = smoke_draw },
    // Placed sound emitters must be map objects for the moving-spike sound (common.c)
    { .object = OBJ_SOUNDEMITTER },
    { .object = -1 },
};

const MapModule MAP_YOUCANTRUN = {
    .room = ROOM_YOUCANTRUN,
    .objects = OBJECTS,
    .packet = packet,
};
