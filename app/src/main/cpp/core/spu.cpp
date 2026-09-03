#include "spu.h"

#include <algorithm>

#include "spg200.h"
#include "state.h"

namespace dsmile {

namespace {
const int kEnvelopeFrameDivides[16] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 13, 13, 13};
const int kRampdownFrameDivides[8] = {2, 4, 6, 8, 10, 12, 13, 13};
const int kPitchbendFrameDivides[8] = {3, 4, 5, 6, 7, 8, 9, 10};

const s16 kAdpcmStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kAdpcmStepAdjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
}  // namespace

u16 Spu::Adpcm::Decode(u8 code) {
  const int step = kAdpcmStepTable[step_index];
  int diff = step >> 3;
  if (code & 1) diff += step >> 2;
  if (code & 2) diff += step >> 1;
  if (code & 4) diff += step;
  if (code & 8) predictor -= diff; else predictor += diff;
  predictor = std::clamp(predictor, -32768, 32767);
  step_index = std::clamp(step_index + kAdpcmStepAdjust[code & 7], 0, 88);
  return (u16)(s16)predictor;
}

void Spu::Reset() {
  sample_clock_.Reset();
  envelope_clock_.Reset();
  env_tick_ = 0;
  rampdown_tick_ = 0;
  rampdown_div_ = 13;
  for (auto& c : ch_) c = Channel{};
  channel_enable_ = channel_fiq_enable_ = channel_fiq_status_ = 0;
  channel_env_rampdown_ = channel_stop_ = channel_zero_cross_ = 0;
  channel_repeat_ = channel_env_mode_ = channel_tone_release_ = 0;
  channel_env_irq_ = channel_pitch_bend_ = 0;
  main_volume_ = 0;
  wave_in_l_ = wave_in_r_ = 0x8000;
  wave_out_l_ = wave_out_r_ = 0x8000;
  beat_base_count_ = current_beat_base_count_ = 0;
  beat_count_ = 0;
  control_ = 0;
  audio_pos_ = 0;
}

void Spu::SaveState(StateWriter& w) const {
  w.S64(sample_clock_.counter);
  w.S64(envelope_clock_.counter);
  w.U32(env_tick_);
  w.U32(rampdown_tick_);
  w.U32((u32)rampdown_div_);
  for (const Channel& c : ch_) {
    w.U32(c.wave_address); w.U32(c.loop_address); w.U32(c.envelope_address);
    w.U32((u32)c.wave_shift);
    w.U16(c.mode); w.U16(c.pan); w.U16(c.envelope0); w.U16(c.envelope_data);
    w.U16(c.envelope1); w.U16(c.envelope_irq); w.U16(c.envelope_loop_control);
    w.U16(c.wave_data_0); w.U16(c.wave_data);
    w.U32(c.phase); w.U32(c.phase_acc); w.U32(c.target_phase);
    w.U16(c.pitch_bend_control);
    w.U8(c.rampdown_clk); w.U8(c.env_clk);
    w.U32((u32)c.adpcm.predictor); w.U32((u32)c.adpcm.step_index);
  }
  w.U16(channel_enable_); w.U16(channel_fiq_enable_); w.U16(channel_fiq_status_);
  w.U16(channel_env_rampdown_); w.U16(channel_stop_); w.U16(channel_zero_cross_);
  w.U16(channel_repeat_); w.U16(channel_env_mode_); w.U16(channel_tone_release_);
  w.U16(channel_env_irq_); w.U16(channel_pitch_bend_);
  w.U16(main_volume_);
  w.U16(wave_in_l_); w.U16(wave_in_r_); w.U16(wave_out_l_); w.U16(wave_out_r_);
  w.U16(beat_base_count_); w.U16(current_beat_base_count_); w.U16(beat_count_);
  w.U16(control_);
}

void Spu::LoadState(StateReader& r) {
  sample_clock_.counter = r.S64();
  envelope_clock_.counter = r.S64();
  env_tick_ = r.U32();
  rampdown_tick_ = r.U32();
  rampdown_div_ = (int)r.U32();
  for (Channel& c : ch_) {
    c.wave_address = r.U32(); c.loop_address = r.U32(); c.envelope_address = r.U32();
    c.wave_shift = (int)r.U32();
    c.mode = r.U16(); c.pan = r.U16(); c.envelope0 = r.U16(); c.envelope_data = r.U16();
    c.envelope1 = r.U16(); c.envelope_irq = r.U16(); c.envelope_loop_control = r.U16();
    c.wave_data_0 = r.U16(); c.wave_data = r.U16();
    c.phase = r.U32(); c.phase_acc = r.U32(); c.target_phase = r.U32();
    c.pitch_bend_control = r.U16();
    c.rampdown_clk = r.U8(); c.env_clk = r.U8();
    c.adpcm.predictor = (s32)r.U32(); c.adpcm.step_index = (int)r.U32();
  }
  channel_enable_ = r.U16(); channel_fiq_enable_ = r.U16(); channel_fiq_status_ = r.U16();
  channel_env_rampdown_ = r.U16(); channel_stop_ = r.U16(); channel_zero_cross_ = r.U16();
  channel_repeat_ = r.U16(); channel_env_mode_ = r.U16(); channel_tone_release_ = r.U16();
  channel_env_irq_ = r.U16(); channel_pitch_bend_ = r.U16();
  main_volume_ = r.U16();
  wave_in_l_ = r.U16(); wave_in_r_ = r.U16(); wave_out_l_ = r.U16(); wave_out_r_ = r.U16();
  beat_base_count_ = r.U16(); current_beat_base_count_ = r.U16(); beat_count_ = r.U16();
  control_ = r.U16();
  audio_pos_ = 0;
  UpdateChannelIrq();
  UpdateBeatIrq();
}

void Spu::RunCycles(int cycles) {
  int fired = sample_clock_.Tick(cycles);
  while (fired--) GenerateSample();

  fired = envelope_clock_.Tick(cycles);
  while (fired--) {
    env_tick_++;
    for (int i = 0; i < 16; i++) {
      if (!(channel_enable_ & (1 << i)) || (channel_stop_ & (1 << i))) continue;
      TickEnvelope(i);
      TickPitchbend(i);
    }
    if (--rampdown_div_ <= 0) {
      rampdown_div_ = 13;
      rampdown_tick_++;
      for (int i = 0; i < 16; i++) {
        if (!(channel_enable_ & (1 << i)) || (channel_stop_ & (1 << i))) continue;
        TickRampdown(i);
      }
    }
    if (current_beat_base_count_) {
      current_beat_base_count_--;
      if (current_beat_base_count_ == 0) {
        current_beat_base_count_ = beat_base_count_;
        u16 count = beat_count_ & 0x3FFF;
        if (count) count--;
        beat_count_ = (beat_count_ & 0xC000) | count;
        if (count == 0 && (beat_count_ & 0x8000)) {
          beat_count_ |= 0x4000;
          UpdateBeatIrq();
        }
      }
    }
  }
}

void Spu::GenerateSample() {
  s32 left_out = 0, right_out = 0;
  for (int i = 0; i < 16; i++) {
    if (!(channel_enable_ & (1 << i)) || (channel_stop_ & (1 << i))) continue;
    TickChannel(i);
    const Channel& c = ch_[i];

    const u16 prev_part = (u16)(((u64)c.wave_data_0 * ((1u << 19) - c.phase_acc)) >> 19);
    const u16 cur_part = (u16)(((u64)c.wave_data * c.phase_acc) >> 19);
    s32 sample = (s16)((u16)(prev_part + cur_part) - 0x8000);

    const int pan = (c.pan >> 8) & 0x7F;
    const int vol = c.pan & 0x7F;
    const int left_pan = std::clamp((0x80 - pan) * 2, 0, 0x7F);
    const int right_pan = std::clamp(pan * 2, 0, 0x7F);

    sample = (sample * (int)(c.envelope_data & 0x7F)) >> 7;
    left_out += (sample * left_pan * vol) >> 14;
    right_out += (sample * right_pan * vol) >> 14;
  }

  const int high_volume = (control_ >> 6) & 3;
  left_out >>= (4 - high_volume);
  right_out >>= (4 - high_volume);

  const s16 left_final = (s16)(((left_out + ((s32)wave_in_l_ - 0x8000)) * main_volume_) >> 7);
  const s16 right_final = (s16)(((right_out + ((s32)wave_in_r_ - 0x8000)) * main_volume_) >> 7);
  wave_out_l_ = (u16)left_final ^ 0x8000;
  wave_out_r_ = (u16)right_final ^ 0x8000;

  if (audio_pos_ <= kAudioBufSize - 2) {
    audio_buf_[audio_pos_++] = left_final;
    audio_buf_[audio_pos_++] = right_final;
  }
}

int Spu::DrainAudio(s16* out, int max_samples) {
  const int n = std::min(audio_pos_, max_samples);
  std::memcpy(out, audio_buf_, n * sizeof(s16));
  audio_pos_ = 0;
  return n;
}

void Spu::TickChannel(int i) {
  Channel& c = ch_[i];
  const u32 acc = c.phase_acc + c.phase;
  c.phase_acc = acc & 0x7FFFF;
  if (acc < 0x80000) return;

  if (channel_fiq_enable_ & (1 << i)) {
    channel_fiq_status_ |= (1 << i);
    UpdateChannelIrq();
  }

  c.wave_data_0 = c.wave_data;

  const int tone_mode = (c.mode >> 12) & 3;
  if (tone_mode == 0) return;  // SW channel: unimplemented

  const u16 word = bus_.Read(c.wave_address);

  if (c.mode & 0x8000) {  // ADPCM
    if (word == 0xFFFF) {
      HandleEndMarker(i);
    } else {
      const u8 nibble = (word >> c.wave_shift) & 0xF;
      c.wave_data = c.adpcm.Decode(nibble) ^ 0x8000;
    }
    c.wave_shift += 4;
    if (c.wave_shift >= 16) {
      c.wave_shift = 0;
      c.wave_address = (c.wave_address + 1) & 0x3FFFFF;
    }
  } else if (!(c.mode & 0x4000)) {  // 8-bit PCM
    const u8 b = (word >> c.wave_shift) & 0xFF;
    if (b == 0xFF) {
      HandleEndMarker(i);
    } else {
      c.wave_data = (u16)((b << 8) | b);
    }
    c.wave_shift += 8;
    if (c.wave_shift >= 16) {
      c.wave_shift = 0;
      c.wave_address = (c.wave_address + 1) & 0x3FFFFF;
    }
  } else {  // 16-bit PCM
    if (word == 0xFFFF) {
      HandleEndMarker(i);
    } else {
      c.wave_data = word;
    }
    c.wave_address = (c.wave_address + 1) & 0x3FFFFF;
  }
}

void Spu::HandleEndMarker(int i) {
  Channel& c = ch_[i];
  const int tone_mode = (c.mode >> 12) & 3;
  if (tone_mode == 1) {
    StopChannel(i);
  } else if (tone_mode == 2) {
    c.wave_address = c.loop_address;
    c.wave_shift = 0;
    c.mode &= (u16)~0x8000;  // hardware quirk: ADPCM loops continue as PCM
  }
}

void Spu::TickEnvelope(int i) {
  Channel& c = ch_[i];
  if ((channel_env_mode_ & (1 << i)) || (channel_env_rampdown_ & (1 << i))) return;
  const int div = kEnvelopeFrameDivides[c.env_clk];
  if ((env_tick_ & ((1u << div) - 1)) != 0) return;

  u16 count = (c.envelope_data >> 8) & 0xFF;
  if (count) count--;
  c.envelope_data = (c.envelope_data & 0x7F) | (count << 8);
  if (count != 0) return;

  int edd = c.envelope_data & 0x7F;
  const int inc = c.envelope0 & 0x7F;
  const int target = (c.envelope0 >> 8) & 0x7F;
  const bool sign = c.envelope0 & 0x80;

  if (edd != target) {
    if (sign) {
      edd = std::clamp(edd - inc, target, 0x7F);
      c.envelope_data = (c.envelope_data & 0xFF00) | edd;
      if (edd == 0) {
        StopChannel(i);
        return;
      }
    } else {
      edd = std::clamp(edd + inc, 0, target);
      c.envelope_data = (c.envelope_data & 0xFF00) | edd;
    }
  }

  if (edd == target) {
    const u32 addr = c.envelope_address + (c.envelope_loop_control & 0x1FF);
    if (c.envelope1 & 0x100) {  // repeat
      u16 repeat_count = (c.envelope1 >> 9) & 0x7F;
      if (repeat_count) repeat_count--;
      c.envelope1 = (c.envelope1 & 0x01FF) | (repeat_count << 9);
      if (repeat_count == 0) {
        c.envelope0 = bus_.Read(addr);
        c.envelope1 = bus_.Read(addr + 1);
        const u16 old_rampdown = c.envelope_loop_control & 0xFE00;
        c.envelope_loop_control = (bus_.Read(addr + 2) & 0x01FF) | old_rampdown;
        if ((c.envelope_irq & 0x40) &&
            (c.envelope_loop_control & 0x1FF) == ((c.envelope_irq >> 7) & 0x1FF)) {
          channel_env_irq_ |= (1 << i);
          UpdateBeatIrq();
        }
      }
    } else {
      c.envelope0 = bus_.Read(addr);
      c.envelope1 = bus_.Read(addr + 1);
      c.envelope_loop_control =
          (c.envelope_loop_control & 0xFE00) | (((c.envelope_loop_control & 0x1FF) + 2) & 0x1FF);
      if ((c.envelope_irq & 0x40) &&
          (c.envelope_loop_control & 0x1FF) == ((c.envelope_irq >> 7) & 0x1FF)) {
        channel_env_irq_ |= (1 << i);
        UpdateBeatIrq();
      }
    }
  }

  c.envelope_data = (c.envelope_data & 0x7F) | ((c.envelope1 & 0xFF) << 8);
}

void Spu::TickPitchbend(int i) {
  Channel& c = ch_[i];
  if (!(channel_pitch_bend_ & (1 << i)) || c.phase == c.target_phase) return;
  const int div = kPitchbendFrameDivides[(c.pitch_bend_control >> 13) & 7];
  if ((env_tick_ & ((1u << div) - 1)) != 0) return;
  const int offset = c.pitch_bend_control & 0x0FFF;
  if (c.pitch_bend_control & 0x1000) {
    c.phase = (u32)std::clamp((int)c.phase - offset, (int)c.target_phase, 0x7FFFF);
  } else {
    c.phase = (u32)std::clamp((int)c.phase + offset, 0, (int)c.target_phase);
  }
}

void Spu::TickRampdown(int i) {
  Channel& c = ch_[i];
  if (!(channel_env_rampdown_ & (1 << i))) return;
  const int div = kRampdownFrameDivides[c.rampdown_clk];
  if ((rampdown_tick_ & ((1u << div) - 1)) != 0) return;
  const int offset = (c.envelope_loop_control >> 9) & 0x7F;
  const int edd = std::clamp((int)(c.envelope_data & 0x7F) - offset, 0, 0x7F);
  c.envelope_data = (c.envelope_data & 0xFF00) | edd;
  if (edd == 0) StopChannel(i);
}

void Spu::StartChannel(int i) {
  Channel& c = ch_[i];
  c.wave_shift = 0;
  c.adpcm.Reset();
  if (!(channel_env_mode_ & (1 << i))) {
    c.envelope_data = (c.envelope_data & 0x7F) | ((c.envelope1 & 0xFF) << 8);
  }
}

void Spu::StopChannel(int i) {
  channel_stop_ |= (1 << i);
  channel_tone_release_ &= (u16)~(1 << i);
  channel_env_rampdown_ &= (u16)~(1 << i);
  ch_[i].mode &= (u16)~0x8000;
}

void Spu::UpdateChannelIrq() {
  bus_.SetSpuChannelIrqLine(channel_fiq_status_ != 0);
}

void Spu::UpdateBeatIrq() {
  const bool beat = (beat_count_ & 0x8000) && (beat_count_ & 0x4000);
  bus_.SetSpuBeatIrqLine(beat || channel_env_irq_ != 0);
}

u16 Spu::Read(u32 addr) {
  const u32 bank = addr & 0xFF00;
  if (bank == 0x3000) {
    Channel& c = ch_[(addr >> 4) & 0xF];
    switch (addr & 0xF) {
      case 0x0: return c.wave_address & 0xFFFF;
      case 0x1: return c.mode | (((c.loop_address >> 16) & 0x3F) << 6) | ((c.wave_address >> 16) & 0x3F);
      case 0x2: return c.loop_address & 0xFFFF;
      case 0x3: return c.pan;
      case 0x4: return c.envelope0;
      case 0x5: return c.envelope_data;
      case 0x6: return c.envelope1;
      case 0x7: return c.envelope_irq | ((c.envelope_address >> 16) & 0x3F);
      case 0x8: return c.envelope_address & 0xFFFF;
      case 0x9: return c.wave_data_0;
      case 0xA: return c.envelope_loop_control;
      case 0xB: return c.wave_data;
      default: return 0;
    }
  }
  if (bank == 0x3200) {
    Channel& c = ch_[(addr >> 4) & 0xF];
    switch (addr & 0xF) {
      case 0x0: return (c.phase >> 16) & 7;
      case 0x1: return (c.phase_acc >> 16) & 7;
      case 0x2: return (c.target_phase >> 16) & 7;
      case 0x3: return c.rampdown_clk;
      case 0x4: return c.phase & 0xFFFF;
      case 0x5: return c.phase_acc & 0xFFFF;
      case 0x6: return c.target_phase & 0xFFFF;
      case 0x7: return c.pitch_bend_control;
      default: return 0;
    }
  }
  if (bank == 0x3400) {
    switch (addr & 0xFF) {
      case 0x00: return channel_enable_;
      case 0x01: return main_volume_;
      case 0x02: return channel_fiq_enable_;
      case 0x03: return channel_fiq_status_;
      case 0x04: return beat_base_count_;
      case 0x05: return beat_count_;
      case 0x06: return (u16)(ch_[0].env_clk | (ch_[1].env_clk << 4) | (ch_[2].env_clk << 8) | (ch_[3].env_clk << 12));
      case 0x07: return (u16)(ch_[4].env_clk | (ch_[5].env_clk << 4) | (ch_[6].env_clk << 8) | (ch_[7].env_clk << 12));
      case 0x08: return (u16)(ch_[8].env_clk | (ch_[9].env_clk << 4) | (ch_[10].env_clk << 8) | (ch_[11].env_clk << 12));
      case 0x09: return (u16)(ch_[12].env_clk | (ch_[13].env_clk << 4) | (ch_[14].env_clk << 8) | (ch_[15].env_clk << 12));
      case 0x0A: return channel_env_rampdown_;
      case 0x0B: return channel_stop_;
      case 0x0C: return channel_zero_cross_;
      case 0x0D: return control_;
      case 0x0F: return channel_enable_ & (u16)~channel_stop_;
      case 0x12: return wave_out_l_;
      case 0x13: return wave_out_r_;
      case 0x14: return channel_repeat_;
      case 0x15: return channel_env_mode_;
      case 0x16: return channel_tone_release_;
      case 0x17: return channel_env_irq_;
      case 0x18: return channel_pitch_bend_;
      default: return 0;
    }
  }
  return 0;
}

void Spu::Write(u32 addr, u16 val) {
  const u32 bank = addr & 0xFF00;
  if (bank == 0x3000) {
    Channel& c = ch_[(addr >> 4) & 0xF];
    switch (addr & 0xF) {
      case 0x0:
        c.wave_address = (c.wave_address & 0x3F0000) | val;
        c.wave_shift = 0;
        return;
      case 0x1:
        c.mode = val & 0xF000;
        c.wave_address = (((u32)val & 0x3F) << 16) | (c.wave_address & 0xFFFF);
        c.loop_address = (((u32)(val >> 6) & 0x3F) << 16) | (c.loop_address & 0xFFFF);
        return;
      case 0x2: c.loop_address = (c.loop_address & 0x3F0000) | val; return;
      case 0x3: c.pan = val & 0x7F7F; return;
      case 0x4: c.envelope0 = val & 0x7FFF; return;
      case 0x5: c.envelope_data = val & 0xFF7F; return;
      case 0x6: c.envelope1 = val; return;
      case 0x7:
        c.envelope_irq = val & 0xFFC0;
        c.envelope_address = (((u32)val & 0x3F) << 16) | (c.envelope_address & 0xFFFF);
        return;
      case 0x8: c.envelope_address = (c.envelope_address & 0x3F0000) | val; return;
      case 0x9: c.wave_data_0 = val; return;
      case 0xA: c.envelope_loop_control = val; return;
      case 0xB: c.wave_data = val; return;
      default: return;
    }
  }
  if (bank == 0x3200) {
    Channel& c = ch_[(addr >> 4) & 0xF];
    switch (addr & 0xF) {
      case 0x0: c.phase = (((u32)val & 7) << 16) | (c.phase & 0xFFFF); return;
      case 0x1: c.phase_acc = (((u32)val & 7) << 16) | (c.phase_acc & 0xFFFF); return;
      case 0x2: c.target_phase = (((u32)val & 7) << 16) | (c.target_phase & 0xFFFF); return;
      case 0x3: c.rampdown_clk = val & 7; return;
      case 0x4: c.phase = (c.phase & 0x70000) | val; return;
      case 0x5: c.phase_acc = (c.phase_acc & 0x70000) | val; return;
      case 0x6: c.target_phase = (c.target_phase & 0x70000) | val; return;
      case 0x7: c.pitch_bend_control = val; return;
      default: return;
    }
  }
  if (bank == 0x3400) {
    switch (addr & 0xFF) {
      case 0x00: {
        const u16 old = channel_enable_;
        channel_enable_ = val;
        for (int i = 0; i < 16; i++) {
          if (((old ^ channel_enable_) & (1 << i)) == 0) continue;
          if (channel_stop_ & (1 << i)) continue;
          if (channel_enable_ & (1 << i)) StartChannel(i);
          else StopChannel(i);
        }
        return;
      }
      case 0x01: main_volume_ = val & 0x7F; return;
      case 0x02: channel_fiq_enable_ = val; return;
      case 0x03:
        channel_fiq_status_ &= (u16)~val;
        UpdateChannelIrq();
        return;
      case 0x04:
        beat_base_count_ = val & 0xFFF;
        current_beat_base_count_ = beat_base_count_;
        return;
      case 0x05: {
        const bool old_status = beat_count_ & 0x4000;
        beat_count_ = val;
        beat_count_ = (beat_count_ & (u16)~0x4000) |
                      ((old_status && !(val & 0x4000)) ? 0x4000 : 0);
        UpdateBeatIrq();
        return;
      }
      case 0x06:
        for (int i = 0; i < 4; i++) ch_[i].env_clk = (val >> (i * 4)) & 0xF;
        return;
      case 0x07:
        for (int i = 0; i < 4; i++) ch_[4 + i].env_clk = (val >> (i * 4)) & 0xF;
        return;
      case 0x08:
        for (int i = 0; i < 4; i++) ch_[8 + i].env_clk = (val >> (i * 4)) & 0xF;
        return;
      case 0x09:
        for (int i = 0; i < 4; i++) ch_[12 + i].env_clk = (val >> (i * 4)) & 0xF;
        return;
      case 0x0A:
        channel_env_rampdown_ = val & (channel_enable_ & (u16)~channel_stop_);
        return;
      case 0x0B: {
        const u16 old = channel_stop_;
        channel_stop_ &= (u16)~val;
        for (int i = 0; i < 16; i++) {
          if (((old ^ channel_stop_) & (1 << i)) == 0) continue;
          if ((channel_enable_ & (1 << i)) && !(channel_stop_ & (1 << i))) StartChannel(i);
        }
        return;
      }
      case 0x0C: channel_zero_cross_ = val; return;
      case 0x0D: control_ = (control_ & 0x20) | (val & 0x388); return;
      case 0x10: wave_in_l_ = val; return;
      case 0x11: wave_in_r_ = val; return;
      case 0x14: channel_repeat_ = val; return;
      case 0x15: channel_env_mode_ = val; return;
      case 0x16: channel_tone_release_ = val; return;
      case 0x17:
        channel_env_irq_ &= (u16)~val;
        UpdateBeatIrq();
        return;
      case 0x18: channel_pitch_bend_ = val; return;
      default: return;
    }
  }
}

}  // namespace dsmile
