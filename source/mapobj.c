#include "mapobj.h"

#include <math.h>
#include <strings.h>

#include "level.h"
#include "maps/maps.h"
#include "sprite.h"

#define MAX_OBJS 768

static MapObj objs[MAX_OBJS];
static int obj_count;              // high-water mark
static const MapModule *module;    // the current level's module (NULL if none)
static Room *cur_room;

// ------------------------------------------------------------------ lookup

static const ObjDef *def_in(const ObjDef *table, int object)
{
    if (!table) return NULL;
    for (const ObjDef *d = table; d->object >= 0; d++)
        if (d->object == object) return d;
    return NULL;
}

// The level's own definition first, then the shared ones; parents too (children of a parent
// with a definition inherit it, like GML events).
static const ObjDef *find_def(int object)
{
    for (int obj = object, guard = 0; obj >= 0 && guard < 64; guard++) {
        const ObjDef *d = def_in(module ? module->objects : NULL, obj);
        if (!d) d = def_in(MAP_COMMON_OBJECTS, obj);
        if (d) return d;
        const ObjectInfo *o = object_info(obj);
        obj = o ? o->parent : -1;
    }
    return NULL;
}

static bool dynamic_object(int object)
{
    const ObjDef *d = find_def(object);
    return d && d->dynamic;
}

// ------------------------------------------------------------------ lifecycle

static MapObj *alloc_obj(void)
{
    for (int i = 0; i < MAX_OBJS; i++)
        if (!objs[i].used) {
            if (i >= obj_count) obj_count = i + 1;
            memset(&objs[i], 0, sizeof objs[i]);
            objs[i].used = true;
            return &objs[i];
        }
    return NULL;
}

static void init_common(MapObj *o, int object, float x, float y)
{
    const ObjectInfo *info = object_info(object);
    o->object = object;
    o->x = o->xstart = x;
    o->y = o->ystart = y;
    o->xscale = o->yscale = 1;
    o->sprite = info ? info->sprite : -1;
    o->mask = info ? info->mask : -1;
    o->image_speed = 1;
    o->image_alpha = 1;
    o->blend = 0xFFFFFFFF;
    o->visible = info ? (info->flags & OBJF_VISIBLE) != 0 : true;
    o->nid = -1;
}

void mapobj_room_start(Room *room)
{
    mapobj_room_end();
    cur_room = room;
    module = maps_module_for(room->id);
    world_is_dynamic = dynamic_object;
}

void mapobj_room_ready(Room *room)
{
    // Placed instances with a definition, in creation order
    RoomInstance *list[MAX_OBJS];
    int n = 0;
    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->type != LAYER_INSTANCES) continue;
        for (int k = 0; k < l->count; k++) {
            RoomInstance *in = &l->instances[k];
            in->mapobj = -1;
            if (find_def(in->object) && n < MAX_OBJS) list[n++] = in;
        }
    }
    for (int i = 1; i < n; i++)  // insertion sort by creation order
        for (int j = i; j > 0 && list[j]->creation < list[j - 1]->creation; j--) {
            RoomInstance *t = list[j];
            list[j] = list[j - 1];
            list[j - 1] = t;
        }
    for (int i = 0; i < n; i++) {
        RoomInstance *in = list[i];
        MapObj *o = alloc_obj();
        if (!o) break;
        init_common(o, in->object, in->x, in->y);
        o->def = find_def(in->object);
        o->src = in;
        o->world = in->world >= 0 ? world_get(in->world) : NULL;
        o->xscale = in->xscale;
        o->yscale = in->yscale;
        o->angle = in->angle;
        o->image_index = in->image_index;
        o->image_speed = in->image_speed;
        o->blend = in->colour | 0xFF000000;
        o->image_alpha = ((in->colour >> 24) & 0xFF) / 255.0f;
        o->nid = (int)mapobj_prop(o, "nid", -1);
        for (int k = 0; k < room->layer_count; k++)
            if (in >= room->layers[k].instances && in < room->layers[k].instances + room->layers[k].count)
                o->depth = o->layer_depth = room->layers[k].depth;
        in->mapobj = (int)(o - objs);
    }
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && objs[i].def->create) objs[i].def->create(&objs[i]);
    if (module && module->init) module->init();
}

void mapobj_room_end(void)
{
    if (module && module->leave) module->leave();
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && objs[i].def && objs[i].def->destroy) objs[i].def->destroy(&objs[i]);
    memset(objs, 0, sizeof objs);
    obj_count = 0;
    module = NULL;
    cur_room = NULL;
}

MapObj *mapobj_create(int object, float x, float y)
{
    MapObj *o = alloc_obj();
    if (!o) return NULL;
    init_common(o, object, x, y);
    o->def = find_def(object);
    if (o->def && o->def->create) o->def->create(o);
    return o;
}

void mapobj_destroy(MapObj *o)
{
    if (!o || o->destroyed) return;
    o->destroyed = true;
    if (o->def && o->def->destroy) o->def->destroy(o);
    if (o->world) o->world->disabled = true;
}

// ------------------------------------------------------------------ step

static void run_alarms(MapObj *o)
{
    for (int a = 0; a < 12; a++)
        if (o->alarm[a] > 0 && --o->alarm[a] == 0 && o->def && o->def->alarm) o->def->alarm(o, a);
}

void mapobj_step(void)
{
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && !objs[i].destroyed && objs[i].def && objs[i].def->begin_step) objs[i].def->begin_step(&objs[i]);
    for (int i = 0; i < obj_count; i++) {
        MapObj *o = &objs[i];
        if (!o->used || o->destroyed) continue;
        run_alarms(o);
        if (!o->destroyed && o->def && o->def->step) o->def->step(o);
    }
    for (int i = 0; i < obj_count; i++) {
        MapObj *o = &objs[i];
        if (!o->used || o->destroyed) continue;
        if (o->def && o->def->end_step) o->def->end_step(o);
        // built-in image_index advance
        if (o->sprite >= 0) {
            int frames = mapobj_frames(o);
            o->image_index += o->image_speed * sprite_frame_step(o->sprite);
            if (o->image_index >= frames) o->image_index -= frames * floorf(o->image_index / frames);
            else if (o->image_index < 0) o->image_index += frames;
        }
    }
    if (module && module->step) module->step();
    // free destroyed objects (keeping indices stable within the frame)
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && objs[i].destroyed) {
            if (objs[i].src) objs[i].src->mapobj = -1;
            objs[i].used = false;
        }
}

// ------------------------------------------------------------------ drawing

int mapobj_frames(const MapObj *o)
{
    const SpriteInfo *s = sprite_info(o->sprite);
    return s && s->frame_count > 0 ? s->frame_count : 1;
}

void mapobj_draw_self(const MapObj *o)
{
    if (o->sprite < 0) return;
    sprite_draw(o->sprite, o->image_index, o->x - level.cam_x, o->y - level.cam_y, o->xscale, o->yscale, o->angle,
                o->blend, o->image_alpha);
}

static void draw_obj(MapObj *o)
{
    if (!o->used || o->destroyed || !o->visible) return;
    float dx = 0, dy = 0;
    if (level.gui_clamp)
        level_clamp_offset(o->sprite, o->x - level.cam_x, o->y - level.cam_y, o->xscale, o->yscale, &dx, &dy);
    o->x += dx;
    o->y += dy;
    if (o->def && o->def->draw) o->def->draw(o);
    else mapobj_draw_self(o);
    o->x -= dx;
    o->y -= dy;
}

bool mapobj_owns(const RoomInstance *in) { return in->mapobj >= 0 && objs[in->mapobj].used; }

void mapobj_draw_instance(const RoomInstance *in)
{
    if (!mapobj_owns(in)) return;
    MapObj *o = &objs[in->mapobj];
    if (o->depth == o->layer_depth) draw_obj(o);
}

void mapobj_draw_moved(int lo, int hi)
{
    for (int i = 0; i < obj_count; i++) {
        MapObj *o = &objs[i];
        if (o->used && o->src && o->depth != o->layer_depth && o->depth >= lo && o->depth < hi) draw_obj(o);
    }
}

void mapobj_draw_spawned(bool front)
{
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && !objs[i].src && (objs[i].depth < 0) == front) draw_obj(&objs[i]);
}

void mapobj_draw_front(void)
{
    if (module && module->draw_front) module->draw_front();
}

void mapobj_draw_gui(void)
{
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && !objs[i].destroyed && objs[i].visible && objs[i].def && objs[i].def->draw_gui)
            objs[i].def->draw_gui(&objs[i]);
    if (module && module->draw_gui) module->draw_gui();
}

bool mapobj_packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    if (module && module->packet && module->packet(type, pass, r, reliable)) return true;
    return maps_common_packet(type, pass, r, reliable);
}

// ------------------------------------------------------------------ helpers

MapObj *mapobj_find(int object, int n)
{
    // Placed instances were created in creation order and runtime ones after them, so slot
    // order is creation order.
    for (int i = 0; i < obj_count; i++) {
        MapObj *o = &objs[i];
        if (!o->used || o->destroyed || !object_is_child_of(o->object, object)) continue;
        if (n-- == 0) return o;
    }
    return NULL;
}

int mapobj_number(int object)
{
    int n = 0;
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && !objs[i].destroyed && object_is_child_of(objs[i].object, object)) n++;
    return n;
}

MapObj *mapobj_by_nid(int object, int nid)
{
    for (int i = 0; i < obj_count; i++)
        if (objs[i].used && !objs[i].destroyed && objs[i].nid == nid && object_is_child_of(objs[i].object, object))
            return &objs[i];
    return NULL;
}

void mapobj_move(MapObj *o, float x, float y)
{
    o->x = x;
    o->y = y;
    if (o->world) world_move(o->world, x, y);
}

typedef struct { float l, t, r, b; } Box;

static bool obj_box(const MapObj *o, float x, float y, Box *b)
{
    int spr = o->mask >= 0 ? o->mask : o->sprite;
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return false;
    float l = x + (s->bbox_left - s->xorigin) * o->xscale, r = x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
    float t = y + (s->bbox_top - s->yorigin) * o->yscale, bt = y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    b->l = fminf(l, r);
    b->r = fmaxf(l, r);
    b->t = fminf(t, bt);
    b->b = fmaxf(t, bt);
    return true;
}

bool mapobj_meets_player_at(const MapObj *o, float x, float y)
{
    if (!level.has_player) return false;
    const Player *p = &level.player;
    Box a, b;
    const SpriteInfo *s = sprite_info(p->sprite);
    if (!obj_box(o, x, y, &a) || !s) return false;
    float px = floorf(p->x), py = floorf(p->y);
    float l = px + (s->bbox_left - s->xorigin) * p->image_xscale, r = px + (s->bbox_right + 1 - s->xorigin) * p->image_xscale;
    b.l = fminf(l, r);
    b.r = fmaxf(l, r);
    b.t = py + s->bbox_top - s->yorigin;
    b.b = py + s->bbox_bottom + 1 - s->yorigin;
    return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
}

bool mapobj_meets_player(const MapObj *o) { return mapobj_meets_player_at(o, o->x, o->y); }

bool mapobj_bbox(const MapObj *o, float *l, float *t, float *r, float *b)
{
    Box bx;
    if (!obj_box(o, o->x, o->y, &bx)) return false;
    *l = bx.l;
    *t = bx.t;
    *r = bx.r;
    *b = bx.b;
    return true;
}

const char *mapobj_prop_str(const MapObj *o, const char *name)
{
    if (!o->src) return NULL;
    for (int i = o->src->prop_count - 1; i >= 0; i--)  // creation code comes after the properties
        if (!strcmp(o->src->props[i].name, name)) return o->src->props[i].value;
    return NULL;
}

float mapobj_prop(const MapObj *o, const char *name, float def)
{
    const char *v = mapobj_prop_str(o, name);
    if (!v || !*v) return def;
    if (!strcasecmp(v, "true")) return 1;
    if (!strcasecmp(v, "false")) return 0;
    return (float)atof(v);
}

RoomLayer *layer_find(const char *name)
{
    if (!cur_room) return NULL;
    for (int i = 0; i < cur_room->layer_count; i++)
        if (cur_room->layers[i].name && !strcmp(cur_room->layers[i].name, name)) return &cur_room->layers[i];
    return NULL;
}

void layer_set_pos(RoomLayer *l, float x, float y)
{
    if (!l) return;
    l->manual_pos = true;
    l->bg_x = x;
    l->bg_y = y;
}
