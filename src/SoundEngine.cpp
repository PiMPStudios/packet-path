#include "SoundEngine.h"
#include "raylib.h"
#include <cmath>

static Sound sndSend   = {};
static Sound sndArrive = {};
static Sound sndFail   = {};

static const float kPI = 3.14159265f;

// Allocates a short[] buffer, fills it with a frequency sweep, wraps in a Wave,
// calls LoadSoundFromWave (which copies data to the audio device), then frees.
static Sound MakeSweep(float freqStart, float freqEnd,
                        float duration, float volume, bool square)
{
    const int SR = 44100;
    int n        = (int)(SR * duration);
    short* data  = new short[n];

    float phase = 0.f;
    for (int i = 0; i < n; ++i) {
        float t    = (float)i / (float)n;
        float freq = freqStart + (freqEnd - freqStart) * t;
        float fade = 1.0f - t;
        phase     += 2.f * kPI * freq / (float)SR;
        float s    = square ? (std::sin(phase) >= 0.f ? 1.f : -1.f)
                            : std::sin(phase);
        data[i]    = (short)(s * fade * volume * 32767.f);
    }

    Wave w       = {};
    w.frameCount = (unsigned int)n;
    w.sampleRate = (unsigned int)SR;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = data;
    Sound snd    = LoadSoundFromWave(w);   // copies data to audio device
    delete[] data;
    return snd;
}

// Two-frequency sine chord with linear amplitude fade-out.
static Sound MakeChord(float freq1, float freq2, float duration, float volume) {
    const int SR = 44100;
    int n        = (int)(SR * duration);
    short* data  = new short[n];

    float ph1 = 0.f, ph2 = 0.f;
    for (int i = 0; i < n; ++i) {
        float t    = (float)i / (float)n;
        float fade = 1.0f - t;
        ph1 += 2.f * kPI * freq1 / (float)SR;
        ph2 += 2.f * kPI * freq2 / (float)SR;
        float s = (std::sin(ph1) + std::sin(ph2)) * 0.5f;
        data[i] = (short)(s * fade * volume * 32767.f);
    }

    Wave w       = {};
    w.frameCount = (unsigned int)n;
    w.sampleRate = (unsigned int)SR;
    w.sampleSize = 16;
    w.channels   = 1;
    w.data       = data;
    Sound snd    = LoadSoundFromWave(w);
    delete[] data;
    return snd;
}

void InitSounds() {
    sndSend   = MakeSweep(880.f, 1100.f, 0.15f, 0.4f, false);  // sine sweep up
    sndArrive = MakeChord(660.f, 880.f,  0.25f, 0.4f);          // two-tone chord
    sndFail   = MakeSweep(220.f, 110.f,  0.30f, 0.3f, true);    // square sweep down
}

void UnloadSounds() {
    UnloadSound(sndSend);
    UnloadSound(sndArrive);
    UnloadSound(sndFail);
}

void PlayPacketSend()   { PlaySound(sndSend);   }
void PlayPacketArrive() { PlaySound(sndArrive); }
void PlayPacketFail()   { PlaySound(sndFail);   }
