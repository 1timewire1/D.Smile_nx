#pragma once
#include <cstdint>
#include <cstddef>

// Stereo PCM16 output via libnx's audrv/audren, fed once per frame with
// whatever the SPU drained that frame (interleaved stereo s16 @ 281250 Hz -
// Spu::DrainAudio's native rate). Internally resamples to 48000 Hz with the
// same linear interpolator D.Smile's Android jni_bridge.cpp uses, then
// queues one wavebuf per frame (push model, like the Yokoi reference port's
// switch_sound.cpp) rather than Android's pull-model Oboe callback - there's
// no separate emulation thread here to feed a callback from.
bool switch_audio_init();
void switch_audio_push(const int16_t* interleaved_stereo, size_t in_frames);
void switch_audio_shutdown();

// Counters since start, for the performance log: frames of audio dropped
// because all wavebufs were still queued, frames that found the queue empty
// (an underrun: a gap was heard), and self-heal re-inits.
// Also the rate control's current state: the smoothed queue level (output
// frames) and the resampling adjustment it's applying.
struct SwitchAudioStats {
  int dropped = 0, underruns = 0, reinits = 0;
  double level = 0, adjust = 0;
};
SwitchAudioStats switch_audio_stats();
