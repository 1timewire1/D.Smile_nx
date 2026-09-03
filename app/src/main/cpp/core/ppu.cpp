#include "ppu.h"

#include "spg200.h"
#include "state.h"

namespace dsmile {

namespace {
constexpr u16 kTransparent = 0x8000;

inline u32 LineSegmentAddr(u16 segment_ptr, int ch, int tile_y, int tile_w, int tile_h, int bpp) {
  return ((u32)segment_ptr << 6) + (u32)(ch * tile_h + tile_y) * tile_w * bpp / 16;
}

inline int BlendMix(int old_v, int new_v, int level) {
  return (old_v * (4 - (level + 1))) / 4 + (new_v * (level + 1)) / 4;
}

inline u16 Rgb555To565(u16 v) {
  const u16 r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
  return (u16)((r << 11) | ((((g << 1) | (g >> 4)) & 63) << 5) | b);
}
}  // namespace

void Ppu::Reset(bool pal) {
  total_lines_ = pal ? 312 : 262;
  scanline_clock_ = CycleClock((pal ? 432 : 429) * 4);
  cur_scanline_ = 0;
  std::memset(framebuffer_, 0, sizeof(framebuffer_));
  std::memset(bg_, 0, sizeof(bg_));
  std::memset(sprites_, 0, sizeof(sprites_));
  std::memset(line_scroll_, 0, sizeof(line_scroll_));
  std::memset(line_compress_, 0, sizeof(line_compress_));
  std::memset(palette_, 0, sizeof(palette_));
  sprite_segment_ptr_ = 0;
  blend_level_ = 0;
  vcompress_amount_ = 0x20;
  vcompress_offset_ = 0;
  fade_level_ = 0;
  tv_control_ = 0x0020;
  sprite_ctrl_ = 0;
  stn_lcd_ = 0;
  irq_vpos_ = irq_hpos_ = 0x1FF;
  irq_ctrl_ = irq_status_ = 0;
  sprite_dma_source_ = sprite_dma_target_ = 0;
  lut_dirty_ = true;
  UpdateIrq();
}

void Ppu::SaveState(StateWriter& w) const {
  w.S64(scanline_clock_.counter);
  w.U32((u32)cur_scanline_);
  for (int i = 0; i < 2; i++) {
    const BgData& b = bg_[i];
    w.U16(b.xscroll); w.U16(b.yscroll); w.U16(b.attr); w.U16(b.ctrl);
    w.U16(b.tile_map_ptr); w.U16(b.attribute_map_ptr); w.U16(b.segment_ptr);
  }
  for (const Sprite& s : sprites_) { w.U16(s.ch); w.U16(s.xpos); w.U16(s.ypos); w.U16(s.attr); }
  w.Arr16(line_scroll_, 256);
  w.Arr16(line_compress_, 256);
  w.Arr16(palette_, 256);
  w.U16(sprite_segment_ptr_); w.U16(blend_level_);
  w.U16(vcompress_amount_); w.U16(vcompress_offset_);
  w.U16(fade_level_); w.U16(sprite_ctrl_); w.U16(stn_lcd_);
  w.U16(tv_control_);
  w.U16(irq_vpos_); w.U16(irq_hpos_);
  w.U16(irq_ctrl_); w.U16(irq_status_);
  w.U16(sprite_dma_source_); w.U16(sprite_dma_target_);
}

void Ppu::LoadState(StateReader& r) {
  scanline_clock_.counter = r.S64();
  cur_scanline_ = (int)r.U32();
  for (int i = 0; i < 2; i++) {
    BgData& b = bg_[i];
    b.xscroll = r.U16(); b.yscroll = r.U16(); b.attr = r.U16(); b.ctrl = r.U16();
    b.tile_map_ptr = r.U16(); b.attribute_map_ptr = r.U16(); b.segment_ptr = r.U16();
  }
  for (Sprite& s : sprites_) { s.ch = r.U16(); s.xpos = r.U16(); s.ypos = r.U16(); s.attr = r.U16(); }
  r.Arr16(line_scroll_, 256);
  r.Arr16(line_compress_, 256);
  r.Arr16(palette_, 256);
  sprite_segment_ptr_ = r.U16(); blend_level_ = r.U16();
  vcompress_amount_ = r.U16(); vcompress_offset_ = r.U16();
  fade_level_ = r.U16(); sprite_ctrl_ = r.U16(); stn_lcd_ = r.U16();
  tv_control_ = r.U16();
  irq_vpos_ = r.U16(); irq_hpos_ = r.U16();
  irq_ctrl_ = r.U16(); irq_status_ = r.U16();
  sprite_dma_source_ = r.U16(); sprite_dma_target_ = r.U16();
  lut_dirty_ = true;
  UpdateIrq();
}

bool Ppu::RunCycles(int cycles) {
  bool frame_finished = false;
  const int fired = scanline_clock_.Tick(cycles);
  for (int i = 0; i < fired; i++) {
    if (cur_scanline_ == irq_vpos_ && (irq_ctrl_ & 2)) {
      irq_status_ |= 2;
      UpdateIrq();
    }
    if (cur_scanline_ < 240) {
      DrawLine(cur_scanline_);
      if (cur_scanline_ == 239) {
        if (irq_ctrl_ & 1) {  // vblank status only latches while enabled
          irq_status_ |= 1;
          UpdateIrq();
        }
        frame_finished = true;
      }
      cur_scanline_++;
    } else if (cur_scanline_ >= total_lines_ - 1) {
      irq_status_ &= (u16)~1;
      UpdateIrq();
      cur_scanline_ = 0;
    } else {
      cur_scanline_++;
    }
  }
  return frame_finished;
}

void Ppu::UpdateIrq() {
  bus_.SetPpuIrqLine((irq_ctrl_ & irq_status_ & 7) != 0);
}

void Ppu::StartSpriteDma(u16 length) {
  // Length is NOT masked: a full-table upload is written as 0x400.
  while (length--) {
    const u16 word = bus_.Read(sprite_dma_source_++);
    const u16 t = sprite_dma_target_;
    Sprite& s = sprites_[(t & 0x3FF) >> 2];
    switch (t & 3) {
      case 0: s.ch = word; break;
      case 1: s.xpos = word & 0x1FF; break;
      case 2: s.ypos = word & 0x1FF; break;
      default: s.attr = word & 0x7FFF; break;
    }
    sprite_dma_target_ = (t + 1) & 0x3FF;
  }
  if (irq_ctrl_ & 4) {
    irq_status_ |= 4;
    UpdateIrq();
  }
}

u16 Ppu::Read(u32 addr) {
  const u32 a = addr & 0x7FF;
  if (a >= 0x400) {  // 0x2C00-0x2FFF sprite memory
    const Sprite& s = sprites_[(a & 0x3FF) >> 2];
    switch (a & 3) {
      case 0: return s.ch;
      case 1: return s.xpos;
      case 2: return s.ypos;
      default: return s.attr;
    }
  }
  if (a >= 0x300) return palette_[a & 0xFF];
  if (a >= 0x200) return line_compress_[a & 0xFF];
  if (a >= 0x100) return line_scroll_[a & 0xFF];
  switch (a) {
    case 0x10: return bg_[0].xscroll;
    case 0x11: return bg_[0].yscroll;
    case 0x12: return bg_[0].attr;
    case 0x13: return bg_[0].ctrl;
    case 0x14: return bg_[0].tile_map_ptr;
    case 0x15: return bg_[0].attribute_map_ptr;
    case 0x16: return bg_[1].xscroll;
    case 0x17: return bg_[1].yscroll;
    case 0x18: return bg_[1].attr;
    case 0x19: return bg_[1].ctrl;
    case 0x1A: return bg_[1].tile_map_ptr;
    case 0x1B: return bg_[1].attribute_map_ptr;
    case 0x1C: return vcompress_amount_;
    case 0x1D: return vcompress_offset_;
    case 0x20: return bg_[0].segment_ptr;
    case 0x21: return bg_[1].segment_ptr;
    case 0x22: return sprite_segment_ptr_;
    case 0x2A: return blend_level_;
    case 0x30: return fade_level_;
    case 0x3C: return tv_control_;
    case 0x36: return irq_vpos_;
    case 0x37: return irq_hpos_;
    case 0x38: return (u16)cur_scanline_;
    case 0x42: return sprite_ctrl_;
    case 0x54: return stn_lcd_;
    case 0x62: return irq_ctrl_;
    case 0x63: return irq_status_;
    case 0x70: return sprite_dma_source_;
    case 0x71: return sprite_dma_target_;
    case 0x72: return 0;  // remaining DMA length
    default: return 0;
  }
}

void Ppu::Write(u32 addr, u16 val) {
  const u32 a = addr & 0x7FF;
  if (a >= 0x400) {
    Sprite& s = sprites_[(a & 0x3FF) >> 2];
    switch (a & 3) {
      case 0: s.ch = val; break;
      case 1: s.xpos = val & 0x1FF; break;
      case 2: s.ypos = val & 0x1FF; break;
      default: s.attr = val & 0x7FFF; break;
    }
    return;
  }
  if (a >= 0x300) { palette_[a & 0xFF] = val; return; }
  if (a >= 0x200) { line_compress_[a & 0xFF] = val; return; }
  if (a >= 0x100) { line_scroll_[a & 0xFF] = val & 0x1FF; return; }
  switch (a) {
    case 0x10: bg_[0].xscroll = val & 0x1FF; return;
    case 0x11: bg_[0].yscroll = val & 0xFF; return;
    case 0x12: bg_[0].attr = val & 0x3FFF; return;
    case 0x13: bg_[0].ctrl = val & 0x01FF; return;
    case 0x14: bg_[0].tile_map_ptr = val & 0x3FFF; return;
    case 0x15: bg_[0].attribute_map_ptr = val & 0x3FFF; return;
    case 0x16: bg_[1].xscroll = val & 0x1FF; return;
    case 0x17: bg_[1].yscroll = val & 0xFF; return;
    case 0x18: bg_[1].attr = val & 0x3FFF; return;
    case 0x19: bg_[1].ctrl = val & 0x01FF; return;
    case 0x1A: bg_[1].tile_map_ptr = val & 0x3FFF; return;
    case 0x1B: bg_[1].attribute_map_ptr = val & 0x3FFF; return;
    case 0x1C: vcompress_amount_ = val & 0x1FF; return;
    case 0x1D: vcompress_offset_ = val & 0x1FFF; return;
    case 0x20: bg_[0].segment_ptr = val; return;
    case 0x21: bg_[1].segment_ptr = val; return;
    case 0x22: sprite_segment_ptr_ = val; return;
    case 0x2A: blend_level_ = val & 3; return;
    case 0x30: fade_level_ = val & 0xFF; return;
    case 0x3C: tv_control_ = val; return;
    case 0x36: irq_vpos_ = val & 0x1FF; return;
    case 0x37: irq_hpos_ = val & 0x1FF; return;
    case 0x42: sprite_ctrl_ = val & 3; return;
    case 0x54: stn_lcd_ = val & 0x3F; return;
    case 0x62:
      irq_ctrl_ = val & 7;
      UpdateIrq();
      return;
    case 0x63:
      irq_status_ &= (u16)~(val & 7);
      UpdateIrq();
      return;
    case 0x70: sprite_dma_source_ = val & 0x3FFF; return;
    case 0x71: sprite_dma_target_ = val & 0x3FF; return;
    case 0x72: StartSpriteDma(val); return;
    default: return;
  }
}

void Ppu::BuildPostLut() {
  const int fade = fade_level_ & 0xFF;
  const int sat = tv_control_ & 0xFF;
  const float factor = (float)(255 - sat) / (float)(255 - 0x20);
  for (u32 v = 0; v < 32768; v++) {
    const int r5 = (v >> 10) & 31, g5 = (v >> 5) & 31, b5 = v & 31;
    int r = (r5 << 3) | (r5 >> 2);
    int g = (g5 << 3) | (g5 >> 2);
    int b = (b5 << 3) | (b5 >> 2);
    if (sat != 0x20) {  // saturation adjust around luma
      const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
      r = (int)(luma + (r - luma) * factor + 0.5f);
      g = (int)(luma + (g - luma) * factor + 0.5f);
      b = (int)(luma + (b - luma) * factor + 0.5f);
    }
    r -= fade;  // fade subtracts from each channel post-conversion
    g -= fade;
    b -= fade;
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    post_lut_[v] = (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }
  lut_fade_ = fade_level_;
  lut_tv_ = tv_control_;
  lut_dirty_ = false;
}

void Ppu::DrawLine(int line) {
  for (auto& px : linebuf_) px = kTransparent;

  // Bucket sprites by (layer, blend) once instead of re-scanning all 256
  // slots for every layer/blend combination below (was 4 layers * 2 passes
  // * 256 = 2048 slot checks per scanline, regardless of how many slots are
  // actually in use). Sprite registers can't change mid-DrawLine (no bus
  // writes happen between here and the draws below), so one pass is enough;
  // appending in increasing index order within each bucket preserves the
  // exact draw order the old nested-scan version produced.
  u8 sprite_idx[4][2][256];
  int sprite_n[4][2] = {};
  if (sprite_ctrl_ & 1) {
    for (int i = 0; i < 256; i++) {
      const Sprite& s = sprites_[i];
      if (!s.ch) continue;
      const int layer = (s.attr >> 12) & 3;
      const int blend = (s.attr & 0x4000) ? 1 : 0;
      sprite_idx[layer][blend][sprite_n[layer][blend]++] = (u8)i;
    }
  }

  for (int layer = 0; layer < 4; layer++) {
    for (int bg = 0; bg < 2; bg++) {
      if ((bg_[bg].ctrl & 0x08) && ((bg_[bg].attr >> 12) & 3) == layer) {
        DrawBgScanline(bg, line);
      }
    }
    if (sprite_ctrl_ & 1) {
      for (int k = 0; k < sprite_n[layer][0]; k++) DrawSpriteScanline(sprite_idx[layer][0][k], line);
      for (int k = 0; k < sprite_n[layer][1]; k++) DrawSpriteScanline(sprite_idx[layer][1][k], line);
    }
  }

  u16* out = &framebuffer_[line * 320];
  if (accurate_) {
    if (lut_dirty_ || lut_fade_ != fade_level_ || lut_tv_ != tv_control_) BuildPostLut();
    for (int x = 0; x < 320; x++) {
      const u16 px = linebuf_[x];
      out[x] = post_lut_[(px & kTransparent) ? 0 : (px & 0x7FFF)];
    }
  } else {
    for (int x = 0; x < 320; x++) {
      const u16 px = linebuf_[x];
      out[x] = (px & kTransparent) ? 0 : Rgb555To565(px);
    }
  }
}

void Ppu::DrawBgScanline(int bg_index, int screen_y) {
  const BgData& bg = bg_[bg_index];
  const u16 ctrl = bg.ctrl;
  const u16 attr = bg.attr;

  int virtual_y = screen_y;
  if (ctrl & 0x40) {  // vcompress
    const int offset = sext(vcompress_offset_, 13) + 128 - 128 * (int)vcompress_amount_ / 0x20;
    virtual_y = screen_y * (int)vcompress_amount_ / 0x20 + offset;
  }
  if (virtual_y < 0 || virtual_y >= 240) return;

  const int tilemap_y = (virtual_y + bg.yscroll) & 0xFF;
  const int scroll_x = (bg.xscroll + ((ctrl & 0x10) ? line_scroll_[tilemap_y] : 0)) & 0x1FF;

  if (ctrl & 0x01) {  // bitmap/linemap mode
    const u16 addr_lo = bus_.Read((u32)bg.tile_map_ptr + tilemap_y);
    const u16 addr_hi =
        bus_.Read((u32)bg.attribute_map_ptr + tilemap_y / 2) >> ((tilemap_y & 1) ? 8 : 0);
    const u32 addr = addr_lo | ((u32)(addr_hi & 0xFF) << 16);
    const int bpp = (ctrl & 0x80) ? 16 : ((attr & 3) + 1) * 2;
    for (int screen_x = -scroll_x; screen_x < 320; screen_x += 512) {
      DrawTileLine(screen_y, screen_x, addr, 512, (attr >> 8) & 0xF, false, bpp, ctrl & 0x100);
    }
    return;
  }

  const int hsize = (attr >> 4) & 3, vsize = (attr >> 6) & 3;
  const int tile_w = 8 << hsize, tile_h = 8 << vsize;
  const int tilemap_ytile = tilemap_y / tile_h;
  const int tiles_per_row = 512 >> (hsize + 3);

  for (int screen_x = -(scroll_x % tile_w); screen_x < 320; screen_x += tile_w) {
    const int tilemap_x = (screen_x + scroll_x) & 0x1FF;
    const int tilemap_xtile = tilemap_x / tile_w;
    const int tilepos = (ctrl & 0x04) ? 0 : tiles_per_row * tilemap_ytile + tilemap_xtile;

    const u16 ch = bus_.Read((u32)bg.tile_map_ptr + tilepos);
    if (!ch) continue;

    unsigned palette = (attr >> 8) & 0xF;
    bool vflip = attr & 0x08, hflip = attr & 0x04, blend = ctrl & 0x100;
    if (!(ctrl & 0x02)) {  // attributes from attribute map
      const u16 attr_word = bus_.Read((u32)bg.attribute_map_ptr + (tilepos >> 1));
      const u8 ta = (u8)(attr_word >> ((tilepos & 1) ? 8 : 0));
      palette = ta & 0xF;
      hflip = ta & 0x10;
      vflip = ta & 0x20;
      blend = ta & 0x40;
    }

    const int tile_y = !vflip ? tilemap_y % tile_h : tile_h - (tilemap_y % tile_h) - 1;
    const int bpp = ((attr & 3) + 1) * 2;
    const u32 addr = LineSegmentAddr(bg.segment_ptr, ch, tile_y, tile_w, tile_h, bpp);
    DrawTileLine(screen_y, screen_x, addr, tile_w, palette, hflip, bpp, blend);
  }
}

void Ppu::DrawSpriteScanline(int index, int screen_y) {
  const Sprite& s = sprites_[index];
  const int hsize = (s.attr >> 4) & 3, vsize = (s.attr >> 6) & 3;
  const int tile_w = 8 << hsize, tile_h = 8 << vsize;
  int xpos, ypos;
  if (sprite_ctrl_ & 2) {  // top-left origin mode
    xpos = sext(s.xpos, 9);
    ypos = sext(s.ypos, 9);
  } else {  // centered, y-up
    xpos = (160 + sext(s.xpos, 9)) - tile_w / 2;
    ypos = (128 - sext(s.ypos, 9)) - tile_h / 2;
  }
  const int bpp = ((s.attr & 3) + 1) * 2;
  const bool vflip = s.attr & 0x08;
  const int tile_y = !vflip ? (screen_y - ypos) : (tile_h - 1) - (screen_y - ypos);
  if (tile_y < 0 || tile_y >= tile_h) return;

  const u32 addr = LineSegmentAddr(sprite_segment_ptr_, s.ch, tile_y, tile_w, tile_h, bpp);
  DrawTileLine(screen_y, xpos, addr, tile_w, (s.attr >> 8) & 0xF, s.attr & 0x04, bpp,
               s.attr & 0x4000);
}

void Ppu::DrawTileLine(int screen_y, int x_start, u32 line_addr, int tile_w, unsigned palette,
                       bool hflip, unsigned bpp, bool blend) {
  int pixbuf_shift = -(int)bpp;
  u32 pixbuf = 0;
  u32 addr = line_addr + (hflip ? ((tile_w * bpp) / 16 - 1) : 0);

  // Skip bus reads that contain only offscreen pixels.
  const int left_offscreen = x_start < 0 ? -x_start : 0;
  int skipped_pixels = 0;
  if (left_offscreen > 0) {
    const int skipped_words = (left_offscreen * (int)bpp) / 16;
    if (skipped_words != 0) {
      addr += hflip ? -skipped_words : skipped_words;
      skipped_pixels = (skipped_words * 16 + bpp - 1) / bpp;
      pixbuf_shift -= (skipped_pixels * (int)bpp) % 16;
    }
  }

  for (int screen_x = x_start + skipped_pixels; screen_x < x_start + tile_w && screen_x < 320;
       screen_x++) {
    if (pixbuf_shift < 0) {
      u16 val = bus_.Read(addr);
      addr += hflip ? -1 : 1;
      if (bpp != 16) val = (u16)((val >> 8) | (val << 8));
      pixbuf = hflip ? ((u32)val << 16) | (pixbuf >> 16) : (pixbuf << 16) | val;
      pixbuf_shift += 16;
    }

    const int shift = hflip ? ((16 - (int)bpp) - pixbuf_shift) + 16 : pixbuf_shift;
    const u32 pixdata = (pixbuf >> shift) & ((1u << bpp) - 1);
    pixbuf_shift -= bpp;

    if (screen_x < 0) continue;

    u16 newpx;
    switch (bpp) {
      case 2:
      case 4: newpx = palette_[palette * 16 + pixdata]; break;
      case 6: newpx = palette_[(palette >> 2) * 64 + pixdata]; break;
      case 8: newpx = palette_[pixdata & 0xFF]; break;
      default: newpx = (u16)pixdata; break;  // 16bpp direct
    }

    if (newpx & kTransparent) continue;

    if (blend) {
      const u16 old = linebuf_[screen_x];
      if (!(old & kTransparent)) {
        const int r = BlendMix((old >> 10) & 31, (newpx >> 10) & 31, blend_level_);
        const int g = BlendMix((old >> 5) & 31, (newpx >> 5) & 31, blend_level_);
        const int b = BlendMix(old & 31, newpx & 31, blend_level_);
        newpx = (u16)((r << 10) | (g << 5) | b);
      }
    }
    linebuf_[screen_x] = newpx;
  }
}

}  // namespace dsmile
