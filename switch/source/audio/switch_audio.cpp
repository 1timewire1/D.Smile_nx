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
constexpr int kNumBuffers = 8;

// Dynamic rate control. The console makes 60.05 frames of audio a second;
// the Switch shows 60 and plays 48000 samples, so a fixed resampling ratio
// lets the queue drift empty (a click) and timing jitter swings it further.
// The ratio is nudged instead - at most 0.5%, too little to hear as pitch
// (RetroArch's own rate-control limit) - to hold the queue near
// kTargetQueued output frames: two video frames' worth, ~33 ms. That covers
// the frame-rate mismatch and jitter; it can't hide a game running a few
// percent slow, which still underruns now and then.
constexpr double kTargetQueued = 1600.0;
constexpr double kMaxRateAdjust = 0.005;
constexpr double kLevelSmoothing = 0.02;  // per frame: follows the level over ~50 frames
// When the queue has run dry (start, after the pause menu, an underrun), this
// much silence goes in ahead of the next audio: headroom straight away,
// rather than hovering at empty where every late frame is another click.
constexpr int kPrimeFrames = 800;

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
SwitchAudioStats s_stats;
double s_level = kTargetQueued;  // smoothed count of queued output frames
uint64_t s_last_push_tick = 0;

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
  s_level = kTargetQueued;
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

// Queues one wavebuf of interleaved stereo frames in the next slot - or
// drops them if that slot is still in flight, rather than blocking the
// render loop on it (normal, occasional backpressure). Since only that same
// slot is ever retried, staying stuck on it far longer than a slow frame or
// two would mean its state will never come unstuck on its own (seen on
// hardware as audio silent until a relaunch, which re-creates the driver):
// then re-create the driver ourselves.
static void QueueFrames(const int16_t* frames, int count) {
  AudioDriverWaveBuf& wb = s_wavebuf[s_next_buffer];
  if (wb.state != AudioDriverWaveBufState_Free && wb.state != AudioDriverWaveBufState_Done) {
    s_stats.dropped++;
    if (++s_stuck_frames > kStuckFramesThreshold) {
      s_stats.reinits++;
      switch_audio_shutdown();
      switch_audio_init();
    }
    return;
  }
  s_stuck_frames = 0;

  int16_t* dst = s_buffer[s_next_buffer];
  std::memcpy(dst, frames, count * 2 * sizeof(int16_t));
  wb = {};
  wb.data_pcm16 = dst;
  wb.size = count * 2 * sizeof(int16_t);
  wb.start_sample_offset = 0;
  wb.end_sample_offset = count;
  wb.is_looping = false;

  armDCacheFlush(dst, count * 2 * sizeof(int16_t));
  audrvVoiceAddWaveBuf(&s_drv, kVoiceId, &wb);
  audrvVoiceStart(&s_drv, kVoiceId);
  audrvUpdate(&s_drv);

  s_next_buffer = (s_next_buffer + 1) % kNumBuffers;
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

  // The wavebufs' states only change on an audrvUpdate(). Before the fix
  // for the "audio dies" bug the only call came after queueing, so a slot
  // still playing then looked busy forever: this function returned before
  // ever updating again, and after kStuckFramesThreshold frames (3 s of
  // silence) the whole driver was torn down and re-created. It took a full
  // queue to trigger, rare below 60 fps and routine once the emulator kept up.
  audrvUpdate(&s_drv);
  double queued = 0;  // output frames not yet played; the playing buffer counts as half
  bool dry = true;
  for (const AudioDriverWaveBuf& b : s_wavebuf) {
    if (b.state == AudioDriverWaveBufState_Waiting || b.state == AudioDriverWaveBufState_Queued) {
      queued += (double)b.end_sample_offset;
      dry = false;
    } else if (b.state == AudioDriverWaveBufState_Playing) {
      queued += (double)b.end_sample_offset * 0.5;
      dry = false;
    }
  }
  const uint64_t now = armGetSystemTick();
  if (dry) {
    // Ran dry while being fed every frame: a gap was heard. (Not after a
    // pause - nothing is pushed while the menu is up.)
    if (s_last_push_tick != 0 && armTicksToNs(now - s_last_push_tick) < 100'000'000ull) s_stats.underruns++;
    s_level = kTargetQueued;
  } else {
    s_level += kLevelSmoothing * (queued - s_level);
  }
  s_last_push_tick = now;
  // Too much queued: take input a little faster (fewer output frames), and
  // the reverse - full adjustment when a whole target's worth off.
  const double adjust =
      std::clamp((s_level - kTargetQueued) / kTargetQueued * kMaxRateAdjust, -kMaxRateAdjust, kMaxRateAdjust);
  const double step = kInRate / kOutRate * (1.0 + adjust);
  s_stats.level = s_level;
  s_stats.adjust = adjust;

  const int in_count = (int)in_frames;
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
  if (dry) {
    static const int16_t kSilence[kPrimeFrames * 2] = {};
    QueueFrames(kSilence, kPrimeFrames);
  }
  QueueFrames(s_scratch, out_frames);
}

SwitchAudioStats switch_audio_stats() { return s_stats; }

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
