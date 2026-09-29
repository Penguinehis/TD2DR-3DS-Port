// Objects used by several levels: obj_abyss, obj_deathtp, obj_warning, obj_movingspike,
// obj_soundemitter. Level-specific objects live in their level's file.
#include <math.h>

#include "../achiev.h"
#include "../audio.h"
#include "../chars/chars.h"
#include "../gen/objects.h"
#include "../gen/sounds.h"
#include "../gen/sprites.h"
#include "../level.h"
#include "../sprite.h"
#include "maps.h"

// Axis-aligned box of an object's mask in the room.
static bool box(const MapObj *o, int spr, float *l, float *t, float *r, float *b)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return false;
    float x0 = o->x + (s->bbox_left - s->xorigin) * o->xscale, x1 = o->x + (s->bbox_right + 1 - s->xorigin) * o->xscale;
    float y0 = o->y + (s->bbox_top - s->yorigin) * o->yscale, y1 = o->y + (s->bbox_bottom + 1 - s->yorigin) * o->yscale;
    *l = fminf(x0, x1);
    *r = fmaxf(x0, x1);
    *t = fminf(y0, y1);
    *b = fmaxf(y0, y1);
    return true;
}

// ------------------------------------------------------------------ obj_abyss

static void abyss_step(MapObj *o)
{
    if (!level.has_player || !mapobj_meets_player(o)) return;
    Player *p = &level.player;
    WorldInst *target = world_nearest(o->x, o->y, WC_ABYSS_TARGET);
    if (!target) return;
    p->x = target->x;
    p->y = target->y;
    p->xspd = p->gspd = p->yspd = 0;
    if (p->def && p->def->on_abyss) p->def->on_abyss(p);
    if (mapobj_prop(o, "shouldStun", 1) != 0 && (p->character == CHARACTER_EXE || p->revivalTimes >= 2))
        p->shockedTimer += 30;
    player_hurt(p, 20, 0, 0);
    // TODO(M6): obj_ravintmist_shard in the abyss goes back to the target (Ravine Mist)
}

// ------------------------------------------------------------------ obj_deathtp

static void deathtp_step(MapObj *o)
{
    if (!level.has_player) return;
    Player *p = &level.player;
    if (p->state != ST_DEAD || p->hp > 0) return;
    float l, t, r, b;
    if (!box(o, o->sprite, &l, &t, &r, &b)) return;
    // place_meeting(x, y, obj_player_sensorBL): the sensor is a point
    if (p->sBL.x < l || p->sBL.x >= r || p->sBL.y < t || p->sBL.y >= b) return;
    WorldInst *pt = world_nearest(p->x, p->y, WC_DEATH_TP);
    if (!pt) return;
    p->x = pt->x;
    p->y = pt->y - 2;
    p->gspd = p->xspd = p->yspd = 0;
}

// ------------------------------------------------------------------ obj_warning

static void warning_step(MapObj *o)
{
    if (!level.has_player) {
        o->image_alpha = 0;
        return;
    }
    // distance_to_object: from the player's bbox to ours; approximated from the origin
    float l, t, r, b;
    if (!box(o, o->sprite, &l, &t, &r, &b)) return;
    const Player *p = &level.player;
    float dx = fmaxf(fmaxf(l - p->x, 0), p->x - r), dy = fmaxf(fmaxf(t - p->y, 0), p->y - b);
    float dist = sqrtf(dx * dx + dy * dy);
    o->image_alpha = (48 - fminf(fmaxf(dist, 0), 48)) / 48.0f;
}

// ------------------------------------------------------------------ obj_movingspike

static void movingspike_create(MapObj *o) { o->image_speed = 0; }

static void movingspike_begin_step(MapObj *o)
{
    float cx = level.cam_x + TOP_W / 2, cy = level.cam_y + TOP_H / 2;
    o->visible = fabsf(o->x - cx) < 530 + TOP_W / 2 && fabsf(o->y - cy) < 530 + TOP_H / 2;
    // scr_collision_objects: the spike only hurts on frames 1..4 (mask spr_spike)
    int frame = (int)o->image_index;
    if (!level.has_player || frame <= 0 || frame >= 5) return;
    float l, t, r, b;
    if (!box(o, SPR_SPIKE, &l, &t, &r, &b)) return;
    Player *p = &level.player;
    if (p->x + 6 < l || p->x - 6 >= r || p->y + 20 < t || p->y + 19 >= b) return;
    p->angle = 0;
    player_hurt_snd(p, 20, -p->image_xscale * 4, -6, SND_SPIKE);
}

// ------------------------------------------------------------------ obj_menu_money

static void menu_money_draw(MapObj *o)
{
    // scr_counter_draw_s(obj_achivements.mercoins, x, y)
    char str[24];
    snprintf(str, sizeof str, "%llu", (unsigned long long)achiev.mercoins);
    for (int i = 0; str[i]; i++)
        sprite_draw(SPR_COUNTER, str[i] - '0', o->x + 11 * i - level.cam_x, o->y - level.cam_y, 1, 1, 0, 0xFFFFFFFF, 1);
}

// ------------------------------------------------------------------ table

const ObjDef MAP_COMMON_OBJECTS[] = {
    { .object = OBJ_ABYSS, .step = abyss_step },
    { .object = OBJ_DEATHTP, .step = deathtp_step },
    { .object = OBJ_WARNING, .step = warning_step },
    { .object = OBJ_MOVINGSPIKE, .create = movingspike_create, .begin_step = movingspike_begin_step },
    { .object = OBJ_SOUNDEMITTER },  // positions only; found by the packet handlers
    { .object = OBJ_MENU_MONEY, .draw = menu_money_draw },
    { .object = -1 },
};

// ------------------------------------------------------------------ shared packets

// Positional sound (scr_audio_play_3d with the usual emitter falloff).
void maps_sound_at(int snd, float x, float y)
{
    float dx = x - (level.cam_x + TOP_W / 2), dy = y - (level.cam_y + TOP_H / 2);
    float d = sqrtf(dx * dx + dy * dy);
    float gain = 1.0f - fminf(d, 700) / 700;
    if (d > 250) gain *= fmaxf(0, 1 - (d - 250) / 250);
    if (gain > 0.02f) audio_play_ex(snd, gain, false);
}

bool maps_common_packet(PacketType type, bool pass, NetReader *r, bool reliable)
{
    (void)reliable;
    switch (type) {
    case SERVER_MOVINGSPIKE_STATE: {
        if (pass) return true;
        u8 frame = rd_u8(r);
        if (!mapobj_number(OBJ_MOVINGSPIKE)) return true;
        if (frame == 1 || frame == 3)
            for (int i = 0;; i++) {
                MapObj *e = mapobj_find(OBJ_SOUNDEMITTER, i);
                if (!e) break;
                if ((int)mapobj_prop(e, "stype", -1) == 0) maps_sound_at(SND_MOVINGSPIKE, e->x, e->y - 16);
            }
        for (int i = 0;; i++) {
            MapObj *s = mapobj_find(OBJ_MOVINGSPIKE, i);
            if (!s) break;
            s->image_index = frame;
        }
        return true;
    }
    default:
        return false;
    }
}
