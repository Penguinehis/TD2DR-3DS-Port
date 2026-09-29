// Room and object data (written by tools/export_rooms.py).
//
// objects.bin: "OBJ1" u16 count, then ObjectInfo[count]
//
// <room>.bin:  "ROM1" u32 width u32 height u16 view_w u16 view_h u16 layer_count, then layers:
//   u8 type, s32 depth, u8 visible, str name, then by type:
//   BACKGROUND: s16 sprite, s32 x, s32 y, u8 htiled, u8 vtiled, u8 stretch, u32 colour, f32 hspeed, f32 vspeed,
//               u8 parallax, f32 px, f32 py, f32 pyoff, u8 has_pyoff
//   LEVELART:   (name = sprite name) s16 sprite, f32 yoff
//   INSTANCES:  u16 n, n x { s16 obj, f32 x, y, xscale, yscale, angle, image_speed, u32 colour,
//                            s16 image_index, u8 flags, str name, u16 creation, u8 nprops, nprops x { str key, str value } }
//   ASSETS:     u16 n, n x { s16 sprite, f32 x, y, xscale, yscale, angle, head, anim_speed, u32 colour }
#include "room.h"

static u8 *obj_blob;
static const ObjectInfo *objects;
static int object_count;

void objects_init(void)
{
    size_t size;
    obj_blob = read_file("romfs:/data/objects.bin", &size);
    if (!obj_blob || memcmp(obj_blob, "OBJ1", 4))
        fatal("objects.bin missing.\nRerun tools/export_rooms.py");
    const u8 *p = obj_blob + 4;
    object_count = TAKE(&p, u16);
    objects = (const ObjectInfo *)p;
    if (object_count != OBJ_COUNT)
        fatal("objects.bin has %d objects, build expects %d.", object_count, OBJ_COUNT);
}

const ObjectInfo *object_info(int obj)
{
    return (obj >= 0 && obj < object_count) ? &objects[obj] : NULL;
}

bool object_is_child_of(int obj, int ancestor)
{
    for (int guard = 0; obj >= 0 && guard < 64; guard++) {
        if (obj == ancestor) return true;
        obj = objects[obj].parent;
    }
    return false;
}

static int by_depth_desc(const void *a, const void *b)
{
    const RoomLayer *la = a, *lb = b;
    return (la->depth < lb->depth) - (la->depth > lb->depth);
}

bool room_load(Room *room, int id)
{
    memset(room, 0, sizeof *room);
    if (id < 0 || id >= RM_COUNT) return false;
    size_t size;
    room->blob = read_file(ROOM_FILES[id], &size);
    if (!room->blob || memcmp(room->blob, "ROM1", 4)) {
        free(room->blob);
        room->blob = NULL;
        return false;
    }
    const u8 *p = room->blob + 4;
    room->id = id;
    room->width = TAKE(&p, u32);
    room->height = TAKE(&p, u32);
    room->view_w = TAKE(&p, u16);
    room->view_h = TAKE(&p, u16);
    room->layer_count = TAKE(&p, u16);
    room->layers = calloc(room->layer_count, sizeof(RoomLayer));

    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        l->type = TAKE(&p, u8);
        l->depth = TAKE(&p, s32);
        l->visible = TAKE(&p, u8);
        l->name = take_str(&p);
        switch (l->type) {
        case LAYER_BACKGROUND:
            l->bg_sprite = TAKE(&p, s16);
            l->bg_x = TAKE(&p, s32);
            l->bg_y = TAKE(&p, s32);
            l->htiled = TAKE(&p, u8);
            l->vtiled = TAKE(&p, u8);
            l->stretch = TAKE(&p, u8);
            l->bg_colour = TAKE(&p, u32);
            l->hspeed = TAKE(&p, float);
            l->vspeed = TAKE(&p, float);
            l->parallax = TAKE(&p, u8);
            l->px = TAKE(&p, float);
            l->py = TAKE(&p, float);
            l->pyoff = TAKE(&p, float);
            (void)TAKE(&p, u8);
            break;
        case LAYER_LEVELART:
            l->art_sprite = TAKE(&p, s16);
            l->art_yoff = TAKE(&p, float);
            l->art_alpha = 1;
            break;
        case LAYER_INSTANCES:
            l->count = TAKE(&p, u16);
            l->instances = calloc(l->count ? l->count : 1, sizeof(RoomInstance));
            for (int k = 0; k < l->count; k++) {
                RoomInstance *in = &l->instances[k];
                in->object = TAKE(&p, s16);
                in->x = TAKE(&p, float);
                in->y = TAKE(&p, float);
                in->xscale = TAKE(&p, float);
                in->yscale = TAKE(&p, float);
                in->angle = TAKE(&p, float);
                in->image_speed = TAKE(&p, float);
                in->colour = TAKE(&p, u32);
                in->image_index = TAKE(&p, s16);
                in->flags = TAKE(&p, u8);
                in->name = take_str(&p);
                in->world = -1;
                in->mapobj = -1;
                in->creation = TAKE(&p, u16);
                in->prop_count = TAKE(&p, u8);
                in->props = in->prop_count ? calloc(in->prop_count, sizeof(InstProp)) : NULL;
                for (int j = 0; j < in->prop_count; j++) {
                    in->props[j].name = take_str(&p);
                    in->props[j].value = take_str(&p);
                }
            }
            break;
        case LAYER_ASSETS:
            l->count = TAKE(&p, u16);
            l->assets = calloc(l->count ? l->count : 1, sizeof(RoomAsset));
            for (int k = 0; k < l->count; k++) {
                RoomAsset *a = &l->assets[k];
                a->sprite = TAKE(&p, s16);
                a->x = TAKE(&p, float);
                a->y = TAKE(&p, float);
                a->xscale = TAKE(&p, float);
                a->yscale = TAKE(&p, float);
                a->angle = TAKE(&p, float);
                a->head = TAKE(&p, float);
                a->anim_speed = TAKE(&p, float);
                a->colour = TAKE(&p, u32);
            }
            break;
        }
    }
    qsort(room->layers, room->layer_count, sizeof(RoomLayer), by_depth_desc);
    return true;
}

void room_free(Room *room)
{
    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->instances)
            for (int k = 0; k < l->count; k++) free(l->instances[k].props);
        free(l->instances);
        free(l->assets);
    }
    free(room->layers);
    free(room->blob);
    memset(room, 0, sizeof *room);
}

static int find_matches(Room *room, int obj, RoomInstance **out, int max)
{
    int n = 0;
    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->type != LAYER_INSTANCES) continue;
        for (int k = 0; k < l->count; k++)
            if (object_is_child_of(l->instances[k].object, obj)) {
                if (out && n < max) out[n] = &l->instances[k];
                n++;
            }
    }
    return n;
}

int room_instance_number(Room *room, int obj)
{
    return find_matches(room, obj, NULL, 0);
}

RoomInstance *room_instance_find(Room *room, int obj, int n)
{
    RoomInstance *list[512];
    int count = find_matches(room, obj, list, 512);
    if (count > 512) count = 512;
    if (n < 0 || n >= count) return NULL;
    // selection by creation order
    for (int i = 0; i < count; i++)
        for (int j = i + 1; j < count; j++)
            if (list[j]->creation < list[i]->creation) {
                RoomInstance *t = list[i];
                list[i] = list[j];
                list[j] = t;
            }
    return list[n];
}
