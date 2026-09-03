#include "switch_ui.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glad/glad.h>

namespace {

const char* kVertexSrc =
    "attribute vec2 aPos;\n"
    "attribute vec2 aTex;\n"
    "varying vec2 vTex;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vTex = aTex;\n"
    "}\n";

const char* kFragSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec4 uMul;\n"
    "uniform float uAlphaOnly;\n"
    "void main() {\n"
    "  vec4 t = texture2D(uTex, vTex);\n"
    "  if (uAlphaOnly > 0.5) {\n"
    "    gl_FragColor = vec4(uMul.rgb, t.a * uMul.a);\n"
    "  } else {\n"
    "    gl_FragColor = t * uMul;\n"
    "  }\n"
    "}\n";

struct Vertex {
  float x, y, u, v;
};

GLuint g_program = 0;
GLuint g_vao = 0, g_vbo = 0;
GLuint g_tex_white = 0;
GLint g_uTex = -1, g_uMul = -1, g_uAlphaOnly = -1;
size_t g_vbo_capacity = 0;
int g_viewport_w = 1280, g_viewport_h = 720;

// switch_ui_draw_text_marquee's scroll phase. Restarts whenever the
// scrolling text changes OR there was a gap (a frame where nothing
// marqueed at all - e.g. the selection passed through a row/tile short
// enough to fit) - the gap check matters for the case text-equality alone
// would miss: leaving a long name for a short one and back again would
// otherwise resume mid-cycle instead of restarting, since the stored text
// never actually changed while it was deselected.
std::string g_marquee_text;
double g_marquee_start_ms = 0.0;
bool g_marquee_active_this_frame = false;
bool g_marquee_active_prev_frame = false;

FT_Library g_ft_library = nullptr;
FT_Face g_ft_face = nullptr;

struct GlyphEntry {
  GLuint tex = 0;
  int width = 0, height = 0;
  int bearing_x = 0, bearing_y = 0;
  int advance = 0;
};
// Keyed by (pixel_size << 21 | codepoint).
std::unordered_map<uint64_t, GlyphEntry> g_glyph_cache;

GLuint CompileShader(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    GLsizei len = 0;
    glGetShaderInfoLog(shader, sizeof(log), &len, log);
    printf("dsmile ui: shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

void AppendQuadPx(std::vector<Vertex>& out, float x, float y, float w, float h, float u0, float v0,
                   float u1, float v1) {
  const float cw = g_viewport_w > 0 ? (float)g_viewport_w : 1.0f;
  const float ch = g_viewport_h > 0 ? (float)g_viewport_h : 1.0f;
  const float left = (x / cw) * 2.0f - 1.0f;
  const float right = ((x + w) / cw) * 2.0f - 1.0f;
  const float top = 1.0f - (y / ch) * 2.0f;
  const float bottom = 1.0f - ((y + h) / ch) * 2.0f;

  out.push_back({left, bottom, u0, v0});
  out.push_back({right, bottom, u1, v0});
  out.push_back({right, top, u1, v1});
  out.push_back({right, top, u1, v1});
  out.push_back({left, top, u0, v1});
  out.push_back({left, bottom, u0, v0});
}

void DrawQuad(GLuint tex, const std::vector<Vertex>& verts) {
  if (tex == 0 || verts.empty()) return;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (g_uTex >= 0) glUniform1i(g_uTex, 0);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  const size_t bytes = verts.size() * sizeof(Vertex);
  if (bytes > g_vbo_capacity) {
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, verts.data(), GL_DYNAMIC_DRAW);
    g_vbo_capacity = bytes;
  } else {
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, verts.data());
  }
  glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
}

// Rasterizes (and caches) one glyph at one exact pixel size. Returns a valid
// (possibly tex=0, e.g. space) entry, or nullptr if the face is unavailable.
const GlyphEntry* GetOrRasterizeGlyph(uint32_t codepoint, int pixel_size) {
  if (!g_ft_face) return nullptr;

  const uint64_t key = ((uint64_t)(uint32_t)pixel_size << 21) | (codepoint & 0x1FFFFFu);
  auto it = g_glyph_cache.find(key);
  if (it != g_glyph_cache.end()) return &it->second;

  FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)pixel_size);
  if (FT_Load_Char(g_ft_face, codepoint, FT_LOAD_RENDER)) return nullptr;

  const FT_GlyphSlot slot = g_ft_face->glyph;
  const FT_Bitmap& bmp = slot->bitmap;

  GlyphEntry entry;
  entry.width = (int)bmp.width;
  entry.height = (int)bmp.rows;
  entry.bearing_x = slot->bitmap_left;
  entry.bearing_y = slot->bitmap_top;
  entry.advance = (int)(slot->advance.x >> 6);

  if (entry.width > 0 && entry.height > 0 && bmp.pixel_mode == FT_PIXEL_MODE_GRAY) {
    std::vector<uint8_t> rgba((size_t)entry.width * entry.height * 4);
    for (int y = 0; y < entry.height; y++) {
      const uint8_t* src_row = bmp.buffer + (size_t)y * bmp.pitch;
      for (int x = 0; x < entry.width; x++) {
        uint8_t* dst = &rgba[((size_t)y * entry.width + x) * 4];
        dst[0] = dst[1] = dst[2] = 0;
        dst[3] = src_row[x];
      }
    }
    glGenTextures(1, &entry.tex);
    glBindTexture(GL_TEXTURE_2D, entry.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, entry.width, entry.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
  }

  auto res = g_glyph_cache.emplace(key, entry);
  return &res.first->second;
}

}  // namespace

bool switch_ui_init() {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSrc);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
  if (!vs || !fs) return false;

  g_program = glCreateProgram();
  glBindAttribLocation(g_program, 0, "aPos");
  glBindAttribLocation(g_program, 1, "aTex");
  glAttachShader(g_program, vs);
  glAttachShader(g_program, fs);
  glLinkProgram(g_program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint linked = 0;
  glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[512];
    GLsizei len = 0;
    glGetProgramInfoLog(g_program, sizeof(log), &len, log);
    printf("dsmile ui: program link failed: %s\n", log);
    return false;
  }

  g_uTex = glGetUniformLocation(g_program, "uTex");
  g_uMul = glGetUniformLocation(g_program, "uMul");
  g_uAlphaOnly = glGetUniformLocation(g_program, "uAlphaOnly");

  glGenVertexArrays(1, &g_vao);
  glGenBuffers(1, &g_vbo);
  glBindVertexArray(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(float) * 2));
  glBindVertexArray(0);

  glGenTextures(1, &g_tex_white);
  glBindTexture(GL_TEXTURE_2D, g_tex_white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  const uint8_t white_px[4] = {255, 255, 255, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_px);

  Result rc = plInitialize(PlServiceType_User);
  if (R_FAILED(rc)) {
    printf("dsmile ui: plInitialize failed: 0x%x\n", (unsigned)rc);
    return true;  // rects still work without text
  }

  PlFontData font;
  rc = plGetSharedFontByType(&font, PlSharedFontType_Standard);
  if (R_FAILED(rc)) {
    printf("dsmile ui: plGetSharedFontByType failed: 0x%x\n", (unsigned)rc);
    return true;
  }

  if (FT_Init_FreeType(&g_ft_library)) {
    printf("dsmile ui: FT_Init_FreeType failed\n");
    return true;
  }
  if (FT_New_Memory_Face(g_ft_library, (const FT_Byte*)font.address, (FT_Long)font.size, 0,
                          &g_ft_face)) {
    printf("dsmile ui: FT_New_Memory_Face failed\n");
    FT_Done_FreeType(g_ft_library);
    g_ft_library = nullptr;
  }

  return true;
}

void switch_ui_shutdown() {
  for (auto& kv : g_glyph_cache) {
    if (kv.second.tex) {
      GLuint t = kv.second.tex;
      glDeleteTextures(1, &t);
    }
  }
  g_glyph_cache.clear();

  if (g_ft_face) {
    FT_Done_Face(g_ft_face);
    g_ft_face = nullptr;
  }
  if (g_ft_library) {
    FT_Done_FreeType(g_ft_library);
    g_ft_library = nullptr;
  }
  plExit();

  if (g_tex_white) glDeleteTextures(1, &g_tex_white);
  if (g_vbo) glDeleteBuffers(1, &g_vbo);
  if (g_vao) glDeleteVertexArrays(1, &g_vao);
  if (g_program) glDeleteProgram(g_program);
  g_program = g_vao = g_vbo = g_tex_white = 0;
  g_vbo_capacity = 0;
}

void switch_ui_begin_frame(int viewport_w, int viewport_h) {
  g_viewport_w = viewport_w;
  g_viewport_h = viewport_h;
  g_marquee_active_prev_frame = g_marquee_active_this_frame;
  g_marquee_active_this_frame = false;
  if (!g_program) return;

  glUseProgram(g_program);
  glBindVertexArray(g_vao);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  if (g_uAlphaOnly >= 0) glUniform1f(g_uAlphaOnly, 0.0f);
}

void switch_ui_draw_rect(float x, float y, float w, float h, float r, float g, float b, float a) {
  if (!g_program || !g_tex_white) return;
  if (g_uAlphaOnly >= 0) glUniform1f(g_uAlphaOnly, 0.0f);
  if (g_uMul >= 0) glUniform4f(g_uMul, r, g, b, a);
  std::vector<Vertex> verts;
  AppendQuadPx(verts, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
  DrawQuad(g_tex_white, verts);
}

void switch_ui_draw_texture(uint32_t gl_tex, float x, float y, float w, float h) {
  if (!g_program || gl_tex == 0) return;
  if (g_uAlphaOnly >= 0) glUniform1f(g_uAlphaOnly, 0.0f);
  if (g_uMul >= 0) glUniform4f(g_uMul, 1.0f, 1.0f, 1.0f, 1.0f);
  std::vector<Vertex> verts;
  // Same V-flip as glyphs (see switch_ui_draw_text): stb_image decodes
  // top-row-first too, so it needs the same v0=1,v1=0 swap to avoid landing
  // upside down.
  AppendQuadPx(verts, x, y, w, h, 0.0f, 1.0f, 1.0f, 0.0f);
  DrawQuad((GLuint)gl_tex, verts);
}

void switch_ui_draw_text(float x, float y, float pixel_size, float r, float g, float b, float a,
                          const std::string& text) {
  if (!g_program || !g_ft_face || pixel_size <= 0.0f) return;

  const int px = (int)(pixel_size + 0.5f);
  FT_Set_Pixel_Sizes(g_ft_face, 0, (FT_UInt)px);
  const float baseline_y = y + (float)(g_ft_face->size->metrics.ascender >> 6);

  if (g_uAlphaOnly >= 0) glUniform1f(g_uAlphaOnly, 1.0f);
  if (g_uMul >= 0) glUniform4f(g_uMul, r, g, b, a);

  float cursor_x = x;
  for (unsigned char c : text) {
    const GlyphEntry* ge = GetOrRasterizeGlyph((uint32_t)c, px);
    if (!ge) {
      cursor_x += px * 0.5f;
      continue;
    }
    if (ge->tex != 0) {
      std::vector<Vertex> verts;
      const float gx = cursor_x + (float)ge->bearing_x;
      const float gy = baseline_y - (float)ge->bearing_y;
      // FreeType rasterizes top-row-first; uploaded as-is, that row lands at
      // texture v=0, which GL's convention then samples at the *bottom* of
      // an unflipped quad - the exact upside-down glyphs seen on hardware.
      // v0=1,v1=0 (swapped from the naive 0,1) undoes that.
      AppendQuadPx(verts, gx, gy, (float)ge->width, (float)ge->height, 0.0f, 1.0f, 1.0f, 0.0f);
      DrawQuad(ge->tex, verts);
    }
    cursor_x += (float)ge->advance;
  }

  if (g_uAlphaOnly >= 0) glUniform1f(g_uAlphaOnly, 0.0f);
}

float switch_ui_text_width(const std::string& text, float pixel_size) {
  if (!g_ft_face || pixel_size <= 0.0f) return 0.0f;
  const int px = (int)(pixel_size + 0.5f);
  float width = 0.0f;
  for (unsigned char c : text) {
    const GlyphEntry* ge = GetOrRasterizeGlyph((uint32_t)c, px);
    width += ge ? (float)ge->advance : (float)px * 0.5f;
  }
  return width;
}

std::string switch_ui_ellipsize(const std::string& text, float pixel_size, float max_width) {
  if (switch_ui_text_width(text, pixel_size) <= max_width) return text;
  const float ellipsis_w = switch_ui_text_width("...", pixel_size);
  // Trim from the end until "<prefix>..." fits. Byte-at-a-time (not UTF-8
  // boundary aware, unlike DraStic's ellipsizedText()) - fine for the ASCII
  // ROM/folder names this sees; see switch/README.md if that stops holding.
  std::string s = text;
  while (!s.empty() && switch_ui_text_width(s, pixel_size) + ellipsis_w > max_width) {
    s.pop_back();
  }
  return s + "...";
}

void switch_ui_draw_text_marquee(float x, float y, float w, float h, float pixel_size, float r,
                                  float g, float b, float a, const std::string& text,
                                  int viewport_h) {
  const float tw = switch_ui_text_width(text, pixel_size);
  if (tw <= w) {
    switch_ui_draw_text(x, y, pixel_size, r, g, b, a, text);
    return;
  }

  const double now_ms = (double)armTicksToNs(armGetSystemTick()) / 1'000'000.0;
  const bool continued = g_marquee_active_prev_frame && text == g_marquee_text;
  if (!continued) {
    // Either different text, or a gap since the last frame that marqueed
    // anything at all (the selection passed through something short enough
    // to fit in between) - either way, restart from the beginning instead
    // of picking up wherever the shared 6-second clock happens to be, which
    // read as the animation "not resetting" when flipping between games.
    g_marquee_text = text;
    g_marquee_start_ms = now_ms;
  }
  g_marquee_active_this_frame = true;

  glEnable(GL_SCISSOR_TEST);
  glScissor((GLint)x, (GLint)((float)viewport_h - (y + h)), (GLsizei)w, (GLsizei)h);

  // 6-second ping-pong (0 -> 1 -> 0) from this text's own start time, same
  // period and shape as DraStic's drawScrollTextL/R - slides left to reveal
  // the tail, then back.
  const float t = (float)std::fmod(now_ms - g_marquee_start_ms, 6000.0) / 6000.0f;
  const float pp = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f;
  const float span = tw - w;
  switch_ui_draw_text(x - pp * span, y, pixel_size, r, g, b, a, text);

  glDisable(GL_SCISSOR_TEST);
}
