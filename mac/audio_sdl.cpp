// NFS Hot Pursuit 2. GPL-3.0-or-later.
// The game's sound on macOS: the SPU2's 48 kHz stereo frames pulled by SDL3's audio stream as the device needs them
// (in place of upstream's audio_wasapi.cpp).
#include "audio_out.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

extern FILE *g_hle_log;

namespace {
AudioPull g_pull;

void SDLCALL feed(void *, SDL_AudioStream *stream, int additional, int)
{
    static std::vector<s16> buf;
    const int frames = additional / 4;
    if (frames <= 0) return;
    buf.resize((size_t)frames * 2);
    g_pull(buf.data(), frames);
    SDL_PutAudioStreamData(stream, buf.data(), frames * 4);
}
}   // namespace

bool audio_out_start(AudioPull pull)
{
    g_pull = pull;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(g_hle_log, "audio: SDL audio failed: %s\n", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, AUDIO_RATE};
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
    if (!stream) {
        fprintf(g_hle_log, "audio: no output device: %s\n", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    fprintf(g_hle_log, "audio: SDL3 output, %d Hz stereo\n", AUDIO_RATE);
    return true;
}
