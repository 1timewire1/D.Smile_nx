#include <jni.h>
#include <android/log.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <oboe/Oboe.h>

#include "core/vsmile.h"

#define LOG_TAG "dsmile"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace dsmile;

namespace {

constexpr int kOutRate = 48000;
constexpr double kInRate = 281250.0;
// Audio ring (stereo frames). ~128 ms — pacing reservoir + jitter absorption.
constexpr int kRingFrames = 6144;

class Emulator {
 public:
  bool Init(const u8* cart, size_t cart_size, const u8* sysrom, size_t sysrom_size, bool pal,
            bool play_intro) {
    vs_ = std::make_unique<VSmile>();
    if (!vs_->LoadCart(cart, cart_size)) return false;
    if (sysrom && sysrom_size > 0) vs_->LoadSysrom(sysrom, sysrom_size);
    vs_->SetVtechLogo(play_intro);
    vs_->SetAccurate(accurate_);
    pal_ = pal;
    vs_->Reset(pal);
    frames_per_second_ = pal ? 50.08 : 60.05;
    return true;
  }

  void Start() {
    if (running_.exchange(true)) return;
    StartAudio();
    thread_ = std::thread([this] { Loop(); });
  }

  void Stop() {
    if (!running_.exchange(false)) return;
    ring_cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    StopAudio();
  }

  ~Emulator() { Stop(); }

  void SetPaused(bool p) { paused_.store(p); }
  void SetAccurate(bool a) {
    std::lock_guard<std::mutex> lk(core_mutex_);
    accurate_ = a;
    if (vs_) vs_->SetAccurate(a);
  }
  void SetFastForward(bool ff) { fast_forward_.store(ff); }
  void SetFastForwardSpeed(float s) { ff_speed_.store(s); }  // 0 = uncapped
  void SetRewind(bool r) { rewind_.store(r); }
  // Lock-free: the UI thread must never block on the core lock (it starves
  // during fast-forward). The emu thread applies this every frame instead.
  void SetInput(int x, int y, u32 buttons) {
    const u64 packed = ((u64)(u16)(s16)x << 48) | ((u64)(u16)(s16)y << 32) |
                       (buttons & 0xFFFFFFFFu);
    input_state_.store(packed);
  }

  void Reset() {
    std::lock_guard<std::mutex> lk(core_mutex_);
    if (vs_) vs_->Reset(pal_);
  }

  int CopyFrame(u16* dst) {
    std::lock_guard<std::mutex> lk(frame_mutex_);
    std::memcpy(dst, frame_copy_, sizeof(frame_copy_));
    return frame_seq_.load();
  }

  bool SaveState(std::vector<u8>& out) {
    std::lock_guard<std::mutex> lk(core_mutex_);
    if (!vs_) return false;
    vs_->SaveState(out);
    return true;
  }

  bool LoadState(const u8* data, size_t n) {
    std::lock_guard<std::mutex> lk(core_mutex_);
    return vs_ && vs_->LoadState(data, n);
  }

  u8 Leds() {
    return leds_.load();
  }

 private:
  void Loop() {
    auto next_frame = std::chrono::steady_clock::now();
    const auto frame_dur = std::chrono::nanoseconds((long long)(1e9 / frames_per_second_));
    int rewind_push_counter = 0;

    while (running_.load()) {
      if (paused_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        next_frame = std::chrono::steady_clock::now();
        continue;
      }

      const bool rewinding = rewind_.load();
      {
        std::lock_guard<std::mutex> lk(core_mutex_);
        if (rewinding) {
          if (!rewind_buf_.empty()) {
            vs_->LoadState(rewind_buf_.back().data(), rewind_buf_.back().size());
            rewind_buf_.pop_back();
          }
          vs_->RunFrame();
          vs_->DrainAudio(spu_buf_, kSpuBufMax);  // discard audio while rewinding
        } else {
          // Apply the latest held input every frame so held directions survive
          // any controller re-sync and never depend on a UI event arriving.
          const u64 in = input_state_.load();
          vs_->SetInput((s16)(in >> 48), (s16)(in >> 32), (u32)(in & 0xFFFFFFFF));
          vs_->RunFrame();
          if (++rewind_push_counter >= 1) {
            rewind_push_counter = 0;
            rewind_buf_.emplace_back();
            vs_->SaveState(rewind_buf_.back());
            if (rewind_buf_.size() > kRewindMax) rewind_buf_.pop_front();
          }
        }
        // publish frame + controller LED state
        {
          std::lock_guard<std::mutex> flk(frame_mutex_);
          std::memcpy(frame_copy_, vs_->Framebuffer(), sizeof(frame_copy_));
          frame_seq_.fetch_add(1);
        }
        leds_.store(vs_->Leds());
      }

      if (!rewinding) {
        const int n = [&] {
          std::lock_guard<std::mutex> lk(core_mutex_);
          return vs_->DrainAudio(spu_buf_, kSpuBufMax);
        }();
        PushResampled(spu_buf_, n / 2);
      }

      if (fast_forward_.load()) {
        const float speed = ff_speed_.load();
        if (speed <= 0.0f) continue;  // uncapped
        next_frame += std::chrono::nanoseconds((long long)(1e9 / (frames_per_second_ * speed)));
        const auto now = std::chrono::steady_clock::now();
        if (next_frame < now) next_frame = now;  // don't bank time
        std::this_thread::sleep_until(next_frame);
        continue;
      }

      // Audio-pace only while the stream is genuinely draining. If the output
      // device changed (or the callback stalls for any reason), fall back to a
      // real-time clock so gameplay stays smooth instead of starving on the CV.
      const bool audio_healthy =
          audio_ok_.load() && (NowNs() - last_cb_ns_.load() < 200'000'000LL);
      if (audio_healthy && !rewinding) {
        std::unique_lock<std::mutex> lk(ring_mutex_);
        ring_cv_.wait_for(lk, std::chrono::milliseconds(40), [this] {
          return !running_.load() || RingFree() >= 820 * 2;
        });
        next_frame = std::chrono::steady_clock::now();  // keep clock baseline fresh
      } else {
        next_frame += frame_dur;
        const auto now = std::chrono::steady_clock::now();
        if (next_frame < now) next_frame = now;  // don't bank time after a stall
        std::this_thread::sleep_until(next_frame);
      }
    }
  }

  // ---- audio ----
  int RingCount() const { return ring_count_; }
  int RingFree() const { return kRingFrames * 2 - ring_count_; }

  void PushResampled(const s16* in, int in_frames) {
    if (in_frames <= 0) return;
    const double step = kInRate / kOutRate;
    std::lock_guard<std::mutex> lk(ring_mutex_);
    // resample_pos_ is the fractional input-frame index of the next output,
    // relative to in[0]; range starts at -1 (the held tail of the previous buffer).
    while (resample_pos_ < in_frames - 1) {
      const int i0 = (int)std::floor(resample_pos_);
      const double frac = resample_pos_ - i0;
      const s16 l0 = (i0 < 0) ? resample_prev_l_ : in[i0 * 2];
      const s16 r0 = (i0 < 0) ? resample_prev_r_ : in[i0 * 2 + 1];
      const s16 l1 = in[(i0 + 1) * 2];
      const s16 r1 = in[(i0 + 1) * 2 + 1];
      if (ring_count_ <= kRingFrames * 2 - 2) {
        ring_[ring_write_] = (s16)(l0 + (l1 - l0) * frac);
        ring_[(ring_write_ + 1) % (kRingFrames * 2)] = (s16)(r0 + (r1 - r0) * frac);
        ring_write_ = (ring_write_ + 2) % (kRingFrames * 2);
        ring_count_ += 2;
      }
      resample_pos_ += step;
    }
    resample_prev_l_ = in[(in_frames - 1) * 2];
    resample_prev_r_ = in[(in_frames - 1) * 2 + 1];
    resample_pos_ -= in_frames;
  }

  static int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  class Callback : public oboe::AudioStreamDataCallback {
   public:
    explicit Callback(Emulator& e) : e_(e) {}
    oboe::DataCallbackResult onAudioReady(oboe::AudioStream*, void* data, int32_t frames) override {
      e_.last_cb_ns_.store(NowNs());  // heartbeat for the pacing watchdog
      s16* out = (s16*)data;
      std::lock_guard<std::mutex> lk(e_.ring_mutex_);
      for (int i = 0; i < frames * 2; i++) {
        if (e_.ring_count_ > 0) {
          out[i] = e_.ring_[e_.ring_read_];
          e_.ring_read_ = (e_.ring_read_ + 1) % (kRingFrames * 2);
          e_.ring_count_--;
        } else {
          out[i] = 0;
        }
      }
      e_.ring_cv_.notify_one();
      return oboe::DataCallbackResult::Continue;
    }
   private:
    Emulator& e_;
  };

  // Fired when the output route changes (headphones, Bluetooth, dock, etc.).
  // Oboe closes the disconnected stream; we open a fresh one on the new device.
  class ErrorCallback : public oboe::AudioStreamErrorCallback {
   public:
    explicit ErrorCallback(Emulator& e) : e_(e) {}
    void onErrorBeforeClose(oboe::AudioStream*, oboe::Result) override {
      e_.audio_ok_.store(false);  // stop audio-pacing immediately -> clock pace
    }
    void onErrorAfterClose(oboe::AudioStream*, oboe::Result) override {
      e_.ReopenAudio();
    }
   private:
    Emulator& e_;
  };

  bool OpenAudioStream() {
    oboe::AudioStreamBuilder b;
    b.setDirection(oboe::Direction::Output)
        ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
        ->setSharingMode(oboe::SharingMode::Shared)
        ->setFormat(oboe::AudioFormat::I16)
        ->setChannelCount(2)
        ->setSampleRate(kOutRate)
        ->setUsage(oboe::Usage::Game)
        ->setDataCallback(callback_.get())
        ->setErrorCallback(error_callback_.get());
    const auto res = b.openStream(stream_);
    if (res == oboe::Result::OK && stream_) {
      stream_->requestStart();
      last_cb_ns_.store(NowNs());  // grace period before the watchdog trips
      audio_ok_.store(true);
      return true;
    }
    LOGE("oboe open failed: %s", oboe::convertToText(res));
    audio_ok_.store(false);
    return false;
  }

  void StartAudio() {
    callback_ = std::make_unique<Callback>(*this);
    error_callback_ = std::make_unique<ErrorCallback>(*this);
    std::lock_guard<std::mutex> lk(audio_mutex_);
    audio_shutdown_ = false;
    OpenAudioStream();
  }

  void ReopenAudio() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    if (audio_shutdown_) return;
    stream_.reset();  // Oboe already closed it before this callback
    for (int attempt = 0; attempt < 3 && !audio_shutdown_; attempt++) {
      if (OpenAudioStream()) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ring_cv_.notify_all();
  }

  void StopAudio() {
    std::lock_guard<std::mutex> lk(audio_mutex_);
    audio_shutdown_ = true;
    if (stream_) {
      stream_->requestStop();
      stream_->close();
      stream_.reset();
    }
    audio_ok_.store(false);
  }

  std::unique_ptr<VSmile> vs_;
  bool pal_ = false;
  bool accurate_ = false;
  double frames_per_second_ = 60.05;
  std::mutex core_mutex_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> fast_forward_{false};
  std::atomic<float> ff_speed_{3.0f};
  std::atomic<bool> rewind_{false};
  std::atomic<u8> leds_{0};
  std::atomic<u64> input_state_{0};  // packed joy x:16 | y:16 | buttons:32

  // published frame
  std::mutex frame_mutex_;
  u16 frame_copy_[320 * 240]{};
  std::atomic<int> frame_seq_{0};

  // SPU drain scratch: one NTSC frame is ~4682 pairs; leave headroom.
  static constexpr int kSpuBufMax = 32768;
  s16 spu_buf_[kSpuBufMax];

  // audio ring
  std::mutex ring_mutex_;
  std::condition_variable ring_cv_;
  s16 ring_[kRingFrames * 2]{};
  int ring_read_ = 0, ring_write_ = 0, ring_count_ = 0;
  double resample_pos_ = -1.0;
  s16 resample_prev_l_ = 0, resample_prev_r_ = 0;
  std::atomic<bool> audio_ok_{false};
  std::atomic<int64_t> last_cb_ns_{0};  // when the audio callback last ran
  std::mutex audio_mutex_;
  bool audio_shutdown_ = false;
  std::shared_ptr<oboe::AudioStream> stream_;
  std::unique_ptr<Callback> callback_;
  std::unique_ptr<ErrorCallback> error_callback_;

  // rewind
  static constexpr size_t kRewindMax = 900;  // ~15 s at 60 snapshots/s (1x reverse)
  std::deque<std::vector<u8>> rewind_buf_;
};

std::unique_ptr<Emulator> g_emu;

std::vector<u8> JBytes(JNIEnv* env, jbyteArray arr) {
  if (!arr) return {};
  const jsize n = env->GetArrayLength(arr);
  std::vector<u8> v(n);
  env->GetByteArrayRegion(arr, 0, n, (jbyte*)v.data());
  return v;
}

}  // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeInit(JNIEnv* env, jobject, jbyteArray cart,
                                                   jbyteArray sysrom, jboolean pal,
                                                   jboolean playIntro) {
  g_emu = std::make_unique<Emulator>();
  const auto cart_v = JBytes(env, cart);
  const auto sys_v = JBytes(env, sysrom);
  if (!g_emu->Init(cart_v.data(), cart_v.size(), sys_v.empty() ? nullptr : sys_v.data(),
                   sys_v.size(), pal, playIntro)) {
    g_emu.reset();
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeStart(JNIEnv*, jobject) {
  if (g_emu) g_emu->Start();
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeStop(JNIEnv*, jobject) {
  if (g_emu) g_emu->Stop();
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeDestroy(JNIEnv*, jobject) {
  g_emu.reset();
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSetPaused(JNIEnv*, jobject, jboolean p) {
  if (g_emu) g_emu->SetPaused(p);
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSetFastForward(JNIEnv*, jobject, jboolean f) {
  if (g_emu) g_emu->SetFastForward(f);
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSetFastForwardSpeed(JNIEnv*, jobject, jfloat s) {
  if (g_emu) g_emu->SetFastForwardSpeed(s);
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSetAccurate(JNIEnv*, jobject, jboolean a) {
  if (g_emu) g_emu->SetAccurate(a);
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSetRewind(JNIEnv*, jobject, jboolean r) {
  if (g_emu) g_emu->SetRewind(r);
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSetInput(JNIEnv*, jobject, jint x, jint y,
                                                       jint buttons) {
  if (g_emu) g_emu->SetInput(x, y, (u32)buttons);
}

JNIEXPORT void JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeReset(JNIEnv*, jobject) {
  if (g_emu) g_emu->Reset();
}

// Copies the latest frame (320*240 RGB565) into `buf` (direct ByteBuffer).
// Returns a frame sequence number.
JNIEXPORT jint JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeGetFrame(JNIEnv* env, jobject, jobject buf) {
  if (!g_emu) return -1;
  void* p = env->GetDirectBufferAddress(buf);
  if (!p || env->GetDirectBufferCapacity(buf) < 320 * 240 * 2) return -1;
  return g_emu->CopyFrame((u16*)p);
}

// V.Smile controller LED bitmask: bit0 green, bit1 blue, bit2 yellow, bit3 red.
JNIEXPORT jint JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeGetLeds(JNIEnv*, jobject) {
  return g_emu ? g_emu->Leds() : 0;
}

JNIEXPORT jbyteArray JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeSaveState(JNIEnv* env, jobject) {
  if (!g_emu) return nullptr;
  std::vector<u8> state;
  if (!g_emu->SaveState(state)) return nullptr;
  jbyteArray arr = env->NewByteArray((jsize)state.size());
  env->SetByteArrayRegion(arr, 0, (jsize)state.size(), (const jbyte*)state.data());
  return arr;
}

JNIEXPORT jboolean JNICALL
Java_com_dsmile_emulator_emu_NativeCore_nativeLoadState(JNIEnv* env, jobject, jbyteArray data) {
  if (!g_emu || !data) return JNI_FALSE;
  const auto v = JBytes(env, data);
  return g_emu->LoadState(v.data(), v.size()) ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
