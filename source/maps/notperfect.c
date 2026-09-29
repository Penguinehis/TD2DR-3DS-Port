// room_notperfect: obj_np_controller, obj_np_background, obj_np_teleporn, obj_np_white,
// obj_npring, obj_leftvitalkivatel / obj_rightvitalkivatel, SERVER_NPCONTROLLER_STATE.
//
// How the "rotation" works: the room holds four copies of the level, one per quadrant
// (x offset 2904, y offset 1368; the level art is split in "Tiles" / "Tiles p2"), each laid out
// differently. The server sends SERVER_NPCONTROLLER_STATE every 20 s: first warn = false (the
// next layout's preview, the "ShadowTiles" art at depth 90, fades in over 5 s), then warn = true
// with the new quadrant: the local player is moved by the quadrant offsets, the vital-kivatel
// walls push anyone stuck in a wall out, the white flash hides the jump, and
// obj_np_controller.tid selects the obj_np_teleporn area that keeps the player inside the
// active quadrant. Nothing is rotated at draw time, so the port does exactly the same.
#include <math.h>

#include "../audio.h"
#include "../chars/chars.h"
#include "../game.h"
#include "../gen/objects.h"
#include "../gen/rooms.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

#define NP_QUAD_W 2904
#define NP_QUAD_H 1368
#define SHADOW_DEPTH 90
#define TILES_DEPTH 92
#define MAX_SHADOW 4

typedef struct {
    float x, y, sx;
} Particle;

static struct {
    int tid;                     // obj_np_controller.tid
    float white;                 // obj_np_white image_alpha (GUI flash)
    // ShadowTiles (obj_tile at depth 90): drawn by one runtime object with the tiles' alpha
    RoomLayer *shadow[MAX_SHADOW];
    int shadow_count;
    bool shadow_vis;             // isVis
    float shadow_alpha;          // image_alpha
    // obj_np_background
    RoomLayer *bg, *colour;
    float spd;
    bool side, bigrig;
    int music0;                  // the level track when the room loaded (bigring detection)
    Particle particls[15], particls2[10];
} np;

static float current_time_ms(void) { return level.time * 1000.0f / 60; }

// obj_knux variables (chars/knux.c KnuxVars; keep in sync)
typedef struct {
    int glideTimer, glideTimeout;
    float glide_xspd;
    bool isGliding, isStuck;
} NpKnuxVars;
#define KNUX_GLIDE_RECHARGE (60 * 15)

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

// ------------------------------------------------------------------ obj_np_controller

static void controller_create(MapObj *o)
{
    (void)o;
    np.tid = 0;
}

static void controller_step(MapObj *o)
{
    (void)o;
    if (np.shadow_alpha < 1) np.shadow_alpha += 0.01666f / 5.0f;
}

// Drawn on the "Tiles" instance layer, which init orders before the Tiles art: the background
// particles of obj_np_background (depth 250 in the GML, behind the level art).
static void controller_draw(MapObj *o)
{
    (void)o;
    int frame = np.bigrig ? 1 : 0;
    if (np.side) {
        for (int i = 0; i < 10; i++)
            sprite_draw(BACKGROUND_NOTPERF2, frame, np.particls2[i].x, np.particls2[i].y, 1, 1, 0, 0xFFFFFFFF, 1);
    } else {
        for (int i = 0; i < 15; i++)
            sprite_draw(BACKGROUND_NOTPERF3, frame, np.particls[i].x, np.particls[i].y, 1, 1, 0, 0xFFFFFFFF, 1);
    }
}

// ------------------------------------------------------------------ obj_tile (ShadowTiles)

static void shadow_draw(MapObj *o)
{
    (void)o;
    if (!np.shadow_vis || np.shadow_alpha <= 0) return;
    float a = fminf(np.shadow_alpha, 1);
    for (int n = 0; n < np.shadow_count; n++) {
        const RoomLayer *l = np.shadow[n];
        const SpriteInfo *s = sprite_info(l->art_sprite);
        if (!s) continue;
        for (int k = 0; k < s->frame_count; k++) {
            float x = k * (float)s->width - level.cam_x, y = l->art_yoff - level.cam_y;
            if (x > TOP_W || x + s->width < 0 || y > TOP_H || y + s->height < 0) continue;
            sprite_draw(l->art_sprite, k, x + s->xorigin, y + s->yorigin, 1, 1, 0, 0xFFFFFFFF, a);
        }
    }
}

// ------------------------------------------------------------------ obj_np_background

static void background_create(MapObj *o)
{
    (void)o;
    np.bg = layer_find("Background");
    np.colour = layer_find("Colour");
    np.spd = 0;
    np.side = np.bigrig = false;
    // 3DS view (400x240) instead of the GML 480x270
    for (int i = 0; i < 15; i++) {
        np.particls[i].x = (rand() % 21) * 20;
        np.particls[i].y = i * 16;
    }
    for (int i = 0; i < 10; i++) {
        np.particls2[i].sx = np.particls2[i].x = i * 40;
        np.particls2[i].y = (rand() % 25) * 10;
    }
    if (np.bg) np.bg->bg_colour = 0xFFFFFFFF;
    if (np.colour) np.colour->bg_colour = 0xFF3F0000;  // make_color_rgb(0, 0, 63)
}

static void background_step(MapObj *o)
{
    (void)o;
    // Draw_0 particle movement
    if (np.side) {
        for (int i = 0; i < 10; i++) {
            Particle *p = &np.particls2[i];
            p->x = p->sx + sinf(current_time_ms() / 400 + i) * 2;
            if (--p->y <= -16) p->y = TOP_H + 16 + rand() % 33;
        }
    } else {
        for (int i = 0; i < 15; i++) {
            Particle *p = &np.particls[i];
            if (--p->x <= -64) p->x = TOP_W + 20;
        }
    }
}

static void set_npring_visible(void)
{
    for (int i = 0;; i++) {
        MapObj *r = mapobj_find(OBJ_NPRING, i);
        if (!r) break;
        r->visible = true;
    }
}

// Draw_72 (Pre-Draw)
static void background_end_step(MapObj *o)
{
    // instance_exists(obj_bigring): game.c keeps the exit ring private; it switches the music to
    // the chase track when the ring spawns, which is what is detected here.
    int m = game_music();
    if (!np.bigrig && game_active() && np.music0 >= 0 && m >= 0 && m != np.music0) {
        np.bigrig = true;
        if (np.bg) np.bg->bg_colour = 0xFF0000FF;         // g_TintCol [1, 0, 0, 1]
        if (np.colour) np.colour->bg_colour = 0xFF000000; // c_black
        // (the greyscale filter on the tile layers is a shader: omitted)
        np.white = 1;
        // SERVER_GAME_SPAWN_RING in room_notperfect: with(obj_npring) visible = true
        set_npring_visible();
    }
    if (o->image_index >= 107) np.side = true;
    if (o->image_index >= 217) np.side = false;
    np.spd += np.side ? -0.5f : 0.5f;
    layer_set_pos(np.bg, level.cam_x + np.spd, level.cam_y);
}

static void background_draw(MapObj *o) { (void)o; }  // draw_self at (0, -128) is above the room

// ------------------------------------------------------------------ obj_np_teleporn

static void teleporn_step(MapObj *o)
{
    if (!level.has_player) return;
    if (np.tid != (int)mapobj_prop(o, "tid", 0)) return;
    if (!mapobj_meets_player(o)) {
        level.player.x = mapobj_prop(o, "pX", 0);
        level.player.y = mapobj_prop(o, "pY", 0);
    }
    // obj_exeller_clone instances are not reachable from here (entities.c)
}

// ------------------------------------------------------------------ obj_left/rightvitalkivatel

typedef struct {
    bool active;
} Kivatel;

static void kivatel_push(MapObj *o)
{
    if (!level.has_player) return;
    Player *p = &level.player;
    float l, t, r, b;
    if (!box(o, &l, &t, &r, &b)) return;
    // collision_rectangle(p.x-6, p.y-20, p.x+6, p.y+18, self)
    if (p->x + 6 < l || p->x - 6 >= r || p->y + 18 < t || p->y - 20 >= b) return;
    if (o->object == OBJ_RIGHTVITALKIVATEL) p->x = (r - 1) + 14;  // bbox_right + 14
    else p->x = l - 20;                                            // bbox_left - 20
    p->xspd = 0;
    p->yspd = 0;
}

static void kivatel_create(MapObj *o) { MAPOBJ_VARS(o, Kivatel)->active = false; }

static void kivatel_step(MapObj *o)
{
    if (MAPOBJ_VARS(o, Kivatel)->active) kivatel_push(o);
}

static void kivatel_end_step(MapObj *o)
{
    Kivatel *v = MAPOBJ_VARS(o, Kivatel);
    if (!v->active || !level.has_player) return;
    v->active = false;
    kivatel_push(o);
}

// ------------------------------------------------------------------ obj_npring

static void npring_create(MapObj *o) { o->image_alpha = 0; }

static void npring_step(MapObj *o)
{
    if (!o->visible) return;
    if (o->image_alpha < 1) o->image_alpha += 0.032f;
    // place_meeting(x, y, obj_bigring) -> instance_destroy: the exit ring's position is private
    // to game.c; the real ring is drawn at the same spot, over this one.
}

// ------------------------------------------------------------------ level

static void init(void)
{
    np.music0 = game_active() ? game_music() : -1;
    np.shadow_count = 0;
    Room *room = &level.room;
    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->type == LAYER_LEVELART && l->depth == SHADOW_DEPTH && np.shadow_count < MAX_SHADOW) {
            np.shadow[np.shadow_count++] = l;
            l->visible = false;  // drawn by the obj_tile runtime object with its alpha
        }
    }
    // Room creation code: with(obj_tile) if(depth == 90) { isVis = false; image_alpha = 0; }
    np.shadow_vis = false;
    np.shadow_alpha = 0;
    // The "Tiles" instance layer (obj_np_controller, which draws the background particles) and
    // the Tiles art share depth 92: put the instance layer first so the particles stay behind.
    int first_art = -1, inst = -1;
    for (int i = 0; i < room->layer_count; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->depth != TILES_DEPTH) continue;
        if (l->type == LAYER_LEVELART && first_art < 0) first_art = i;
        if (l->type == LAYER_INSTANCES && l->name && !strcmp(l->name, "Tiles")) inst = i;
    }
    if (first_art >= 0 && inst > first_art) {
        RoomLayer t = room->layers[first_art];
        room->layers[first_art] = room->layers[inst];
        room->layers[inst] = t;
    }
    MapObj *s = mapobj_create(OBJ_TILE, 0, 0);
    if (s) {
        s->depth = SHADOW_DEPTH;  // drawn with the runtime objects, below the player
        s->sprite = -1;
        s->visible = true;
    }
}

static void draw_gui(void)
{
    // obj_np_white: a white screen fading out
    if (np.white > 0) C2D_DrawRectSolid(0, 0, 0, TOP_W, TOP_H, C2D_Color32f(1, 1, 1, fminf(np.white, 1)));
}

static void step(void)
{
    if (np.white > 0) np.white -= 0.1f;
}

static void set_kivatels(void)
{
    for (int i = 0;; i++) {
        MapObj *k = mapobj_find(OBJ_LEFTVITALKIVATEL, i);
        if (!k) break;
        MAPOBJ_VARS(k, Kivatel)->active = true;
    }
    for (int i = 0;; i++) {
        MapObj *k = mapobj_find(OBJ_RIGHTVITALKIVATEL, i);
        if (!k) break;
        MAPOBJ_VARS(k, Kivatel)->active = true;
    }
}

static bool packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    if (type != SERVER_NPCONTROLLER_STATE) return false;
    if (pass) return true;
    bool warn = rd_u8(r);
    int state = rd_u8(r);
    int prev = rd_u8(r);
    if (!warn) {
        np.shadow_vis = true;
        np.shadow_alpha = 0;
        return true;
    }
    np.white = 1;
    audio_play(SND_NPTELEPORT);
    np.shadow_vis = false;
    np.shadow_alpha = 0;
    if (!level.has_player) return true;
    Player *p = &level.player;
    if (p->character == CHARACTER_KNUX) {
        NpKnuxVars *k = CHAR_VARS(p, NpKnuxVars);
        if (k->isStuck) {
            k->isStuck = false;
            k->isGliding = false;
            k->glideTimer = KNUX_GLIDE_RECHARGE;
        }
    }
    // (obj_exeller_clone instances move with the player in the GML: not reachable here)
    if (prev == 1 || prev == 3) p->x -= NP_QUAD_W;
    if (prev == 2 || prev == 3) p->y -= NP_QUAD_H;
    if (state == 1 || state == 3) p->x += NP_QUAD_W;
    if (state == 2 || state == 3) p->y += NP_QUAD_H;
    // (the Cream ringsTimer assignment in the GML runs in obj_netclient's scope: no effect)
    set_kivatels();
    np.tid = state;
    return true;
}

static void leave(void) { memset(&np, 0, sizeof np); }

static const ObjDef OBJECTS[] = {
    { .object = OBJ_NP_CONTROLLER, .create = controller_create, .step = controller_step, .draw = controller_draw },
    { .object = OBJ_NP_BACKGROUND, .create = background_create, .step = background_step,
      .end_step = background_end_step, .draw = background_draw },
    { .object = OBJ_NP_TELEPORN, .step = teleporn_step },
    { .object = OBJ_LEFTVITALKIVATEL, .create = kivatel_create, .begin_step = kivatel_step, .step = kivatel_step,
      .end_step = kivatel_end_step },
    { .object = OBJ_RIGHTVITALKIVATEL, .create = kivatel_create, .begin_step = kivatel_step, .step = kivatel_step,
      .end_step = kivatel_end_step },
    { .object = OBJ_NPRING, .create = npring_create, .step = npring_step },
    { .object = OBJ_TILE, .draw = shadow_draw },
    { .object = -1 },
};

const MapModule MAP_NOTPERFECT = {
    .room = ROOM_NOTPERFECT,
    .objects = OBJECTS,
    .init = init,
    .step = step,
    .draw_gui = draw_gui,
    .packet = packet,
    .leave = leave,
};
