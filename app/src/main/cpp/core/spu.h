#pragma once
#include "common.h"

namespace dsmile {

class Spg200;

// SPG200 SPU: 16 channels, native 281250 Hz stereo output.
class Spu {
 public:
  Spu(Spg200& bus) : bus_(bus), sample_clock_(96), envelope_clock_(384) {}

  void Reset();
  void RunCycles(int cycles);
  u16 Read(u32 addr);  // 0x3000-0x37FF
  void Write(u32 addr, u16 val);

  // Drains the internal audio buffer (interleaved stereo, signed 16-bit,
  // 281250 Hz). Returns sample count (pairs*2) written since last call.
  int DrainAudio(s16* out, int max_samples);

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

 private:
  struct Adpcm {
    s32 predictor = 0;
    int step_index = 0;
    void Reset() { predictor = 0; step_index = 0; }
    u16 Decode(u8 code);
  };
  struct Channel {
    u32 wave_address = 0;   // 22-bit
    u32 loop_address = 0;   // 22-bit
    u32 envelope_address = 0;
    int wave_shift = 0;
    u16 mode = 0;           // bits12-13 tone_mode, bit14 tone_color, bit15 adpcm
    u16 pan = 0;            // vol 0-6, pan 8-14
    u16 envelope0 = 0;      // inc 0-6, sign 7, target 8-14
    u16 envelope_data = 0;  // edd 0-6, count 8-15
    u16 envelope1 = 0;      // load 0-7, repeat 8, repeat_count 9-15
    u16 envelope_irq = 0;   // irq_enable bit6, fire_addr bits7-15
    u16 envelope_loop_control = 0;  // ea_offset 0-8, rampdown_offset 9-15
    u16 wave_data_0 = 0x8000, wave_data = 0x8000;
    u32 phase = 0, phase_acc = 0, target_phase = 0;  // 19-bit
    u16 pitch_bend_control = 0;
    u8 rampdown_clk = 0;
    u8 env_clk = 0;
    Adpcm adpcm;
  };

  void GenerateSample();
  void TickChannel(int ch);
  void HandleEndMarker(int ch);
  void TickEnvelope(int ch);
  void TickPitchbend(int ch);
  void TickRampdown(int ch);
  void StartChannel(int ch);
  void StopChannel(int ch);
  void UpdateChannelIrq();
  void UpdateBeatIrq();

  Spg200& bus_;
  CycleClock sample_clock_;
  CycleClock envelope_clock_;
  u32 env_tick_ = 0;       // increments at 70312.5 Hz
  u32 rampdown_tick_ = 0;  // increments every 13 env ticks
  int rampdown_div_ = 13;

  Channel ch_[16];
  u16 channel_enable_ = 0, channel_fiq_enable_ = 0, channel_fiq_status_ = 0;
  u16 channel_env_rampdown_ = 0, channel_stop_ = 0, channel_zero_cross_ = 0;
  u16 channel_repeat_ = 0, channel_env_mode_ = 0, channel_tone_release_ = 0;
  u16 channel_env_irq_ = 0, channel_pitch_bend_ = 0;
  u16 main_volume_ = 0;
  u16 wave_in_l_ = 0x8000, wave_in_r_ = 0x8000;
  u16 wave_out_l_ = 0x8000, wave_out_r_ = 0x8000;
  u16 beat_base_count_ = 0, current_beat_base_count_ = 0;
  u16 beat_count_ = 0;  // bits0-13 count, bit14 irq_status, bit15 irq_enable
  u16 control_ = 0;

  // Ring buffer: ~2 frames of 281250 Hz stereo.
  static constexpr int kAudioBufSize = 16384;
  s16 audio_buf_[kAudioBufSize];
  int audio_pos_ = 0;
};

}  // namespace dsmile
