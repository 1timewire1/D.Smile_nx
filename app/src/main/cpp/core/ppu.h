#pragma once
#include "common.h"

namespace dsmile {

class Spg200;

// SPG200 PPU: 2 tile/bitmap layers + 256 sprites, 320x240, scanline renderer.
class Ppu {
 public:
  Ppu(Spg200& bus) : bus_(bus), scanline_clock_(1716) {}

  void Reset(bool pal);
  bool RunCycles(int cycles);  // true when the visible frame just finished
  u16 Read(u32 addr);          // 0x2800-0x2FFF
  void Write(u32 addr, u16 val);
  int CurrentLine() const { return cur_scanline_; }
  // Framebuffer: RGB565, row-major 320x240.
  const u16* Framebuffer() const { return &framebuffer_[0]; }

  // Accurate mode adds the console's fade + saturation post-processing that
  // the fast path omits. Off = fast path stays byte-identical.
  void SetAccurate(bool a) { accurate_ = a; lut_dirty_ = true; }

  void SaveState(struct StateWriter& w) const;
  void LoadState(struct StateReader& r);

 private:
  struct BgData {
    u16 xscroll, yscroll;
    u16 attr, ctrl;
    u16 tile_map_ptr, attribute_map_ptr;
    u16 segment_ptr;
  };
  struct Sprite {
    u16 ch, xpos, ypos, attr;
  };

  void DrawLine(int line);
  void DrawBgScanline(int bg, int line);
  void DrawSpriteScanline(int sprite, int line);
  void DrawTileLine(int line, int x_start, u32 line_addr, int tile_width, unsigned palette,
                    bool hflip, unsigned bpp, bool blend);
  void UpdateIrq();
  void StartSpriteDma(u16 length);
  void BuildPostLut();  // 32K RGB555 -> RGB565 with saturation + fade baked in

  Spg200& bus_;
  CycleClock scanline_clock_;
  int total_lines_ = 262;
  int cur_scanline_ = 0;

  BgData bg_[2]{};
  Sprite sprites_[256]{};
  u16 line_scroll_[256]{};
  u16 line_compress_[256]{};
  u16 palette_[256]{};

  u16 sprite_segment_ptr_ = 0;
  u16 blend_level_ = 0;
  u16 vcompress_amount_ = 0x20, vcompress_offset_ = 0;
  u16 fade_level_ = 0;
  u16 tv_control_ = 0x0020;  // reg 0x283C: hue(hi) / saturation(lo), 0x20 = neutral
  u16 sprite_ctrl_ = 0;
  u16 stn_lcd_ = 0;
  u16 irq_vpos_ = 0x1FF, irq_hpos_ = 0x1FF;
  u16 irq_ctrl_ = 0, irq_status_ = 0;  // bit0 vblank, bit1 pos, bit2 dma
  u16 sprite_dma_source_ = 0, sprite_dma_target_ = 0;

  // Accurate-mode post-processing
  bool accurate_ = false;
  bool lut_dirty_ = true;
  u16 lut_fade_ = 0xFFFF, lut_tv_ = 0xFFFF;
  u16 post_lut_[32768]{};

  // Scanline work buffer in raw RGB555 (bit15 = still-transparent), converted
  // to RGB565 into framebuffer_ at end of line.
  u16 linebuf_[320]{};
  u16 framebuffer_[320 * 240]{};
};

}  // namespace dsmile
