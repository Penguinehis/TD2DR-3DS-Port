#include "level.h"

#include <limits.h>
#include <math.h>

#include "mapobj.h"
#include "pets.h"
#include "settings.h"
#include "maps/maps.h"
#include "sprite.h"
#include "world.h"

static Pet my_pet;  // settings.pet following level.player

Level level;

const char *room_name(int id)
{
    const char *path = ROOM_FILES[id];
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void level_unload(void)
{
    mapobj_room_end();
    room_free(&level.room);
    level.has_player = false;
}

void level_load(int id, int spawn_character)
{
    Room *room = &level.room;
    mapobj_room_end();
    room_free(room);
    if (!room_load(room, id)) fatal("Could not load %s", ROOM_FILES[id]);
    level.room_id = id;
    level.time = 0;
    level.cam_x = level.cam_y = 0;
    level.cam_dist = 0;
    level.cam_look_timer = 0;
    player_controls_lock = player_swap_dirs = false;
    player_room_id = id;
    level.pet_hidden = false;
    my_pet = (Pet){ -1000, -1000, 0 };
    mapobj_room_start(room);
    world_build(room);
    mapobj_room_ready(room);
    level.has_player = false;
    if (spawn_character < 0) return;
    // Spawn at the first spawn point if the room has one (the server picks one online).
    for (int i = 0; i < room->layer_count && !level.has_player; i++) {
        RoomLayer *l = &room->layers[i];
        if (l->type != LAYER_INSTANCES) continue;
        for (int k = 0; k < l->count; k++)
            if (l->instances[k].object == OBJ_SPAWNPOINT) {
                player_init(&level.player, spawn_character, 0, l->instances[k].x, l->instances[k].y);
                level.has_player = true;
                break;
            }
    }
    if (level.has_player) {
        level.cam_x = floorf(level.player.x) - TOP_W / 2;
        level.cam_y = floorf(level.player.y) - TOP_H / 2;
    }
}

void (*level_draw_objects)(void);

void level_spawn(int character, int exe_character, float x, float y)
{
    player_init(&level.player, character, exe_character, x, y);
    level.has_player = true;
    level.cam_x = floorf(x) - TOP_W / 2;
    level.cam_y = floorf(y) - TOP_H / 2;
}

Keys level_keys(u32 held, u32 down)
{
    if (player_swap_dirs) {
        u32 swap = KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN;
        u32 h = held & swap, d = down & swap;
        held &= ~swap;
        down &= ~swap;
        if (h & KEY_LEFT) held |= KEY_RIGHT;
        if (h & KEY_RIGHT) held |= KEY_LEFT;
        if (h & KEY_UP) held |= KEY_DOWN;
        if (h & KEY_DOWN) held |= KEY_UP;
        if (d & KEY_LEFT) down |= KEY_RIGHT;
        if (d & KEY_RIGHT) down |= KEY_LEFT;
        if (d & KEY_UP) down |= KEY_DOWN;
        if (d & KEY_DOWN) down |= KEY_UP;
    }
    // KEY_LEFT etc. combine the D-Pad and the Circle Pad.
    Keys k = {
        .left = held & KEY_LEFT, .right = held & KEY_RIGHT, .up = held & KEY_UP, .down = held & KEY_DOWN,
        .a = held & KEY_A, .b = held & KEY_B, .c = held & KEY_Y,
        .left_p = down & KEY_LEFT, .right_p = down & KEY_RIGHT, .up_p = down & KEY_UP,
        .down_p = down & KEY_DOWN, .a_p = down & KEY_A, .b_p = down & KEY_B, .c_p = down & KEY_Y,
        .em1 = held & KEY_L, .em2 = held & KEY_R, .em3 = held & (KEY_ZL | KEY_ZR),
        .em1_p = down & KEY_L, .em2_p = down & KEY_R, .em3_p = down & (KEY_ZL | KEY_ZR),
    };
    return k;
}

// obj_camera Draw_76, follow mode, with the 3DS view size (400x240 instead of 480x270).
static void camera_follow(void)
{
    const Player *p = &level.player;
    const float hw = TOP_W / 2, hh = TOP_H / 2;
    if (!p->isGrounded) {
        float yy = floorf(p->y) - hh;
        level.cam_dist = level.cam_y - yy;
        level.cam_x = floorf(p->x) - hw;
        if (fabsf(level.cam_dist) > 32) {
            level.cam_dist = level.cam_dist > 0 ? 32 : -32;
            level.cam_y = floorf(p->y) - hh + level.cam_dist;
        }
    } else {
        if (p->isLookingDown) {
            level.cam_look_timer++;
            if (level.cam_look_timer > 60 * 1.5 && level.cam_dist < 100) level.cam_dist += 2;
        } else if (p->isLookingUp) {
            level.cam_look_timer++;
            if (level.cam_look_timer > 60 * 1.5 && level.cam_dist > -100) level.cam_dist -= 2;
        } else {
            level.cam_look_timer = 0;
        }
        level.cam_x = floorf(p->x) - hw;
        level.cam_y = floorf(p->y) - hh + level.cam_dist;
    }
    if (level.cam_look_timer == 0)
        for (int i = 0; i < 2 && level.cam_dist != 0; i++)
            level.cam_dist -= (level.cam_dist > 0) - (level.cam_dist < 0);
}

static void clamp_camera(void)
{
    float max_x = (float)level.room.width - TOP_W, max_y = (float)level.room.height - TOP_H;
    if (level.cam_x > max_x) level.cam_x = max_x;
    if (level.cam_y > max_y) level.cam_y = max_y;
    if (level.cam_x < 0) level.cam_x = 0;
    if (level.cam_y < 0) level.cam_y = 0;
}

void level_update(const Keys *k, u32 held)
{
    if (level.has_player) {
        player_step(&level.player, k, level.room.width, level.room.height);
        int pet = pet_sprite(settings.pet);
        if (pet >= 0)
            pet_follow(&my_pet, pet, level.player.x, level.player.y, level.player.image_xscale, level.player.isHiding);
        world_step();
        mapobj_step();
        camera_follow();
        float ex, ey;
        if (maps_limpcity_camera(&ex, &ey)) {
            level.cam_x = floorf(ex) - TOP_W / 2;
            level.cam_y = floorf(ey) - TOP_H / 2;
        }
    } else {
        mapobj_step();
        circlePosition cp;
        hidCircleRead(&cp);
        float speed = (held & KEY_B) ? 16 : 4;
        float dx = cp.dx / 156.0f, dy = -cp.dy / 156.0f;
        if (held & KEY_DLEFT) dx = -1;
        if (held & KEY_DRIGHT) dx = 1;
        if (held & KEY_DUP) dy = -1;
        if (held & KEY_DDOWN) dy = 1;
        if (fabsf(dx) > 0.1f) level.cam_x += dx * speed;
        if (fabsf(dy) > 0.1f) level.cam_y += dy * speed;
    }
    clamp_camera();
    // Pixel-perfect: the camera always sits on whole pixels.
    level.cam_x = floorf(level.cam_x);
    level.cam_y = floorf(level.cam_y);
    level.time++;
}

// ------------------------------------------------------------------ drawing

static float anim_frame(int spr, float start, float speed)
{
    return start + level.time * sprite_frame_step(spr) * speed;
}

static bool on_screen(int spr, float x, float y, float xs, float ys)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return false;
    float ax = fabsf(xs), ay = fabsf(ys);
    float reach = (s->width > s->height ? s->width : s->height) * (ax > ay ? ax : ay);
    return x + reach >= 0 && y + reach >= 0 && x - reach <= level.view_w && y - reach <= level.view_h;
}

static void draw_background(const RoomLayer *l)
{
    const SpriteInfo *s = sprite_info(l->bg_sprite);
    if (!s || !settings.gfx_backgrounds) return;
    float lx = l->bg_x, ly = l->bg_y;
    if (l->parallax && !l->manual_pos) {
        if (settings.gfx_parallax) {
            lx = level.cam_x * l->px;
            ly = (level.cam_y + l->pyoff) * l->py;
        } else {
            lx = level.cam_x;  // pinned to the screen
            ly = level.cam_y;
        }
    }
    lx += level.time * l->hspeed;
    ly += level.time * l->vspeed;
    float frame = anim_frame(l->bg_sprite, 0, 1);
    if (l->stretch) {
        sprite_draw(l->bg_sprite, frame, lx - level.cam_x + s->xorigin, ly - level.cam_y + s->yorigin,
                    level.room.width / (float)s->width, level.room.height / (float)s->height, 0, l->bg_colour, 1);
        return;
    }
    sprite_draw_tiled(l->bg_sprite, frame, lx - level.cam_x, ly - level.cam_y, l->htiled, l->vtiled, level.view_w, level.view_h,
                      l->bg_colour);
}

static void draw_level_art(const RoomLayer *l)
{
    const SpriteInfo *s = sprite_info(l->art_sprite);
    if (!s) return;
    for (int k = 0; k < s->frame_count; k++) {
        float x = k * (float)s->width - level.cam_x, y = l->art_yoff - level.cam_y;
        if (x > level.view_w || x + s->width < 0 || y > level.view_h || y + s->height < 0) continue;
        sprite_draw(l->art_sprite, k, x + s->xorigin, y + s->yorigin, 1, 1, 0, 0xFFFFFFFF, l->art_alpha);
    }
}

static void draw_hidden_box(const RoomInstance *in, const ObjectInfo *o)
{
    const SpriteInfo *s = sprite_info(o->mask >= 0 ? o->mask : o->sprite);
    float x = in->x - level.cam_x, y = in->y - level.cam_y;
    if (!s) {
        C2D_DrawRectSolid(x - 3, y - 3, 0, 6, 6, C2D_Color32(255, 0, 255, 200));
        return;
    }
    float l = x + (s->bbox_left - s->xorigin) * in->xscale;
    float t = y + (s->bbox_top - s->yorigin) * in->yscale;
    float w = (s->bbox_right - s->bbox_left + 1) * in->xscale;
    float h = (s->bbox_bottom - s->bbox_top + 1) * in->yscale;
    u32 col = object_is_child_of(in->object, OBJ_SOLID_PARENT) ? C2D_Color32(255, 40, 40, 90)
                                                               : C2D_Color32(40, 160, 255, 90);
    C2D_DrawRectSolid(l, t, 0, w, h, col);
}

static void draw_instances(const RoomLayer *l)
{
    for (int k = 0; k < l->count; k++) {
        const RoomInstance *in = &l->instances[k];
        const ObjectInfo *o = object_info(in->object);
        if (!o) continue;
        if (mapobj_owns(in)) {
            mapobj_draw_instance(in);
            continue;
        }
        // Invisible objects, and objects whose Draw event replaces the default sprite
        // drawing (ported by hand later), are only shown as debug boxes.
        bool visible = (o->flags & OBJF_VISIBLE) && !(o->flags & OBJF_CUSTOM_DRAW);
        if (!visible) {
            if (level.show_hidden) draw_hidden_box(in, o);
            continue;
        }
        float x = in->x - level.cam_x, y = in->y - level.cam_y;
        if (o->sprite < 0 || !on_screen(o->sprite, x, y, in->xscale, in->yscale)) continue;
        float frame = anim_frame(o->sprite, in->image_index, in->image_speed);
        if (in->world >= 0) {
            const WorldInst *w = world_get(in->world);
            if (w->cats & (WC_SPRING | WC_HD_SPRING)) frame = w->frame;  // image_speed 0, driven by use
        }
        sprite_draw(o->sprite, frame, x, y,
                    in->xscale, in->yscale, in->angle, in->colour, ((in->colour >> 24) & 0xFF) / 255.0f);
    }
}

static void draw_assets(const RoomLayer *l)
{
    for (int k = 0; k < l->count; k++) {
        const RoomAsset *a = &l->assets[k];
        float x = a->x - level.cam_x, y = a->y - level.cam_y;
        if (!on_screen(a->sprite, x, y, a->xscale, a->yscale)) continue;
        sprite_draw(a->sprite, anim_frame(a->sprite, a->head, a->anim_speed), x, y,
                    a->xscale, a->yscale, a->angle, a->colour, ((a->colour >> 24) & 0xFF) / 255.0f);
    }
}

static void draw_player(void)
{
    if (!level.has_player) return;
    if (!level.pet_hidden)
        pet_draw(&my_pet, pet_sprite(settings.pet), level.player.image_xscale, level.player.isHiding ? 0.5f : 1);
    player_draw(&level.player, level.cam_x, level.cam_y);
}

void level_draw(void)
{
    if (level.view_w <= 0) {
        level.view_w = TOP_W;
        level.view_h = TOP_H;
    }
    const Room *room = &level.room;
    bool player_drawn = false;
    int until = INT_MAX;  // placed instances with a changed depth: those deeper than this are drawn
    for (int i = 0; i < room->layer_count; i++) {
        const RoomLayer *l = &room->layers[i];
        // The player object lives at depth 0; layers are sorted deepest first.
        if (!player_drawn && l->depth < 0) {
            mapobj_draw_moved(0, until);
            until = 0;
            mapobj_draw_spawned(false);
            if (level_draw_objects) level_draw_objects();
            draw_player();
            player_drawn = true;
        }
        if (l->depth < until) {
            mapobj_draw_moved(l->depth, until);
            until = l->depth;
        }
        if (!l->visible && !(level.show_hidden && l->type == LAYER_INSTANCES)) continue;
        switch (l->type) {
        case LAYER_BACKGROUND: if (!dbg_flag('b')) draw_background(l); break;
        case LAYER_LEVELART: if (!dbg_flag('a')) draw_level_art(l); break;
        case LAYER_INSTANCES: if (!dbg_flag('i')) draw_instances(l); break;
        case LAYER_ASSETS: if (!dbg_flag('s')) draw_assets(l); break;
        }
    }
    if (!player_drawn) {
        mapobj_draw_moved(0, until);
        until = 0;
        mapobj_draw_spawned(false);
        if (level_draw_objects) level_draw_objects();
        draw_player();
    }
    mapobj_draw_moved(INT_MIN, until);
    mapobj_draw_spawned(true);
    mapobj_draw_front();
}

int level_count_instances(void)
{
    int n = 0;
    for (int i = 0; i < level.room.layer_count; i++)
        if (level.room.layers[i].type == LAYER_INSTANCES) n += level.room.layers[i].count;
    return n;
}
