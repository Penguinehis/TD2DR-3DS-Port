// Test bot for the Disaster server protocol: joins a lobby, readies up, votes, picks a
// character, then streams CLIENT_PLAYER_DATA walking left and right. Used to test the 3DS
// client without a second console.
//
//   bot <host> [port] [nickname] [seconds] [exe]
// "exe" makes the bot the EXE (op-only chat trigger; local clients are operators).
//
// Build (Docker): see tools/netbot/build.sh
#include <enet/enet.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../../source/net_packets.h"

#define BUILD_VER 1101

static ENetPeer *peer;
static uint16_t my_id;
static int exe_id = -1;
static int state;  // 0 pending, 1 lobby, 2 vote, 3 charselect, 4 game
static float px, py;
static int game_ready;
static int have_target;
static int want_exe;  // seen another player's position: walk around it

typedef struct { uint8_t b[256]; int n; } Pk;
static void w(Pk *p, const void *v, int n) { memcpy(p->b + p->n, v, n); p->n += n; }
static void w8(Pk *p, uint8_t v) { w(p, &v, 1); }
static void w16(Pk *p, uint16_t v) { w(p, &v, 2); }
static void w32(Pk *p, uint32_t v) { w(p, &v, 4); }
static void w64(Pk *p, uint64_t v) { w(p, &v, 8); }
static void wstr(Pk *p, const char *s) { w(p, s, strlen(s) + 1); }
static void begin(Pk *p, int type) { p->n = 0; w8(p, 0); w8(p, type); }
static void send_pk(Pk *p, int reliable)
{
    enet_peer_send(peer, reliable ? 0 : 1, enet_packet_create(p->b, p->n, reliable ? ENET_PACKET_FLAG_RELIABLE : 0));
}

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("[bot] ");
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
}

static uint16_t f16(float f)
{
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int e = (int)((x >> 23) & 0xFF) - 127 + 15;
    if (e <= 0) return sign;
    if (e >= 31) return sign | 0x7C00;
    return sign | (e << 10) | ((x & 0x7FFFFF) >> 13);
}

int main(int argc, char **argv)
{
    const char *hostname = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 8606;
    const char *nick = argc > 3 ? argv[3] : "netbot";
    int seconds = argc > 4 ? atoi(argv[4]) : 120;
    want_exe = argc > 5 && !strcmp(argv[5], "exe");

    enet_initialize();
    ENetHost *host = enet_host_create(NULL, 1, 2, 5000000, 5000000);
    ENetAddress addr;
    enet_address_set_host(&addr, hostname);
    addr.port = port;
    peer = enet_host_connect(host, &addr, 2, 0);
    logf_("connecting to %s:%d as %s", hostname, port, nick);

    time_t end = time(NULL) + seconds;
    int tick = 0;
    while (time(NULL) < end) {
        ENetEvent ev;
        while (enet_host_service(host, &ev, 16) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) logf_("connected");
            else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) { logf_("disconnected (%u)", ev.data); return 0; }
            else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                uint8_t *d = ev.packet->data;
                int type = d[1];
                Pk p;
                switch (type) {
                case SERVER_PREIDENTITY:
                    begin(&p, IDENTITY);
                    w16(&p, BUILD_VER); w32(&p, (uint32_t)-1); wstr(&p, nick); wstr(&p, "mod-netbot-0001");
                    w8(&p, 3); w8(&p, (uint8_t)-1); w64(&p, 0); w64(&p, 0);
                    send_pk(&p, 1);
                    break;
                case SERVER_IDENTITY_RESPONSE:
                    memcpy(&my_id, d + 3, 2);
                    logf_("identity: lobby=%d id=%d", d[2], my_id);
                    if (d[2]) {
                        state = 1;
                        if (want_exe) {
                            begin(&p, CLIENT_CHAT_MESSAGE); w16(&p, my_id); wstr(&p, "i want big burgr"); send_pk(&p, 1);
                        }
                        begin(&p, CLIENT_LOBBY_PLAYERS_REQUEST); send_pk(&p, 1);
                        begin(&p, CLIENT_LOBBY_READY_STATE); w8(&p, 1); send_pk(&p, 1);
                    }
                    break;
                case SERVER_GAME_BACK_TO_LOBBY:
                    logf_("back to lobby");
                    state = 1;
                    game_ready = 0;
                    have_target = 0;
                    begin(&p, CLIENT_LOBBY_PLAYERS_REQUEST); send_pk(&p, 1);
                    begin(&p, CLIENT_LOBBY_READY_STATE); w8(&p, 1); send_pk(&p, 1);
                    break;
                case CLIENT_CHAT_MESSAGE: {
                    uint16_t id; memcpy(&id, d + 2, 2);
                    logf_("chat from %d: %s", id, (char *)d + 4);
                    break;
                }
                case SERVER_LOBBY_COUNTDOWN: logf_("countdown %d %d", d[2], d[3]); break;
                case SERVER_VOTE_MAPS:
                    logf_("vote maps %d %d %d", d[2], d[3], d[4]);
                    state = 2;
                    begin(&p, CLIENT_VOTE_REQUEST); w8(&p, 0); send_pk(&p, 1);
                    break;
                case SERVER_LOBBY_EXE: {
                    uint16_t exe, map; memcpy(&exe, d + 2, 2); memcpy(&map, d + 4, 2);
                    exe_id = exe;
                    state = 3;
                    logf_("charselect: exe %d map %d", exe, map);
                    if (exe == my_id) { begin(&p, CLIENT_REQUEST_EXECHARACTER); w8(&p, 1); }
                    else { begin(&p, CLIENT_REQUEST_CHARACTER); w8(&p, 2); }  // knuckles
                    send_pk(&p, 1);
                    break;
                }
                case SERVER_LOBBY_CHARACTER_RESPONSE:
                    logf_("character response %d ok=%d", d[2], d[3]);
                    if (!d[3]) { begin(&p, CLIENT_REQUEST_CHARACTER); w8(&p, 4); send_pk(&p, 1); }
                    break;
                case SERVER_LOBBY_GAME_START: logf_("game start"); state = 4; break;
                case SERVER_GAME_PLAYERS_READY: logf_("players ready"); game_ready = 1; break;
                case SERVER_GAME_SPAWN_RING: break;
                case SERVER_HEARTBEAT: case SERVER_GAME_PING: case SERVER_PONG: break;
                case CLIENT_PLAYER_DATA: {
                    uint16_t pid, x, y;
                    memcpy(&pid, d + 2, 2); memcpy(&x, d + 4, 2); memcpy(&y, d + 6, 2);
                    if (pid != my_id && !have_target) {
                        have_target = 1;
                        px = x + 40; py = y;
                        logf_("following player %d at %d,%d", pid, x, y);
                    }
                    break;
                }
                case SERVER_GAME_TIME_SYNC: break;
                default:
                    logf_("packet %d (%zu bytes, ch %d)", type, ev.packet->dataLength, ev.channelID);
                }
                enet_packet_destroy(ev.packet);
            }
        }
        tick++;
        if (state == 4 && !have_target && tick % 30 == 0) {
            // not placed yet: report ready with a harmless packet so the match can start
            Pk p;
            begin(&p, CLIENT_PING);
            send_pk(&p, 1);
        }
        if (state == 4 && have_target) {
            // walk back and forth near the first spawn the server gave us (unknown: stay put)
            float t = tick / 60.0f;
            float xspd = cosf(t) * 4;
            px += xspd;
            Pk p;
            begin(&p, CLIENT_PLAYER_DATA);
            w16(&p, (uint16_t)(int)px); w16(&p, (uint16_t)(int)py);
            w16(&p, f16(xspd)); w16(&p, f16(0));
            w8(&p, 1); w16(&p, 0); w8(&p, (uint8_t)(tick / 8)); w8(&p, xspd < 0 ? (uint8_t)-1 : 1);
            // as the EXE, attack for 1 s every 4 s
            if (exe_id == my_id) w8(&p, (tick % 240) < 60 ? (1 << 4) : 0);
            // as a survivor (Knuckles): full hp, attacking 1 s every 4 s (stuns the EXE on contact)
            else { w8(&p, 100); w8(&p, 0); w16(&p, 0); w8(&p, (tick % 240) < 60 ? (1 << 4) : 0); }
            send_pk(&p, 0);
            if (tick % 60 == 0) { begin(&p, CLIENT_PING); send_pk(&p, 0); }
        }
    }
    enet_peer_disconnect(peer, 0);
    enet_host_flush(host);
    return 0;
}
