// room_deserttown: obj_deserttown_i (inside the buildings, marked by obj_deserttown_trigger:
// the front tiles at depth -400 and the "TilesBalls" fog fade out, the interior tiles at depth
// -300 fade in) and the frozen frames of obj_deserttown_tiles / obj_deserttown_misc.
#include <math.h>

#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

static struct {
    float value;
    bool coll;
    RoomLayer *fog;           // "TilesBalls" (depth -400)
    RoomLayer *inner;         // scr_level_split(spr_deserttown_tiles3, -300)
    RoomLayer *outer;         // scr_level_split(spr_deserttown_tiles2, -400)
} dt;

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

// position_meeting(x, y, obj_deserttown_trigger)
static bool in_trigger(float x, float y)
{
    for (int i = 0;; i++) {
        MapObj *o = mapobj_find(OBJ_DESERTTOWN_TRIGGER, i);
        if (!o) return false;
        float l, t, r, b;
        if (box(o, &l, &t, &r, &b) && x >= l && x < r && y >= t && y < b) return true;
    }
}

// image_speed = 0 (Create) for placed instances drawn by level.c
static void freeze_instances(int object)
{
    for (int i = 0; i < level.room.layer_count; i++) {
        RoomLayer *l = &level.room.layers[i];
        if (l->type != LAYER_INSTANCES) continue;
        for (int k = 0; k < l->count; k++)
            if (l->instances[k].object == object) l->instances[k].image_speed = 0;
    }
}

// ------------------------------------------------------------------ obj_deserttown_i

static void i_create(MapObj *o)
{
    (void)o;
    dt.value = 0;
    dt.coll = false;
    dt.fog = layer_find("TilesBalls");
    // The split layers fade, which level.c cannot do: they are drawn here (draw_front).
    dt.inner = layer_find("spr_deserttown_tiles3");
    dt.outer = layer_find("spr_deserttown_tiles2");
    if (dt.fog) dt.fog->visible = false;
    if (dt.inner) dt.inner->visible = false;
    if (dt.outer) dt.outer->visible = false;
}

static void i_step(MapObj *o)
{
    (void)o;
    dt.coll = false;
    const Player *p = &level.player;
    // TODO(shared): the GML also takes the camera branch when obj_netclient.gameEnds
    if (level.has_player && (p->hp > 0 || p->revivalTimes >= 2)) {
        if (in_trigger(p->x, p->y)) dt.coll = true;
    } else if (in_trigger(level.cam_x + TOP_W / 2, level.cam_y + TOP_H / 2)) {
        dt.coll = true;
    }
    if (dt.coll) {
        if (dt.value < 1) dt.value += 0.1f;
    } else {
        if (dt.value > 0) dt.value -= 0.1f;
    }
}

// ------------------------------------------------------------------ level

static void draw_art(const RoomLayer *l, float alpha)
{
    if (!l || alpha <= 0) return;
    const SpriteInfo *s = sprite_info(l->art_sprite);
    if (!s) return;
    for (int k = 0; k < s->frame_count; k++) {
        float x = k * (float)s->width - level.cam_x, y = l->art_yoff - level.cam_y;
        if (x > TOP_W || x + s->width < 0 || y > TOP_H || y + s->height < 0) continue;
        sprite_draw(l->art_sprite, k, x + s->xorigin, y + s->yorigin, 1, 1, 0, 0xFFFFFFFF, alpha);
    }
}

static void draw_fog(float alpha)
{
    const RoomLayer *l = dt.fog;
    if (!l || alpha <= 0) return;
    // As level.c draws a background layer (not in the parallax table), with the alpha of
    // layer_background_alpha(_c, 1 - value)
    float lx = l->bg_x + level.time * l->hspeed, ly = l->bg_y + level.time * l->vspeed;
    float frame = level.time * sprite_frame_step(l->bg_sprite);
    u32 a = (u32)(fminf(alpha, 1) * 255);
    sprite_draw_tiled(l->bg_sprite, frame, lx - level.cam_x, ly - level.cam_y, l->htiled, l->vtiled, TOP_W, TOP_H,
                      (l->bg_colour & 0xFFFFFF) | (a << 24));
}

// Depth order: tiles3 (-300), then TilesBalls and tiles2 (-400)
static void draw_front(void)
{
    float v = fminf(fmaxf(dt.value, 0), 1);
    draw_art(dt.inner, v);
    draw_fog(1 - v);
    draw_art(dt.outer, 1 - v);
}

static void init(void)
{
    freeze_instances(OBJ_DESERTTOWN_TILES);
    freeze_instances(OBJ_DESERTTOWN_MISC);
    freeze_instances(OBJ_DESERTTOWN);
}

static void leave(void) { memset(&dt, 0, sizeof dt); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_DESERTTOWN_I, .create = i_create, .step = i_step },
    { .object = OBJ_DESERTTOWN_TRIGGER },  // sampled with position_meeting
    { .object = -1 },
};

const MapModule MAP_DESERTTOWN = {
    .room = ROOM_DESERTTOWN,
    .objects = OBJECTS,
    .init = init,
    .draw_front = draw_front,
    .leave = leave,
};
