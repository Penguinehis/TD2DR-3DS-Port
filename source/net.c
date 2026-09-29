#include "net.h"

#include <malloc.h>

#include "enet/enet.h"
#include "player.h"
#include "settings.h"

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define CONNECT_TIMEOUT (60 * 10)

NetClient net;

static u32 *soc_buffer;
static bool soc_ready;
static ENetHost *host;
static ENetPeer *peer;
static NetGameHandler game_handler;

// ------------------------------------------------------------------ packets

void pkt_begin(NetPacket *p, PacketType type)
{
    p->len = 0;
    pkt_u8(p, 0);  // passthrough
    pkt_u8(p, (u8)type);
}

static void pkt_bytes(NetPacket *p, const void *v, int n)
{
    if (p->len + n > (int)sizeof p->buf) return;
    memcpy(p->buf + p->len, v, n);
    p->len += n;
}

void pkt_u8(NetPacket *p, u8 v) { pkt_bytes(p, &v, 1); }
void pkt_u16(NetPacket *p, u16 v) { pkt_bytes(p, &v, 2); }
void pkt_u32(NetPacket *p, u32 v) { pkt_bytes(p, &v, 4); }
void pkt_u64(NetPacket *p, u64 v) { pkt_bytes(p, &v, 8); }
void pkt_f32(NetPacket *p, float v) { pkt_bytes(p, &v, 4); }
void pkt_str(NetPacket *p, const char *s) { pkt_bytes(p, s, strlen(s) + 1); }

// IEEE 754 half precision (GameMaker buffer_f16)
static u16 float_to_half(float f)
{
    u32 x;
    memcpy(&x, &f, 4);
    u32 sign = (x >> 16) & 0x8000;
    int exp = (int)((x >> 23) & 0xFF) - 127 + 15;
    u32 mant = x & 0x7FFFFF;
    if (((x >> 23) & 0xFF) == 0xFF) return sign | 0x7C00 | (mant ? 0x200 : 0);
    if (exp >= 31) return sign | 0x7C00;
    if (exp <= 0) {
        if (exp < -10) return sign;
        mant |= 0x800000;
        return sign | (mant >> (14 - exp));
    }
    return sign | (exp << 10) | (mant >> 13);
}

static float half_to_float(u16 h)
{
    u32 sign = (u32)(h & 0x8000) << 16;
    int exp = (h >> 10) & 0x1F;
    u32 mant = h & 0x3FF;
    u32 x;
    if (exp == 0) {
        if (!mant) x = sign;
        else {
            exp = 1;
            while (!(mant & 0x400)) { mant <<= 1; exp--; }
            mant &= 0x3FF;
            x = sign | ((u32)(exp - 15 + 127) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        x = sign | 0x7F800000 | (mant << 13);
    } else {
        x = sign | ((u32)(exp - 15 + 127) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &x, 4);
    return f;
}

void pkt_f16(NetPacket *p, float v) { pkt_u16(p, float_to_half(v)); }

static bool rd_take(NetReader *r, void *out, int n)
{
    if (r->bad || r->pos + n > r->len) {
        r->bad = true;
        memset(out, 0, n);
        return false;
    }
    memcpy(out, r->data + r->pos, n);
    r->pos += n;
    return true;
}

u8 rd_u8(NetReader *r) { u8 v; rd_take(r, &v, 1); return v; }
u16 rd_u16(NetReader *r) { u16 v; rd_take(r, &v, 2); return v; }
u32 rd_u32(NetReader *r) { u32 v; rd_take(r, &v, 4); return v; }
u64 rd_u64(NetReader *r) { u64 v; rd_take(r, &v, 8); return v; }
float rd_f32(NetReader *r) { float v; rd_take(r, &v, 4); return v; }
double rd_f64(NetReader *r) { double v; rd_take(r, &v, 8); return v; }
float rd_f16(NetReader *r) { return half_to_float(rd_u16(r)); }

const char *rd_str(NetReader *r)
{
    static char bufs[4][256];
    static int next;
    char *out = bufs[next++ & 3];
    int n = 0;
    while (1) {
        if (r->pos >= r->len) { r->bad = true; break; }
        char c = (char)r->data[r->pos++];
        if (!c) break;
        if (n < 255) out[n++] = c;
    }
    out[n] = 0;
    return out;
}

void net_send(const NetPacket *p, bool reliable)
{
    if (!peer || net.state == NET_OFF || net.state == NET_CONNECTING || net.state == NET_ERROR) return;
    ENetPacket *pk = enet_packet_create(p->buf, p->len, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (enet_peer_send(peer, reliable ? 0 : 1, pk) != 0) enet_packet_destroy(pk);
}

// ------------------------------------------------------------------ setup

bool net_init(void)
{
    soc_buffer = memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (!soc_buffer) return false;
    Result rc = socInit(soc_buffer, SOC_BUFFERSIZE);
    if (R_FAILED(rc)) {
        dbg_log("socInit failed: %08lx", rc);
        free(soc_buffer);
        soc_buffer = NULL;
        return false;
    }
    soc_ready = true;
    enet_initialize();
    return true;
}

void net_exit(void)
{
    net_disconnect();
    if (host) enet_host_destroy(host);
    host = NULL;
    enet_deinitialize();
    if (soc_ready) socExit();
    soc_ready = false;
    free(soc_buffer);
    soc_buffer = NULL;
}

static void reset_session(void)
{
    memset(net.players, 0, sizeof net.players);
    net.chat_count = 0;
    net.lobby_ready = false;
    net.my_ready = false;
    net.counting = false;
    net.countdown = 0;
    net.wait_clock = false;
    net.exe_id = -1;
    net.map = -1;
    net.my_character = -1;
    net.my_exe_character = -1;
    net.my_vote = -1;
    for (int i = 0; i < 7; i++) net.av_characters[i] = i != CHARACTER_EXE;
}

static void set_error(int code, const char *text)
{
    net.state = NET_ERROR;
    net.error_code = code;
    snprintf(net.error_text, sizeof net.error_text, "%s", text ? text : "");
    dbg_log("net: error %d %s", code, net.error_text);
}

bool net_connect(const char *address, s32 want_lobby)
{
    if (!soc_ready) {
        set_error(DR_FAILEDTOCONNECT, "Wi-Fi sockets unavailable");
        return false;
    }
    net_disconnect();

    // net_join: "host:port" (the port is remembered for reconnects and lobby changes)
    char hostname[64];
    snprintf(hostname, sizeof hostname, "%s", address);
    net.base_port = NET_DEFAULT_PORT;
    char *colon = strrchr(hostname, ':');
    if (colon) {
        int port = atoi(colon + 1);
        if (port > 0 && port < 65536) {
            net.base_port = port;
            *colon = 0;
        }
    }
    snprintf(net.host, sizeof net.host, "%s", hostname);
    net.port = want_lobby == -1 ? net.base_port : (u16)want_lobby;
    net.want_lobby = want_lobby;

    if (!host) {
        host = enet_host_create(NULL, 1, 2, 5000000, 5000000);
        if (!host) {
            set_error(DR_FAILEDTOCONNECT, "Could not create the network host");
            return false;
        }
    }
    ENetAddress addr;
    if (enet_address_set_host(&addr, net.host) != 0) {
        set_error(DR_FAILEDTOCONNECT, "Could not resolve the server address");
        return false;
    }
    addr.port = net.port;
    peer = enet_host_connect(host, &addr, 2, 0);
    if (!peer) {
        set_error(DR_FAILEDTOCONNECT, "Could not start the connection");
        return false;
    }
    net.state = NET_CONNECTING;
    net.connect_timer = CONNECT_TIMEOUT;
    dbg_log("net: connecting to %s:%u", net.host, net.port);
    return true;
}

void net_disconnect(void)
{
    if (peer) {
        enet_peer_disconnect_now(peer, 0);
        peer = NULL;
    }
    if (host) enet_host_flush(host);
    net.state = NET_OFF;
}

NetPlayer *net_player(u16 id)
{
    for (int i = 0; i < NET_MAX_PLAYERS; i++)
        if (net.players[i].used && net.players[i].id == id) return &net.players[i];
    return NULL;
}

static NetPlayer *add_player(u16 id)
{
    NetPlayer *p = net_player(id);
    if (!p)
        for (int i = 0; i < NET_MAX_PLAYERS; i++)
            if (!net.players[i].used) { p = &net.players[i]; break; }
    if (!p) return NULL;
    memset(p, 0, sizeof *p);
    p->used = true;
    p->id = id;
    p->pet = -1;
    p->character = -1;
    p->exe_character = -1;
    return p;
}

int net_player_count(void)
{
    int n = 0;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) n += net.players[i].used;
    return n;
}

static void add_chat(const char *sender, const char *msg)
{
    if (net.chat_count == NET_CHAT_LINES) {
        memmove(&net.chat[0], &net.chat[1], sizeof(ChatLine) * (NET_CHAT_LINES - 1));
        net.chat_count--;
    }
    ChatLine *c = &net.chat[net.chat_count++];
    snprintf(c->sender, sizeof c->sender, "%s", sender);
    snprintf(c->text, sizeof c->text, "%s", msg);
    net.ev_chat++;
}

// ------------------------------------------------------------------ actions

static void send_identity(void)
{
    // net_identity
    NetPacket p;
    pkt_begin(&p, IDENTITY);
    pkt_u16(&p, BUILD_VER);
    pkt_u32(&p, (u32)net.want_lobby);
    pkt_str(&p, settings.nickname);
    char udid[64];
    snprintf(udid, sizeof udid, "mod-3ds-%016llx", settings_console_id());
    pkt_str(&p, udid);
    pkt_u8(&p, (u8)settings.lobby_icon);
    pkt_u8(&p, (u8)settings.pet);
    pkt_u64(&p, 0);
    pkt_u64(&p, 0);
    net_send(&p, true);
}

static void request_lobby_players(void)
{
    NetPacket p;
    pkt_begin(&p, CLIENT_LOBBY_PLAYERS_REQUEST);
    net_send(&p, true);
}

void net_set_ready(bool ready)
{
    net.my_ready = ready;
    NetPacket p;
    pkt_begin(&p, CLIENT_LOBBY_READY_STATE);
    pkt_u8(&p, ready);
    net_send(&p, true);
}

void net_send_chat(const char *msg)
{
    char lower[96];
    int n = 0;
    for (; msg[n] && n < 80; n++) lower[n] = (msg[n] >= 'A' && msg[n] <= 'Z') ? msg[n] + 32 : msg[n];
    lower[n] = 0;
    NetPacket p;
    pkt_begin(&p, CLIENT_CHAT_MESSAGE);
    p.buf[0] = 1;  // the PC client sends chat with passthrough set
    pkt_u16(&p, net.id);
    pkt_str(&p, lower);
    net_send(&p, true);
    add_chat(settings.nickname, lower);
}

void net_vote(int slot)
{
    if (net.my_vote >= 0 || slot < 0 || slot > 2) return;
    net.my_vote = slot;
    NetPacket p;
    pkt_begin(&p, CLIENT_VOTE_REQUEST);
    pkt_u8(&p, slot);
    net_send(&p, true);
}

void net_request_character(int character)
{
    NetPacket p;
    if (net.exe_id == net.id) {
        pkt_begin(&p, CLIENT_REQUEST_EXECHARACTER);
    } else {
        if (!net.av_characters[character]) { net.ev_nono++; return; }
        pkt_begin(&p, CLIENT_REQUEST_CHARACTER);
    }
    pkt_u8(&p, character);
    net_send(&p, true);
}

void net_set_game_handler(NetGameHandler h) { game_handler = h; }

// ------------------------------------------------------------------ packet handling

static void go_lobby(void)
{
    net.state = NET_LOBBY;
    net.lobby_ready = false;
    net.my_ready = false;
    net.counting = false;
    request_lobby_players();
}

// net_state_pending
static void handle_pending(PacketType type, bool pass, NetReader *r)
{
    if (pass && type != SERVER_PREIDENTITY) return;
    switch (type) {
    case SERVER_PREIDENTITY:
        send_identity();
        break;
    case SERVER_IDENTITY_RESPONSE: {
        bool lobby = rd_u8(r);
        net.id = rd_u16(r);
        dbg_log("net: identity ok, id %u, %s", net.id, lobby ? "lobby" : "match running");
        if (lobby) go_lobby();
        break;
    }
    case SERVER_GAME_TIME_SYNC:
        net.wait_clock = true;
        net.wait_timer = rd_u16(r);
        break;
    case SERVER_WAITING_PLAYER_INFO: {
        bool in_game = rd_u8(r);
        u16 id = rd_u16(r);
        const char *nick = rd_str(r);
        NetPlayer *p = add_player(id);
        if (!p) break;
        snprintf(p->nickname, sizeof p->nickname, "%s", nick);
        p->wait_in_game = in_game;
        if (in_game) {
            p->wait_exe = rd_u8(r);
            p->wait_char = (s8)rd_u8(r);
        } else {
            p->icon = rd_u8(r);
        }
        net.ev_join++;
        break;
    }
    case CLIENT_CHAT_MESSAGE: {
        u16 id = rd_u16(r);
        const char *msg = rd_str(r);
        if (id == 0) add_chat("(server)", msg);
        else if (net_player(id)) add_chat(net_player(id)->nickname, msg);
        break;
    }
    case SERVER_RESULTS:
        net.map = rd_u8(r);
        net.state = NET_RESULTS;
        break;
    default:
        break;
    }
}

// net_state_lobby
static void handle_lobby(PacketType type, bool pass, NetReader *r)
{
    if (pass) return;
    switch (type) {
    case SERVER_PLAYER_JOINED: {
        u16 id = rd_u16(r);
        const char *name = rd_str(r);
        NetPlayer *p = add_player(id);
        if (!p) break;
        snprintf(p->nickname, sizeof p->nickname, "%s", name);
        p->icon = rd_u8(r);
        p->pet = (s8)rd_u8(r);
        net.ev_join++;
        break;
    }
    case SERVER_LOBBY_PLAYER: {
        u16 id = rd_u16(r);
        bool ready = rd_u8(r);
        const char *name = rd_str(r);
        NetPlayer *p = add_player(id);
        if (!p) break;
        snprintf(p->nickname, sizeof p->nickname, "%s", name);
        p->ready = ready;
        p->icon = rd_u8(r);
        p->pet = (s8)rd_u8(r);
        break;
    }
    case SERVER_LOBBY_CORRECT:
        net.lobby_ready = true;
        break;
    case SERVER_LOBBY_READY_STATE: {
        u16 id = rd_u16(r);
        bool st = rd_u8(r);
        if (id == net.id) net.my_ready = st;
        NetPlayer *p = net_player(id);
        if (p) p->ready = st;
        net.ev_ready++;
        break;
    }
    case CLIENT_CHAT_MESSAGE: {
        u16 id = rd_u16(r);
        const char *msg = rd_str(r);
        if (id == 0) add_chat("(server)", msg);
        else if (net_player(id)) add_chat(net_player(id)->nickname, msg);
        break;
    }
    case SERVER_VOTE_MAPS:
        for (int i = 0; i < 3; i++) net.vote_maps[i] = rd_u8(r);
        memset(net.vote_counts, 0, sizeof net.vote_counts);
        net.my_vote = -1;
        net.vote_timer = 30;
        net.state = NET_VOTE;
        break;
    default:
        break;
    }
}

// SERVER_LOBBY_EXE, handled in both the lobby (".map" command) and the vote
static void start_charselect(NetReader *r)
{
    u16 who = rd_u16(r);
    net.map = rd_u16(r);
    net.exe_id = who;
    net.my_character = -1;
    net.my_exe_character = -1;
    for (int i = 0; i < 7; i++) net.av_characters[i] = i != CHARACTER_EXE;
    for (int i = 0; i < NET_MAX_PLAYERS; i++) {
        net.players[i].character = -1;
        net.players[i].exe_character = -1;
    }
    if (who == net.id) net.my_character = CHARACTER_EXE;
    else if (net_player(who)) net_player(who)->character = CHARACTER_EXE;
    net.char_timer = 30;
    net.state = NET_CHARSELECT;
    dbg_log("net: charselect, exe %u, map %d", who, net.map);
}

// net_state_vote
static void handle_vote(PacketType type, bool pass, NetReader *r)
{
    if (pass) return;
    switch (type) {
    case SERVER_VOTE_TIME_SYNC:
        net.vote_timer = rd_u8(r);
        if (net.vote_timer <= 3) net.ev_clock++;
        break;
    case SERVER_VOTE_SET:
        for (int i = 0; i < 3; i++) net.vote_counts[i] = rd_u8(r);
        break;
    default:
        break;
    }
}

// net_state_charselect
static void handle_charselect(PacketType type, bool pass, NetReader *r)
{
    if (pass) return;
    switch (type) {
    case SERVER_CHAR_TIME_SYNC:
        net.char_timer = rd_u8(r);
        if (net.char_timer <= 3) net.ev_clock++;
        break;
    case SERVER_LOBBY_GAME_START:
        net.state = NET_GAME;
        dbg_log("net: game start");
        break;
    case SERVER_LOBBY_CHARACTER_RESPONSE: {
        u8 ch = rd_u8(r);
        bool ok = rd_u8(r);
        if (ok) {
            net.my_character = ch;
            net.ev_clock++;
        } else {
            if (ch < 7) net.av_characters[ch] = false;
            net.ev_nono++;
        }
        break;
    }
    case SERVER_LOBBY_EXECHARACTER_RESPONSE:
        net.my_character = CHARACTER_EXE;
        net.my_exe_character = rd_u8(r);
        net.ev_clock++;
        break;
    case SERVER_LOBBY_CHARACTER_CHANGE: {
        u16 id = rd_u16(r);
        u8 ch = rd_u8(r);
        NetPlayer *p = net_player(id);
        if (!p) break;
        if (net.exe_id == id) {
            p->character = CHARACTER_EXE;
            p->exe_character = ch;
        } else {
            p->character = ch;
            if (ch < 7) net.av_characters[ch] = false;
        }
        net.ev_clock++;
        break;
    }
    default:
        break;
    }
}

// net_tcpprocess (state-independent packets first)
static void handle_reliable(NetReader *r)
{
    bool pass = rd_u8(r);
    PacketType type = rd_u8(r);
    if (r->bad) return;

    switch (type) {
    case SERVER_PLAYER_LEFT: {
        if (pass) return;
        u16 id = rd_u16(r);
        NetPlayer *p = net_player(id);
        if (!p) return;
        if (net.state == NET_CHARSELECT && p->character > 0 && p->character < 7)
            net.av_characters[p->character] = true;
        if (game_handler && net.state == NET_GAME) game_handler(type, pass, r, true);
        p->used = false;
        net.ev_leave++;
        return;
    }
    case SERVER_PLAYER_FORCE_DISCONNECT: {
        if (pass) return;
        const char *msg = rd_str(r);
        net_disconnect();
        set_error(DR_OTHER, msg);
        return;
    }
    case SERVER_LOBBY_COUNTDOWN:
        if (pass || (net.state != NET_LOBBY && net.state != NET_CHARSELECT)) return;
        net.counting = rd_u8(r);
        net.countdown = rd_u8(r);
        net.ev_clock++;
        return;
    case SERVER_LOBBY_CHANGELOBBY: {
        s32 lobby = (s32)rd_u32(r);
        char address[80];
        snprintf(address, sizeof address, "%s:%u", net.host, net.base_port);
        dbg_log("net: moved to lobby port %ld", lobby);
        net_connect(address, lobby);
        return;
    }
    case SERVER_LOBBY_EXE_CHANCE:
        if (pass) return;
        net.exe_chance = rd_u8(r);
        return;
    case SERVER_GAME_BACK_TO_LOBBY:
        if (pass) return;
        for (int i = 0; i < NET_MAX_PLAYERS; i++) net.players[i].used = false;
        net.exe_id = -1;
        net.my_character = net.my_exe_character = -1;
        if (game_handler && net.state == NET_GAME) game_handler(type, pass, r, true);
        go_lobby();
        return;
    case SERVER_HEARTBEAT:
        return;
    default:
        break;
    }

    switch (net.state) {
    case NET_WAITING: handle_pending(type, pass, r); break;
    case NET_LOBBY:
        if (type == SERVER_LOBBY_EXE && !pass) start_charselect(r);
        else handle_lobby(type, pass, r);
        break;
    case NET_VOTE:
        if (type == SERVER_LOBBY_EXE && !pass) start_charselect(r);
        else handle_vote(type, pass, r);
        break;
    case NET_CHARSELECT: handle_charselect(type, pass, r); break;
    case NET_GAME:
    case NET_RESULTS:
        if (game_handler) game_handler(type, pass, r, true);
        break;
    default: break;
    }
}

void net_poll(void)
{
    if (!host) return;
    if (net.state == NET_CONNECTING && --net.connect_timer <= 0) {
        net_disconnect();
        set_error(DR_FAILEDTOCONNECT, "The server did not answer");
        return;
    }
    ENetEvent ev;
    while (host && enet_host_service(host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            dbg_log("net: connected");
            reset_session();
            net.state = NET_WAITING;
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            peer = NULL;
            if (net.state != NET_OFF) set_error((int)ev.data, NULL);
            break;
        case ENET_EVENT_TYPE_RECEIVE: {
            NetReader r = { ev.packet->data, (int)ev.packet->dataLength, 0, false };
            if (ev.channelID == 0) {
                handle_reliable(&r);
            } else if (net.state == NET_GAME && game_handler) {
                bool pass = rd_u8(&r);
                PacketType type = rd_u8(&r);
                if (!r.bad) game_handler(type, pass, &r, false);
            }
            enet_packet_destroy(ev.packet);
            break;
        }
        default:
            break;
        }
    }
    if (host) enet_host_flush(host);
}
