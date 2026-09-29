// Generic survivor behaviour used by characters that are not ported yet: the End Step
// state machine and Pre-Draw shared by every obj_<survivor> (without character states).
#include <math.h>

#include "chars.h"

void char_basic_end_step(Player *p)
{
    if (!p->emotion) p->state = ST_IDLE;
    if (p->isDead || p->shockedTimer > 0) { p->state = ST_DEAD; return; }
    if (p->isHurt) { p->state = ST_HURT; return; }
    if (p->isLookingUp) { p->state = ST_LOOKUP; return; }
    if (p->isLookingDown) { p->state = ST_LOOKDOWN; return; }
    if (p->isSpinning) { p->state = ST_SPIN; return; }
    if (fabsf(p->xspd) > 0 && p->isGrounded) p->state = fabsf(p->xspd) < 8 ? ST_WALK : ST_RUN;
    if (p->isOnEdge) { p->state = ST_BALANCING; return; }
    if (!p->isGrounded) p->state = p->isJumping ? ST_JUMP : ST_FALL;
}

void char_basic_pre_draw(Player *p)
{
    int spr = player_anim_sprite(p);
    if (spr >= 0) p->sprite = spr;
    int frames = player_frames(p);
    switch (p->state) {
    case ST_HURT: p->image_speed = 0; p->image_index = 0; break;
    case ST_BALANCING: p->image_xscale = p->edgeDir; p->image_speed = 1; break;
    case ST_FALL: p->image_index = p->yspd > 0 ? 1 : 0; break;
    case ST_WALK: p->image_speed = fabsf(p->xspd) / p->maxHSpeed; break;
    case ST_RUN:
    case ST_JUMP: p->image_speed = fmaxf(fabsf(p->xspd) / 20, 0.5f); break;
    case ST_DEAD:
        p->image_speed = p->isGrounded ? 1 : 0;
        if (p->image_index >= frames - 1) p->image_index = frames - 1;
        break;
    case ST_LOOKUP:
    case ST_LOOKDOWN:
        p->image_speed = 0.2f;
        if (p->image_index >= 1) p->image_index = 1;
        break;
    case ST_SPIN: p->image_speed = fabsf(p->xspd) / 5; break;
    default: p->image_speed = 1; break;
    }
}
