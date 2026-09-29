#include "audio.h"

#include <math.h>

#include "loader.h"
#include "sprite.h"
#include <tremor/ivorbisfile.h>

// NDSP channel 0 streams music; 1..23 play effects.
#define MUSIC_CH 0
#define SFX_CH_FIRST 1
#define SFX_CH_LAST 23
#define MUSIC_BUFS 4
#define MUSIC_BUF_FRAMES 4096            // stereo frames per buffer (128 ms at 32 kHz)
#define SFX_CACHE_BYTES (4 * 1024 * 1024)

float audio_sfx_volume = 1, audio_music_volume = 1;

static bool ndsp_ok;

// ------------------------------------------------------------------ effects

typedef struct {
    s16 *data;       // linear memory
    u32 samples, rate;
    u32 last_used;
    bool missing;
} SfxSample;

static SfxSample samples[SND_COUNT];
static u32 cache_bytes, use_clock;

typedef struct {
    int snd;
    ndspWaveBuf buf;
    int handle;
} SfxChannel;

static SfxChannel channels[SFX_CH_LAST + 1];
static int next_handle = 1;

static bool sample_in_use(int snd)
{
    for (int c = SFX_CH_FIRST; c <= SFX_CH_LAST; c++)
        if (channels[c].snd == snd && ndspChnIsPlaying(c)) return true;
    return false;
}

static void evict_for(u32 need)
{
    while (cache_bytes + need > SFX_CACHE_BYTES) {
        int victim = -1;
        for (int i = 0; i < SND_COUNT; i++)
            if (samples[i].data && !sample_in_use(i) && (victim < 0 || samples[i].last_used < samples[victim].last_used))
                victim = i;
        if (victim < 0) return;
        cache_bytes -= samples[victim].samples * 2;
        linearFree(samples[victim].data);
        samples[victim].data = NULL;
    }
}

// "PCM1" u32 rate u32 samples, s16 mono samples. In play the file is read in the background
// (the sound is skipped until it is in memory); at level start and for preloads, at once.
static SfxSample *load_sample(int snd, bool wait)
{
    SfxSample *s = &samples[snd];
    s->last_used = ++use_clock;
    if (s->data) return s;
    if (s->missing) return NULL;
    char path[128];
    snprintf(path, sizeof path, "romfs:/audio/sfx/%s.pcm", SOUND_DEFS[snd].name);
    void *file = NULL;
    size_t size = 0;
    int r;
    if (wait || sprites_loading()) {
        r = LOADER_MISSING;
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            file = n > 0 ? malloc(n) : NULL;
            r = file && fread(file, 1, n, f) == (size_t)n ? LOADER_READY : LOADER_FAILED;
            size = n;
            fclose(f);
        }
    } else {
        r = loader_fetch(path, &file, &size);
    }
    if (r == LOADER_MISSING) s->missing = true;
    if (r != LOADER_READY) {
        if (r != LOADER_PENDING) free(file);
        return NULL;
    }
    const u8 *p = file;
    u32 head[2];
    if (size < 12 || memcmp(p, "PCM1", 4)) {
        free(file);
        s->missing = true;
        return NULL;
    }
    memcpy(head, p + 4, 8);
    u32 bytes = head[1] * 2;
    if (bytes > size - 12) bytes = (u32)(size - 12) & ~1u;
    evict_for(bytes);
    s->data = linearAlloc(bytes > 0 ? bytes : 2);
    if (!s->data) {
        free(file);
        return NULL;
    }
    memcpy(s->data, p + 12, bytes);
    free(file);
    s->samples = bytes / 2;
    s->rate = head[0];
    DSP_FlushDataCache(s->data, bytes);
    cache_bytes += bytes;
    return s;
}

void audio_preload(int snd)
{
    if (ndsp_ok && snd >= 0 && snd < SND_COUNT && SOUND_DEFS[snd].kind == 1) load_sample(snd, true);
}

int audio_play_ex(int snd, float gain, bool loop)
{
    if (!ndsp_ok || snd < 0 || snd >= SND_COUNT) return -1;
    if (SOUND_DEFS[snd].kind == 2) {
        audio_music(snd);
        return -1;
    }
    if (SOUND_DEFS[snd].kind != 1) return -1;
    SfxSample *s = load_sample(snd, false);
    if (!s || !s->samples) return -1;

    int ch = -1;
    for (int c = SFX_CH_FIRST; c <= SFX_CH_LAST; c++)
        if (!ndspChnIsPlaying(c)) { ch = c; break; }
    if (ch < 0) return -1;  // all busy: drop, like a full voice pool

    SfxChannel *sc = &channels[ch];
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, (float)s->rate);
    ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
    float vol = gain * SOUND_DEFS[snd].volume * audio_sfx_volume;
    float mix[12] = { vol, vol };
    ndspChnSetMix(ch, mix);
    memset(&sc->buf, 0, sizeof sc->buf);
    sc->buf.data_vaddr = s->data;
    sc->buf.nsamples = s->samples;
    sc->buf.looping = loop;
    sc->snd = snd;
    sc->handle = next_handle++;
    ndspChnWaveBufAdd(ch, &sc->buf);
    return sc->handle;
}

int audio_play(int snd) { return audio_play_ex(snd, 1, false); }

void audio_set_gain(int handle, float gain)
{
    if (!ndsp_ok || handle <= 0) return;
    for (int c = SFX_CH_FIRST; c <= SFX_CH_LAST; c++) {
        if (channels[c].handle != handle) continue;
        float vol = gain * SOUND_DEFS[channels[c].snd].volume * audio_sfx_volume;
        float mix[12] = { vol, vol };
        ndspChnSetMix(c, mix);
    }
}

void audio_stop(int handle)
{
    for (int c = SFX_CH_FIRST; c <= SFX_CH_LAST; c++)
        if (channels[c].handle == handle && handle > 0) ndspChnWaveBufClear(c);
}

void audio_stop_sound(int snd)
{
    if (snd >= 0 && snd < SND_COUNT && SOUND_DEFS[snd].kind == 2) {
        if (audio_music_current() == snd) audio_music_stop();
        return;
    }
    for (int c = SFX_CH_FIRST; c <= SFX_CH_LAST; c++)
        if (channels[c].snd == snd) ndspChnWaveBufClear(c);
}

bool audio_is_playing(int snd)
{
    if (snd >= 0 && snd < SND_COUNT && SOUND_DEFS[snd].kind == 2) return audio_music_current() == snd;
    return sample_in_use(snd);
}

// ------------------------------------------------------------------ music

static Thread music_thread;
static LightEvent music_event;
static LightLock music_lock;
static volatile bool music_quit;
static s16 *music_pcm;                  // MUSIC_BUFS * MUSIC_BUF_FRAMES * 2 samples, linear
static ndspWaveBuf music_bufs[MUSIC_BUFS];
static OggVorbis_File music_vf;
static bool music_open;
static int music_snd = -1;
static float music_gain = 1;
static float music_fade = 1;            // crossfade factor (audio_music_request)
static int music_req = -1;              // track to open on the music thread
static ogg_int64_t music_req_pos;

static void music_close_locked(void)
{
    if (music_open) ov_clear(&music_vf);  // closes the FILE
    music_open = false;
    music_snd = -1;
    ndspChnWaveBufClear(MUSIC_CH);
}

// Fill one buffer from the stream, looping at the end. Returns false if the stream failed.
static bool music_fill(ndspWaveBuf *wb)
{
    char *out = (char *)wb->data_pcm16;
    int want = MUSIC_BUF_FRAMES * 4, got = 0, section, restarts = 0, errors = 0;
    while (got < want) {
        long n = ov_read(&music_vf, out + got, want - got, &section);
        if (n == 0) {  // end of track: loop
            if (ov_pcm_seek(&music_vf, 0) != 0 || ++restarts > 2) break;
            continue;
        }
        if (n < 0) {  // hole in the data (give up on a stream that keeps failing)
            if (++errors > 8) break;
            continue;
        }
        got += n;
    }
    if (got == 0) return false;
    wb->nsamples = got / 4;
    DSP_FlushDataCache(wb->data_pcm16, got);
    ndspChnWaveBufAdd(MUSIC_CH, wb);
    return true;
}

static void music_open_locked(int snd, ogg_int64_t pos);

static void music_worker(void *arg)
{
    (void)arg;
    while (!music_quit) {
        LightEvent_Wait(&music_event);
        LightLock_Lock(&music_lock);
        if (music_req >= 0) {
            int snd = music_req;
            music_req = -1;
            music_open_locked(snd, music_req_pos);
        }
        if (music_open) {
            for (int i = 0; i < MUSIC_BUFS; i++) {
                ndspWaveBuf *wb = &music_bufs[i];
                if (wb->status == NDSP_WBUF_DONE || wb->status == NDSP_WBUF_FREE) {
                    if (!music_fill(wb)) {
                        music_close_locked();
                        break;
                    }
                }
            }
        }
        LightLock_Unlock(&music_lock);
    }
}

static void ndsp_callback(void *arg)
{
    (void)arg;
    LightEvent_Signal(&music_event);
}

static void music_apply_gain(void)
{
    float vol = music_gain * music_fade * audio_music_volume * (music_snd >= 0 ? SOUND_DEFS[music_snd].volume : 1);
    float mix[12] = { vol, vol };
    ndspChnSetMix(MUSIC_CH, mix);
}

static void music_start(int snd, ogg_int64_t pos);

void audio_music(int snd) { music_start(snd, 0); }

// A track change is only a request here; the music thread opens the file and decodes the
// first buffers (a blocking read would freeze the game on hardware).
static void music_start(int snd, ogg_int64_t pos)
{
    if (!ndsp_ok || snd < 0 || snd >= SND_COUNT || SOUND_DEFS[snd].kind != 2) return;
    LightLock_Lock(&music_lock);
    music_close_locked();
    music_req = snd;
    music_req_pos = pos;
    music_snd = snd;
    music_gain = 1;
    LightLock_Unlock(&music_lock);
    LightEvent_Signal(&music_event);
}

// music thread, lock held
static void music_open_locked(int snd, ogg_int64_t pos)
{
    char path[128];
    snprintf(path, sizeof path, "romfs:/audio/music/%s.ogg", SOUND_DEFS[snd].name);
    FILE *f = fopen(path, "rb");
    if (f && ov_open(f, &music_vf, NULL, 0) == 0) {
        music_open = true;
        music_snd = snd;
        vorbis_info *vi = ov_info(&music_vf, -1);
        ndspChnReset(MUSIC_CH);
        ndspChnSetInterp(MUSIC_CH, NDSP_INTERP_LINEAR);
        ndspChnSetRate(MUSIC_CH, vi ? (float)vi->rate : 32000.0f);
        ndspChnSetFormat(MUSIC_CH, vi && vi->channels == 1 ? NDSP_FORMAT_MONO_PCM16 : NDSP_FORMAT_STEREO_PCM16);
        if (pos > 0) {
            ogg_int64_t len = ov_pcm_total(&music_vf, -1);
            if (len > 0) ov_pcm_seek(&music_vf, pos % len);
        }
        music_apply_gain();
        for (int i = 0; i < MUSIC_BUFS; i++) {
            memset(&music_bufs[i], 0, sizeof music_bufs[i]);
            music_bufs[i].data_pcm16 = music_pcm + i * MUSIC_BUF_FRAMES * 2;
            if (!music_fill(&music_bufs[i])) break;
        }
    } else {
        if (f) fclose(f);
        music_snd = snd;  // stays "current": not asked for again every frame
        dbg_log("audio: cannot open %s", path);
    }
}

void audio_music_stop(void)
{
    if (!ndsp_ok) return;
    LightLock_Lock(&music_lock);
    music_close_locked();
    music_req = -1;
    LightLock_Unlock(&music_lock);
}

void audio_music_gain(float gain)
{
    music_gain = gain;
    if (ndsp_ok) music_apply_gain();
}

int audio_music_current(void) { return music_snd; }

// Tracks made to play in sync, with the GML fade time
static int music_pair_ms(int a, int b)
{
    if ((a == SND_MUS_ACT9 && b == SND_MUS_ACT9_CHASE) || (a == SND_MUS_ACT9_CHASE && b == SND_MUS_ACT9)) return 1000;
    if ((a == SND_MUS_DOTDOTDOT && b == SND_MUS_DOTDOTDOT2) || (a == SND_MUS_DOTDOTDOT2 && b == SND_MUS_DOTDOTDOT))
        return 2000;
    return 0;
}

static int fade_to = -1;       // track waiting for the fade out
static float fade_step;         // per frame

void audio_music_request(int snd)
{
    if (!ndsp_ok) return;
    if (snd == -2) snd = fade_to >= 0 ? fade_to : music_snd;
    if (snd == -1) {
        fade_to = -1;
        music_fade = 1;
        if (music_snd >= 0) audio_music_stop();
        return;
    }
    if (fade_to >= 0 && snd != fade_to && !music_pair_ms(music_snd, snd)) fade_to = -1;
    if (snd == music_snd && fade_to < 0) {
        // fading back in (or already there)
        if (music_fade < 1) {
            music_fade = fminf(1, music_fade + fade_step);
            music_apply_gain();
        }
        return;
    }
    int ms = music_pair_ms(music_snd, snd);
    if (!ms) {
        fade_to = -1;
        music_fade = 1;
        music_start(snd, 0);
        return;
    }
    // fade out half the time, then swap at the same position and fade in the other half
    fade_to = snd;
    fade_step = 1.0f / (ms / 2 * 60 / 1000.0f);
    music_fade = fmaxf(0, music_fade - fade_step);
    music_apply_gain();
    if (music_fade <= 0) {
        LightLock_Lock(&music_lock);
        ogg_int64_t pos = music_open ? ov_pcm_tell(&music_vf) : 0;
        // minus what is decoded but not heard yet
        for (int i = 0; i < MUSIC_BUFS; i++)
            if (music_bufs[i].status == NDSP_WBUF_QUEUED || music_bufs[i].status == NDSP_WBUF_PLAYING)
                pos -= music_bufs[i].nsamples;
        pos += ndspChnGetSamplePos(MUSIC_CH);
        if (pos < 0) pos = 0;
        LightLock_Unlock(&music_lock);
        fade_to = -1;
        music_start(snd, pos);
        music_apply_gain();
    }
}

void audio_stop_all(void)
{
    if (!ndsp_ok) return;
    audio_music_stop();
    for (int c = SFX_CH_FIRST; c <= SFX_CH_LAST; c++) ndspChnWaveBufClear(c);
}

// ------------------------------------------------------------------ setup

bool audio_available(void) { return ndsp_ok; }

void audio_apply_volume(int music, int sfx)
{
    audio_music_volume = music / 10.0f;
    audio_sfx_volume = sfx / 10.0f;
    if (ndsp_ok) music_apply_gain();
}

void audio_init(void)
{
    // Needs the DSP firmware (sdmc:/3ds/dspfirm.cdc on hardware); without it the game is silent.
    if (R_FAILED(ndspInit())) {
        dbg_log("audio: ndspInit failed (missing dspfirm.cdc?)");
        return;
    }
    ndsp_ok = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    music_pcm = linearAlloc(MUSIC_BUFS * MUSIC_BUF_FRAMES * 4);
    LightEvent_Init(&music_event, RESET_ONESHOT);
    LightLock_Init(&music_lock);
    ndspSetCallback(ndsp_callback, NULL);
    // The decoder runs on the system core when available (New 3DS core 2 otherwise core 1).
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    APT_SetAppCpuTimeLimit(30);
    music_thread = threadCreate(music_worker, NULL, 32 * 1024, prio - 1, 1, false);
    if (!music_thread) music_thread = threadCreate(music_worker, NULL, 32 * 1024, prio - 1, -2, false);
}

void audio_exit(void)
{
    if (!ndsp_ok) return;
    audio_stop_all();
    music_quit = true;
    LightEvent_Signal(&music_event);
    if (music_thread) {
        threadJoin(music_thread, U64_MAX);
        threadFree(music_thread);
    }
    for (int i = 0; i < SND_COUNT; i++)
        if (samples[i].data) linearFree(samples[i].data);
    linearFree(music_pcm);
    ndspExit();
    ndsp_ok = false;
}
