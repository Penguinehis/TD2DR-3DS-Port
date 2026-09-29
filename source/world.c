// Collision world built from a room's instances.
//
// Shapes follow GameMaker: an instance collides with its mask sprite (the object's mask,
// else its sprite). Sprites exported with a precise mask are tested per pixel; others use
// their bounding box. Scale and rotation are applied like GameMaker's image_xscale /
// image_yscale / image_angle.
#include "world.h"

#include <math.h>

#include "sprite.h"

#define GRID 128
#define MAX_PER_CELL_QUERY 256

static WorldInst *insts;
static int inst_count;

// Uniform grid of instance index lists.
static int grid_w, grid_h;
static int *cell_start, *cell_items;
static u32 query_stamp = 1;
static u32 *seen;
static int *dyn;          // indices of dynamic instances
static int dyn_count;

bool (*world_is_dynamic)(int object);

int world_count(void) { return inst_count; }
WorldInst *world_get(int i) { return &insts[i]; }

static void cell_range(const WorldInst *w, int *cx0, int *cy0, int *cx1, int *cy1)
{
    *cx0 = (int)floorf(w->l / GRID) + 1;
    *cx1 = (int)floorf((w->r - 0.001f) / GRID) + 1;
    *cy0 = (int)floorf(w->t / GRID) + 1;
    *cy1 = (int)floorf((w->b - 0.001f) / GRID) + 1;
    if (*cx0 < 0) *cx0 = 0;
    if (*cy0 < 0) *cy0 = 0;
    if (*cx1 >= grid_w) *cx1 = grid_w - 1;
    if (*cy1 >= grid_h) *cy1 = grid_h - 1;
}

static u32 categorize(int obj)
{
    u32 c = 0;
    if (object_is_child_of(obj, OBJ_FLOOR_PARENT)) c |= WC_FLOOR;
    if (object_is_child_of(obj, OBJ_PLATFORM_JUMPTROUGH)) c |= WC_JUMPTHROUGH;
    if (object_is_child_of(obj, OBJ_SPRING_PARENT)) c |= WC_SPRING;
    if (obj == OBJ_HD_SPRING) c |= WC_HD_SPRING;
    if (object_is_child_of(obj, OBJ_DAMAGE)) c |= WC_DAMAGE;
    if (object_is_child_of(obj, OBJ_SPIKE)) c |= WC_SPIKE;
    if (object_is_child_of(obj, OBJ_ANGLEANULLER) || object_is_child_of(obj, OBJ_ANGLEANULLER_EGGSAFE)) c |= WC_ANGLE_NULL;
    if (object_is_child_of(obj, OBJ_ANGLEALLOWER)) c |= WC_ANGLE_ALLOW;
    if (object_is_child_of(obj, OBJ_HIDING_PARENT)) c |= WC_HIDING;
    if (obj == OBJ_ABYSS_TARGET) c |= WC_ABYSS_TARGET;
    if (obj == OBJ_DEATHTP_POINT) c |= WC_DEATH_TP;
    if (obj == OBJ_SPAWNPOINT) c |= WC_SPAWN;
    return c;
}

// Spring launch speeds (xdir, ydir from each spring object's Create event).
static void spring_dirs(int obj, float *x, float *y)
{
    switch (obj) {
    case OBJ_SPRING_UP: *x = 0; *y = -12; break;
    case OBJ_SPRING_LEFT: *x = -12; *y = -12; break;
    case OBJ_SPRING_RIGHT: *x = 12; *y = -12; break;
    case OBJ_BSPRING_UP: *x = 0; *y = -10; break;
    case OBJ_BSPRING_LEFT: *x = -10; *y = -10; break;
    case OBJ_BSPRING_RIGHT: *x = 10; *y = -10; break;
    case OBJ_YSPRING_UP: *x = 0; *y = -11; break;
    default: *x = 0; *y = 0; break;
    }
}

static void compute_aabb(WorldInst *w)
{
    const SpriteInfo *s = sprite_info(w->mask_sprite);
    if (!s) {
        // Shapeless marker objects (spawn points): a 1x1 point.
        w->l = w->x; w->t = w->y; w->r = w->x + 1; w->b = w->y + 1;
        return;
    }
    // Local rectangle relative to the origin, then scaled and rotated.
    float lx0 = (s->bbox_left - s->xorigin) * w->xscale;
    float lx1 = (s->bbox_right + 1 - s->xorigin) * w->xscale;
    float ly0 = (s->bbox_top - s->yorigin) * w->yscale;
    float ly1 = (s->bbox_bottom + 1 - s->yorigin) * w->yscale;
    if (w->angle == 0) {
        w->l = w->x + fminf(lx0, lx1); w->r = w->x + fmaxf(lx0, lx1);
        w->t = w->y + fminf(ly0, ly1); w->b = w->y + fmaxf(ly0, ly1);
        return;
    }
    float a = w->angle * (float)M_PI / 180.0f, c = cosf(a), sn = sinf(a);
    float xs[4] = { lx0, lx1, lx1, lx0 }, ys[4] = { ly0, ly0, ly1, ly1 };
    w->l = w->t = 1e9f; w->r = w->b = -1e9f;
    for (int i = 0; i < 4; i++) {
        // GameMaker angles are counter-clockwise with y pointing down.
        float rx = xs[i] * c + ys[i] * sn, ry = -xs[i] * sn + ys[i] * c;
        w->l = fminf(w->l, w->x + rx); w->r = fmaxf(w->r, w->x + rx);
        w->t = fminf(w->t, w->y + ry); w->b = fmaxf(w->b, w->y + ry);
    }
}

void world_free(void)
{
    free(insts); insts = NULL; inst_count = 0;
    free(cell_start); cell_start = NULL;
    free(cell_items); cell_items = NULL;
    free(seen); seen = NULL;
    free(dyn); dyn = NULL; dyn_count = 0;
}

void world_move(WorldInst *w, float x, float y)
{
    w->x = x;
    w->y = y;
    compute_aabb(w);
}

void world_build(Room *room)
{
    world_free();
    int cap = 0;
    for (int i = 0; i < room->layer_count; i++)
        if (room->layers[i].type == LAYER_INSTANCES) cap += room->layers[i].count;
    insts = calloc(cap ? cap : 1, sizeof(WorldInst));

    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->type != LAYER_INSTANCES) continue;
        for (int k = 0; k < l->count; k++) {
            RoomInstance *in = &l->instances[k];
            u32 cats = categorize(in->object);
            if (!cats) continue;
            const ObjectInfo *o = object_info(in->object);
            WorldInst *w = &insts[inst_count++];
            w->object = in->object;
            w->cats = cats;
            w->x = in->x; w->y = in->y;
            w->xscale = in->xscale; w->yscale = in->yscale;
            w->angle = in->angle;
            w->mask_sprite = o->mask >= 0 ? o->mask : o->sprite;
            w->frame = in->image_index;
            w->src = in;
            in->world = inst_count - 1;
            spring_dirs(in->object, &w->spring_x, &w->spring_y);
            w->dynamic = world_is_dynamic && world_is_dynamic(in->object);
            compute_aabb(w);
        }
    }

    // Grid
    grid_w = (room->width + GRID - 1) / GRID + 2;
    grid_h = (room->height + GRID - 1) / GRID + 2;
    int cells = grid_w * grid_h;
    cell_start = calloc(cells + 1, sizeof(int));
    int total = 0;
    dyn = calloc(inst_count ? inst_count : 1, sizeof(int));
    for (int i = 0; i < inst_count; i++) {
        if (insts[i].dynamic) { dyn[dyn_count++] = i; continue; }
        int cx0, cy0, cx1, cy1;
        cell_range(&insts[i], &cx0, &cy0, &cx1, &cy1);
        for (int cy = cy0; cy <= cy1; cy++)
            for (int cx = cx0; cx <= cx1; cx++) { cell_start[cy * grid_w + cx + 1]++; total++; }
    }
    for (int c = 0; c < cells; c++) cell_start[c + 1] += cell_start[c];
    cell_items = malloc((total ? total : 1) * sizeof(int));
    int *fill = calloc(cells, sizeof(int));
    for (int i = 0; i < inst_count; i++) {
        if (insts[i].dynamic) continue;
        int cx0, cy0, cx1, cy1;
        cell_range(&insts[i], &cx0, &cy0, &cx1, &cy1);
        for (int cy = cy0; cy <= cy1; cy++)
            for (int cx = cx0; cx <= cx1; cx++) {
                int c = cy * grid_w + cx;
                cell_items[cell_start[c] + fill[c]++] = i;
            }
    }
    free(fill);
    seen = calloc(inst_count ? inst_count : 1, sizeof(u32));
}

int world_query_rect(float l, float t, float r, float b, u32 cats, WorldInst **out, int max)
{
    if (!inst_count) return 0;
    query_stamp++;
    int cx0 = (int)floorf(l / GRID) + 1, cx1 = (int)floorf(r / GRID) + 1;
    int cy0 = (int)floorf(t / GRID) + 1, cy1 = (int)floorf(b / GRID) + 1;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 >= grid_w) cx1 = grid_w - 1;
    if (cy1 >= grid_h) cy1 = grid_h - 1;
    int n = 0;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) {
            int c = cy * grid_w + cx;
            for (int k = cell_start[c]; k < cell_start[c + 1]; k++) {
                int i = cell_items[k];
                if (seen[i] == query_stamp) continue;
                seen[i] = query_stamp;
                WorldInst *w = &insts[i];
                if (!(w->cats & cats) || w->disabled) continue;
                if (w->r <= l || w->l > r || w->b <= t || w->t > b) continue;
                if (n < max) out[n++] = w;
            }
        }
    for (int k = 0; k < dyn_count; k++) {
        WorldInst *w = &insts[dyn[k]];
        if (!(w->cats & cats) || w->disabled) continue;
        if (w->r <= l || w->l > r || w->b <= t || w->t > b) continue;
        if (n < max) out[n++] = w;
    }
    return n;
}

bool inst_contains_point(const WorldInst *w, float px, float py)
{
    if (px < w->l || px >= w->r || py < w->t || py >= w->b) return false;
    const SpriteInfo *s = sprite_info(w->mask_sprite);
    if (!s) return true;
    if (w->angle == 0 && !sprite_has_mask(w->mask_sprite)) return true;
    // Back into sprite-local pixels.
    float dx = px - w->x, dy = py - w->y;
    if (w->angle != 0) {
        float a = -w->angle * (float)M_PI / 180.0f, c = cosf(a), sn = sinf(a);
        float rx = dx * c + dy * sn, ry = -dx * sn + dy * c;
        dx = rx; dy = ry;
    }
    float lx = dx / w->xscale + s->xorigin, ly = dy / w->yscale + s->yorigin;
    int ix = (int)floorf(lx), iy = (int)floorf(ly);
    if (!sprite_has_mask(w->mask_sprite))
        return ix >= s->bbox_left && ix <= s->bbox_right && iy >= s->bbox_top && iy <= s->bbox_bottom;
    return sprite_mask_test(w->mask_sprite, (int)w->frame, ix, iy);
}

WorldInst *list_point(WorldInst **list, int n, float px, float py)
{
    for (int i = 0; i < n; i++)
        if (inst_contains_point(list[i], px, py)) return list[i];
    return NULL;
}

WorldInst *world_point(float px, float py, u32 cats)
{
    WorldInst *cand[MAX_PER_CELL_QUERY];
    int n = world_query_rect(px, py, px, py, cats, cand, MAX_PER_CELL_QUERY);
    return list_point(cand, n, px, py);
}

WorldInst *world_rect(float l, float t, float r, float b, u32 cats)
{
    if (l > r) { float tmp = l; l = r; r = tmp; }
    if (t > b) { float tmp = t; t = b; b = tmp; }
    WorldInst *cand[MAX_PER_CELL_QUERY];
    int n = world_query_rect(l, t, r, b, cats, cand, MAX_PER_CELL_QUERY);
    for (int i = 0; i < n; i++) {
        WorldInst *w = cand[i];
        const SpriteInfo *s = sprite_info(w->mask_sprite);
        if (!s || (w->angle == 0 && !sprite_has_mask(w->mask_sprite))) return w;
        // Precise shape: sample the overlap area per pixel.
        float x0 = fmaxf(l, w->l), x1 = fminf(r + 1, w->r), y0 = fmaxf(t, w->t), y1 = fminf(b + 1, w->b);
        for (float y = floorf(y0); y < y1; y++)
            for (float x = floorf(x0); x < x1; x++)
                if (inst_contains_point(w, x + 0.5f, y + 0.5f)) return w;
    }
    return NULL;
}

WorldInst *world_nearest(float x, float y, u32 cats)
{
    WorldInst *best = NULL;
    float bd = 1e30f;
    for (int i = 0; i < inst_count; i++) {
        WorldInst *w = &insts[i];
        if (!(w->cats & cats) || w->disabled) continue;
        float d = (w->x - x) * (w->x - x) + (w->y - y) * (w->y - y);
        if (d < bd) { bd = d; best = w; }
    }
    return best;
}

void world_step(void)
{
    for (int i = 0; i < inst_count; i++) {
        WorldInst *w = &insts[i];
        if (!(w->cats & (WC_SPRING | WC_HD_SPRING))) continue;
        // obj_spring_parent alarm[0] (0.5 s): back to frame 0; alarm[1] (4 s): usable again.
        if (w->spring_anim > 0 && --w->spring_anim == 0) w->frame = 0;
        if (w->spring_cooldown > 0) w->spring_cooldown--;
    }
}
