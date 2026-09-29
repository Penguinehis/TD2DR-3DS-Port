#pragma once
// Objects spawned during a match by the characters or the server (M5b): the Tails projectile,
// Eggman trackers, black rings, Exetior stomp balls, Exeller clones, ring shards and the
// quick effects (scr_effect_quick / net_quick_effect). Ports of the obj_* objects and their
// net_state_game / net_udpprocess handlers.

#include "common.h"
#include "net.h"

void ents_clear(void);
// set by game.c: the tracker reveal arrow (obj_surv_indicator), target puppet id
extern void (*ents_on_tracker_reveal)(u16 target);
// A black ring within dist pixels of (x, y) (instance_exists + distance_to_object).
bool ents_blackring_near(float x, float y, float dist);
// Our own Tails projectile (not breaking) overlaps the rectangle.
bool ents_own_projectile_meets(float l, float t, float r, float b);
// place_meeting(x, y, obj_tails_projectile) (any player's shot, not breaking) and
// place_meeting(x, y, obj_exetior_stompballs) against a rectangle.
bool ents_projectile_meets(float l, float t, float r, float b);
bool ents_stompball_meets(float l, float t, float r, float b);
// A match packet; returns true if it was an entity packet.
bool ents_packet(PacketType type, bool pass, NetReader *r, bool reliable);
void ents_step(void);
void ents_draw(void);        // at the player's depth
void ents_draw_front(void);  // effects and black rings (depth -200)

// scr_effect_quick: a local effect. fade: fade out by speed per frame instead of animating.
void effect_quick(float x, float y, int sprite, float speed, bool fade, float xspd, float yspd, float xscale);
// net_quick_effect: a local effect that the other players see too (sprite from global.net_effs).
void net_quick_effect(float x, float y, int sprite, bool fade, int dir, int xspd, int yspd, float speed);
