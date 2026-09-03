#include "switch_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <malloc.h>

#include <switch.h>

namespace {

constexpr int kOutRate = 48000;
constexpr double kInRate = 281250.0;  // Spu native rate
constexpr int kVoiceId = 0;
// One SPU frame is ~4687 input pairs -> ~800 output pairs at 48000/60; give
// generous headroom for a slow frame or two before this would ever clip.
constexpr int kMaxOutFrames = 4096;
constexpr int kNumBuffers = 4;

// Self-heal thresholds (see switch_audio_push()'s own comment for why this
// exists): a stuck wavebuf slot or a failed init is retried automatically
// rather than staying silent for the rest of the process's life the way a
// one-shot, no-retry init/never-recover design would - matches what
// actually fixed it by hand (a full relaunch re-runs this exact sequence),
// just automated instead of requiring one.
constexpr int kStuckFramesThreshold = 180;   // ~3s @60fps - well past any normal backpressure drop
constexpr int kRetryCooldownFrames = 300;    // ~5s @60fps between reinit attempts, so a genuinely
                                              // dead audio service isn't hammered every frame

AudioDriver s_drv;
bool s_audren_ok = false;
bool s_driver_ok = false;
bool s_voice_ok = false;

void* s_mempool_ptr = nullptr;
size_t s_mempool_size = 0;
int s_mempool_id = -1;
int16_t* s_buffer[kNumBuffers] = {};
AudioDriverWaveBuf s_wavebuf[kNumBuffers] = {};
int s_next_buffer = 0;
int s_stuck_frames = 0;
int s_retry_cooldown = 0;

// Linear resampler state, carried across calls - identical algorithm to
// Android's jni_bridge.cpp PushResampled().
double s_resample_pos = -1.0;
int16_t s_prev_l = 0, s_prev_r = 0;

int16_t s_scratch[kMaxOutFrames * 2];

}  // namespace

bool switch_audio_init() {
  // Reset everything a mid-session self-heal re-init needs to start clean -
  // harmless on the very first call too, since these are all already at
  // these same values from static initialization.
  s_next_buffer = 0;
  s_stuck_frames = 0;
  s_resample_pos = -1.0;
  s_prev_l = s_prev_r = 0;

  static const AudioRendererConfig ar_config = {
      .output_rate = AudioRendererOutputRate_48kHz,
      .num_voices = 2,
      .num_effects = 0,
      .num_sinks = 1,
      .num_mix_objs = 1,
      .num_mix_buffers = 2,
  };

  s_audren_ok = R_SUCCEEDED(audrenInitialize(&ar_config));
  if (!s_audren_ok) return false;

  s_driver_ok = R_SUCCEEDED(audrvCreate(&s_drv, &ar_config, 2));
  if (!s_driver_ok) return false;

  static const uint8_t sink_channels[] = {0, 1};
  audrvDeviceSinkAdd(&s_drv, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);
  audrvUpdate(&s_drv);
  audrenStartAudioRenderer();

  const size_t per_buffer_bytes = kMaxOutFrames * 2 * sizeof(int16_t);
  s_mempool_size = (per_buffer_bytes * kNumBuffers + 0xFFF) & ~size_t(0xFFF);
  s_mempool_ptr = memalign(0x1000, s_mempool_size);
  if (!s_mempool_ptr) return false;
  std::memset(s_mempool_ptr, 0, s_mempool_size);

  s_mempool_id = audrvMemPoolAdd(&s_drv, s_mempool_ptr, s_mempool_size);
  audrvMemPoolAttach(&s_drv, s_mempool_id);

  for (int i = 0; i < kNumBuffers; i++) {
    s_buffer[i] = (int16_t*)((uint8_t*)s_mempool_ptr + i * per_buffer_bytes);
    s_wavebuf[i] = {};
  }

  s_voice_ok = audrvVoiceInit(&s_drv, kVoiceId, 2 /* stereo */, PcmFormat_Int16, kOutRate);
  if (s_voice_ok) {
    audrvVoiceSetDestinationMix(&s_drv, kVoiceId, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&s_drv, kVoiceId, 1.0f, 0, 0);  // L -> L
    audrvVoiceSetMixFactor(&s_drv, kVoiceId, 1.0f, 1, 1);  // R -> R
    audrvVoiceStart(&s_drv, kVoiceId);
  }
  audrvUpdate(&s_drv);
  return s_voice_ok;
}

void switch_audio_push(const int16_t* in, size_t in_frames) {
  // Never initialized (or an earlier self-heal attempt also failed) - rather
  // than stay silent for the rest of the process's life, retry on a cooldown
  // instead of every single frame, so a genuinely dead audio service isn't
  // hammered with reinit attempts 60 times a second.
  if (!s_voice_ok) {
    if (s_retry_cooldown > 0) {
      s_retry_cooldown--;
      return;
    }
    switch_audio_shutdown();  // clears any partial state from the last failed attempt
    if (!switch_audio_init()) {
      s_retry_cooldown = kRetryCooldownFrames;
      return;
    }
  }
  if (in_frames == 0) return;

  const int in_count = (int)in_frames;
  const double step = kInRate / kOutRate;
  int out_frames = 0;
  while (s_resample_pos < in_count - 1 && out_frames < kMaxOutFrames) {
    const int i0 = (int)std::floor(s_resample_pos);
    const double frac = s_resample_pos - i0;
    const int16_t l0 = (i0 < 0) ? s_prev_l : in[i0 * 2];
    const int16_t r0 = (i0 < 0) ? s_prev_r : in[i0 * 2 + 1];
    const int16_t l1 = in[(i0 + 1) * 2];
    const int16_t r1 = in[(i0 + 1) * 2 + 1];
    s_scratch[out_frames * 2] = (int16_t)(l0 + (l1 - l0) * frac);
    s_scratch[out_frames * 2 + 1] = (int16_t)(r0 + (r1 - r0) * frac);
    out_frames++;
    s_resample_pos += step;
  }
  s_prev_l = in[(in_count - 1) * 2];
  s_prev_r = in[(in_count - 1) * 2 + 1];
  s_resample_pos -= in_count;

  if (out_frames == 0) return;

  AudioDriverWaveBuf& wb = s_wavebuf[s_next_buffer];
  // Drop this frame's audio if the slot we'd reuse is still in flight,
  // rather than blocking the render loop waiting on it - normal, occasional
  // backpressure. But since we only ever retry the *same* slot (s_next_buffer
  // doesn't advance below on this path), staying stuck on one slot for way
  // longer than a slow frame or two would mean that wavebuf's state will
  // never come unstuck on its own (observed on hardware as audio going
  // silent until the whole app is relaunched, which re-creates the audio
  // driver from scratch) - so if that drags on, do exactly that ourselves.
  if (wb.state != AudioDriverWaveBufState_Free && wb.state != AudioDriverWaveBufState_Done) {
    if (++s_stuck_frames > kStuckFramesThreshold) {
      switch_audio_shutdown();
      switch_audio_init();
    }
    return;
  }
  s_stuck_frames = 0;

  int16_t* dst = s_buffer[s_next_buffer];
  std::memcpy(dst, s_scratch, out_frames * 2 * sizeof(int16_t));

  wb = {};
  wb.data_pcm16 = dst;
  wb.size = out_frames * 2 * sizeof(int16_t);
  wb.start_sample_offset = 0;
  wb.end_sample_offset = out_frames;
  wb.is_looping = false;

  armDCacheFlush(dst, out_frames * 2 * sizeof(int16_t));
  audrvVoiceAddWaveBuf(&s_drv, kVoiceId, &wb);
  audrvVoiceStart(&s_drv, kVoiceId);
  audrvUpdate(&s_drv);

  s_next_buffer = (s_next_buffer + 1) % kNumBuffers;
}

void switch_audio_shutdown() {
  if (s_voice_ok) {
    audrvVoiceStop(&s_drv, kVoiceId);
    audrvVoiceDrop(&s_drv, kVoiceId);
    s_voice_ok = false;
  }
  if (s_mempool_id >= 0) {
    audrvMemPoolDetach(&s_drv, s_mempool_id);
    audrvMemPoolRemove(&s_drv, s_mempool_id);
    s_mempool_id = -1;
  }
  if (s_mempool_ptr) {
    free(s_mempool_ptr);
    s_mempool_ptr = nullptr;
  }
  if (s_driver_ok) {
    audrvClose(&s_drv);
    s_driver_ok = false;
  }
  if (s_audren_ok) {
    audrenExit();
    s_audren_ok = false;
  }
}
