// Composites a running game frame into a CRT TV on the V.Smile purple
// backdrop with a silver bezel, for README hero images. Host-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../app/src/main/cpp/core/vsmile.h"

using namespace dsmile;

struct Rgb { int r, g, b; };

static void WriteBmp(const char* path, const std::vector<Rgb>& px, int w, int h) {
  const int row = ((w * 3 + 3) / 4) * 4;
  const int size = 54 + row * h;
  std::vector<u8> bmp(size, 0);
  u8* p = bmp.data();
  p[0] = 'B'; p[1] = 'M';
  *(u32*)(p + 2) = size; *(u32*)(p + 10) = 54; *(u32*)(p + 14) = 40;
  *(s32*)(p + 18) = w; *(s32*)(p + 22) = -h;
  *(u16*)(p + 26) = 1; *(u16*)(p + 28) = 24;
  u8* out = p + 54;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const Rgb& c = px[y * w + x];
      u8* o = out + y * row + x * 3;
      o[0] = (u8)std::min(255, std::max(0, c.b));
      o[1] = (u8)std::min(255, std::max(0, c.g));
      o[2] = (u8)std::min(255, std::max(0, c.r));
    }
  FILE* f = fopen(path, "wb");
  fwrite(bmp.data(), 1, bmp.size(), f);
  fclose(f);
}

int main(int argc, char** argv) {
  const char* rom = argv[1];
  const int frames = argc > 2 ? atoi(argv[2]) : 1500;
  const char* mode = argc > 3 ? argv[3] : "crt";   // crt | clean
  const char* bg = argc > 4 ? argv[4] : "purple";  // purple | blue | black
  const char* out = argc > 5 ? argv[5] : "shot.bmp";

  FILE* f = fopen(rom, "rb");
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  std::vector<u8> data(size); fread(data.data(), 1, size, f); fclose(f);

  static VSmile vs;
  vs.LoadCart(data.data(), data.size());
  vs.SetAccurate(true);
  vs.Reset(false);
  for (int i = 0; i < frames; i++) vs.RunFrame();
  const u16* fb = vs.Framebuffer();

  // Source frame -> RGB888 with a light bloom copy.
  std::vector<Rgb> src(320 * 240), blur(320 * 240);
  for (int i = 0; i < 320 * 240; i++) {
    const u16 v = fb[i];
    src[i] = {((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
  }
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      int r = 0, g = 0, b = 0, n = 0;
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
          int sx = x + dx, sy = y + dy;
          if (sx < 0 || sx >= 320 || sy < 0 || sy >= 240) continue;
          const Rgb& c = src[sy * 320 + sx];
          r += c.r; g += c.g; b += c.b; n++;
        }
      blur[y * 320 + x] = {r / n, g / n, b / n};
    }

  const bool crt = std::string(mode) == "crt";
  const int W = 1280, H = 980;
  std::vector<Rgb> img(W * H);

  // Screen rect (4:3) and bezel.
  const int sw = 960, sh = 720;
  const int sx0 = (W - sw) / 2, sy0 = (H - sh) / 2 - 6;
  const int bez = 46;

  auto sampleSrc = [&](float u, float v, bool bloom) -> Rgb {
    float fx = u * 320 - 0.5f, fy = v * 240 - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    auto at = [&](int x, int y) -> Rgb {
      x = std::min(319, std::max(0, x)); y = std::min(239, std::max(0, y));
      return bloom ? blur[y * 320 + x] : src[y * 320 + x];
    };
    Rgb a = at(x0, y0), b = at(x0 + 1, y0), c = at(x0, y0 + 1), d = at(x0 + 1, y0 + 1);
    auto lerp = [](int p, int q, float t) { return p + (q - p) * t; };
    return {(int)lerp(lerp(a.r, b.r, tx), lerp(c.r, d.r, tx), ty),
            (int)lerp(lerp(a.g, b.g, tx), lerp(c.g, d.g, tx), ty),
            (int)lerp(lerp(a.b, b.b, tx), lerp(c.b, d.b, tx), ty)};
  };

  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      Rgb col{0, 0, 0};

      // Background.
      float ny = (float)y / H;
      if (std::string(bg) == "purple") {
        col = {(int)(128 + (69 - 128) * ny), (int)(102 + (48 - 102) * ny),
               (int)(201 + (125 - 201) * ny)};
        float wave = std::sin(x * 0.018f + y * 0.010f) * 0.5f + 0.5f;
        col.r += (int)(wave * 10); col.g += (int)(wave * 8); col.b += (int)(wave * 14);
      } else if (std::string(bg) == "blue") {
        col = {(int)(205 + (64 - 205) * ny), (int)(235 + (153 - 235) * ny),
               (int)(252 + (230 - 252) * ny)};
      } else {
        col = {8, 8, 10};
      }

      const bool inScreen = x >= sx0 && x < sx0 + sw && y >= sy0 && y < sy0 + sh;
      const bool inBezel = x >= sx0 - bez && x < sx0 + sw + bez && y >= sy0 - bez &&
                           y < sy0 + sh + bez;

      if (inBezel && !inScreen) {
        // Silver bezel: vertical brushed gradient with an inner lip.
        float t = (float)(y - (sy0 - bez)) / (sh + 2 * bez);
        int base = (int)(238 - 120 * t);
        int lip = 0;
        int dl = std::min(std::min(x - (sx0 - bez), sx0 + sw + bez - 1 - x),
                          std::min(y - (sy0 - bez), sy0 + sh + bez - 1 - y));
        int di = std::min(std::min(x - sx0, sx0 + sw - 1 - x), std::min(y - sy0, sy0 + sh - 1 - y));
        if (inScreen == false && di > -14 && di < 0) lip = 30;  // inner highlight
        int s = base + lip - (dl < 6 ? 40 : 0);
        col = {s, (int)(s * 0.99f), (int)(s * 1.02f)};
      }

      if (inScreen) {
        float u = (float)(x - sx0) / sw, v = (float)(y - sy0) / sh;
        if (crt) {
          float px = u * 2 - 1, py = v * 2 - 1;
          float r2 = px * px + py * py;
          float k = 1 + 0.10f * (0.9f * r2 + 0.25f * r2 * r2);
          px *= k; py *= k;
          u = px * 0.5f + 0.5f; v = py * 0.5f + 0.5f;
          if (u < 0 || u > 1 || v < 0 || v > 1) {
            col = {6, 6, 11};  // tube glass beyond the curve
          } else {
            Rgb c = sampleSrc(u, v, false);
            Rgb bl = sampleSrc(u, v, true);
            c.r = std::min(255, c.r + (int)(bl.r * 0.30f));
            c.g = std::min(255, c.g + (int)(bl.g * 0.30f));
            c.b = std::min(255, c.b + (int)(bl.b * 0.30f));
            float scan = 0.72f + 0.28f * std::cos(6.2832f * v * 240);
            float lum = (c.r + c.g + c.b) / 765.0f;
            float s = 1 - (0.30f - 0.18f * lum) * (0.5f + 0.5f * std::cos(6.2832f * v * 240));
            c.r = (int)(c.r * s); c.g = (int)(c.g * s); c.b = (int)(c.b * s);
            int m = x % 3;
            float mr = m == 0 ? 1.10f : 0.94f, mg = m == 1 ? 1.10f : 0.94f, mb = m == 2 ? 1.10f : 0.94f;
            float vig = 1 - 0.26f * r2;
            col = {(int)(c.r * mr * vig * 1.14f), (int)(c.g * mg * vig * 1.14f),
                   (int)(c.b * mb * vig * 1.14f)};
            (void)scan;
          }
        } else {
          col = sampleSrc(u, v, false);
        }
      }

      img[y * W + x] = {std::min(255, std::max(0, col.r)), std::min(255, std::max(0, col.g)),
                        std::min(255, std::max(0, col.b))};
    }
  }

  WriteBmp(out, img, W, H);
  std::printf("wrote %s (%dx%d, mode=%s bg=%s frame=%d)\n", out, W, H, mode, bg, frames);
  return 0;
}
