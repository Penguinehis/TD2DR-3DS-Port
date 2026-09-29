#pragma once
// Client for the Sonic.exe: The Disaster 2D Remake server protocol (port of obj_netclient and
// the net_* scripts). One ENet peer, 2 channels: 0 reliable, 1 unreliable.

#include "common.h"
#include "net_packets.h"

#define BUILD_VER 1101
#define NET_DEFAULT_PORT 8606
#define NET_MAX_PLAYERS 32
#define NET_CHAT_LINES 8

// Client states (STATE_* in net_packets.gml, plus connection states)
typedef enum {
    NET_OFF,         // not connected
    NET_CONNECTING,  // ENet handshake in progress
    NET_WAITING,     // STATE_PENDING: connected, waiting for the running match to end
    NET_LOBBY,       // STATE_LOBBY
    NET_VOTE,        // STATE_VOTE
    NET_CHARSELECT,  // STATE_CHARSELECT
    NET_GAME,        // STATE_GAME
    NET_RESULTS,     // STATE_RESULTS
    NET_ERROR,       // disconnected with an error (see net.error_*)
} NetState;

// Disconnect reasons (DisconnectReason in betterserver/include/Lib.h; also the frames of
// spr_menu_error).
enum {
    DR_FAILEDTOCONNECT, DR_KICKEDBYHOST, DR_BANNEDBYHOST, DR_VERMISMATCH, DR_SERVERTIMEOUT,
    DR_PACKETSNOTRECV, DR_GAMESTARTED, DR_AFKTIMEOUT, DR_LOBBYFULL, DR_RATELIMITED, DR_SHUTDOWN,
    DR_IPINUSE, DR_DONTREPORT = 254, DR_OTHER = 255,
};

typedef struct {
    bool used;
    u16 id;
    char nickname[32];
    u8 icon;          // lobby icon
    s8 pet;
    bool ready;
    s8 character;     // -1 none, CHARACTER_EXE, CHARACTER_TAILS...
    s8 exe_character; // -1 none, EXE_ORIGINAL...
    // waiting room (a match is running): the player's role in it
    bool wait_in_game, wait_exe;
    s8 wait_char;
} NetPlayer;

typedef struct {
    char sender[32];
    char text[128];
} ChatLine;

typedef struct {
    NetState state;
    u16 id;                     // our id (nid)
    char host[64];
    u16 base_port, port;
    s32 want_lobby;
    int connect_timer;          // frames left for the ENet handshake

    int error_code;             // DR_*
    char error_text[128];

    NetPlayer players[NET_MAX_PLAYERS];
    ChatLine chat[NET_CHAT_LINES];
    int chat_count;

    // lobby
    bool lobby_ready;           // SERVER_LOBBY_CORRECT received: player list complete
    bool my_ready;
    bool counting;              // countdown running
    u8 countdown;
    u8 exe_chance;
    // waiting room
    bool wait_clock;
    u16 wait_timer;             // frames left in the running match

    // vote
    u8 vote_maps[3];
    u8 vote_counts[3];
    u8 vote_timer;
    s8 my_vote;

    // character select
    int exe_id;                 // player id of the EXE
    int map;                    // level index (global.levels)
    u8 char_timer;
    s8 my_character;            // global.character (-1 until chosen; CHARACTER_EXE if EXE)
    s8 my_exe_character;        // global.exeCharacter
    bool av_characters[7];      // avCharacters

    // counters the UI can watch to play sounds / animate (incremented on events)
    u32 ev_join, ev_leave, ev_chat, ev_ready, ev_clock, ev_nono;
} NetClient;

extern NetClient net;

bool net_init(void);
void net_exit(void);

// "host" or "host:port". want_lobby is the port of a specific lobby, or -1.
bool net_connect(const char *address, s32 want_lobby);
void net_disconnect(void);
// Service ENet and handle packets. Call once per frame.
void net_poll(void);

NetPlayer *net_player(u16 id);
int net_player_count(void);

// Actions
void net_set_ready(bool ready);
void net_send_chat(const char *msg);
void net_vote(int slot);                 // 0..2
void net_request_character(int character); // CHARACTER_TAILS.. (survivor) or EXE_* + 1 (EXE)

// Packet writer
typedef struct {
    u8 buf[256];
    int len;
} NetPacket;

void pkt_begin(NetPacket *p, PacketType type);
void pkt_u8(NetPacket *p, u8 v);
void pkt_u16(NetPacket *p, u16 v);
void pkt_u32(NetPacket *p, u32 v);
void pkt_u64(NetPacket *p, u64 v);
void pkt_f32(NetPacket *p, float v);
void pkt_f16(NetPacket *p, float v);
void pkt_str(NetPacket *p, const char *s);
void net_send(const NetPacket *p, bool reliable);

// Packet reader (bounds-checked: reads past the end return 0 and set bad)
typedef struct {
    const u8 *data;
    int len, pos;
    bool bad;
} NetReader;

u8 rd_u8(NetReader *r);
u16 rd_u16(NetReader *r);
u32 rd_u32(NetReader *r);
u64 rd_u64(NetReader *r);
float rd_f32(NetReader *r);
double rd_f64(NetReader *r);
float rd_f16(NetReader *r);
const char *rd_str(NetReader *r);  // pointer into a static rotating buffer

// Hook for the game state (M4): unhandled reliable/unreliable packets in NET_GAME.
typedef void (*NetGameHandler)(PacketType type, bool passthrough, NetReader *r, bool reliable);
void net_set_game_handler(NetGameHandler h);
