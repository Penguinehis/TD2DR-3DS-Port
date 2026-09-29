#pragma once
// Audio through NDSP: streamed Ogg Vorbis music (tremor, decoded on a worker thread) and
// PCM16 sound effects cached in linear memory. Sound ids are SND_* from gen/sounds.h.
// Equivalents: audio_play_sound -> audio_play, scr_play_music -> audio_music,
// scr_stop_music -> audio_music_stop, audio_stop_all -> audio_stop_all.

#include "common.h"
#include "gen/sounds.h"

void audio_init(void);
// false when the DSP could not start (sdmc:/3ds/dspfirm.cdc missing): the game is silent.
bool audio_available(void);
void audio_exit(void);

// One-shot effect (or a looping one). Returns a handle for audio_stop / audio_is_playing
// (-1 if nothing played). Music ids passed here are started as music.
int audio_play(int snd);
void audio_preload(int snd);           // read an effect now (match start), not on first use
int audio_play_ex(int snd, float gain, bool loop);
void audio_stop(int handle);
void audio_set_gain(int handle, float gain);  // audio_sound_gain on a playing instance
void audio_stop_sound(int snd);        // every instance of a sound (audio_stop_sound)
bool audio_is_playing(int snd);        // any instance of a sound (audio_is_playing)

void audio_music(int snd);             // loop a track, replacing the current one
void audio_music_stop(void);
void audio_music_gain(float gain);     // 0..1
int audio_music_current(void);         // SND_* of the playing track, -1 if none
// Per-frame track request (-1 silence, -2 keep): paired tracks the GML plays together and
// crossfades with audio_sound_gain (mus_act9 / _chase, mus_dotdotdot / 2) fade out, swap at the
// same position and fade back in; anything else switches at once.
void audio_music_request(int snd);

void audio_stop_all(void);

// Global volume multipliers (settings)
extern float audio_sfx_volume, audio_music_volume;
void audio_apply_volume(int music, int sfx);  // 0..10 (settings)
