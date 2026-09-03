#include "switch_render.h"

#include <algorithm>
#include <cstdio>

#include <glad/glad.h>

#include "core/switch_settings.h"

namespace {

// Every shader here is copied verbatim from Android's GameRenderer.kt
// (source of truth: app/src/main/java/com/dsmile/emulator/emu/GameRenderer.kt)
// so the two renderers stay pixel-behavior-identical. GLSL ES 1.00 (no
// #version pragma; attribute/varying/gl_FragColor) - an ES 3.0 context (what
// InitEgl in main.cpp requests) is required to keep compiling that for
// backward compatibility, same as any ES2-era GLES app; already proven by
// the original bring-up pass's "sharp" shader.
const char* kVertexSrc =
    "attribute vec2 aPos;\n"
    "attribute vec2 aTex;\n"
    "varying vec2 vTex;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vTex = aTex;\n"
    "}\n";

const char* kFragPixelSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "void main() { gl_FragColor = texture2D(uTex, vTex); }\n";

const char* kFragSharpSrc =
    "precision highp float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "uniform vec2 uOutSize;\n"
    "void main() {\n"
    "  vec2 texel = vTex * uTexSize;\n"
    "  vec2 scale = max(floor(uOutSize / uTexSize), vec2(1.0));\n"
    "  vec2 texelFloor = floor(texel);\n"
    "  vec2 f = texel - texelFloor;\n"
    "  vec2 region = vec2(0.5) - 0.5 / scale;\n"
    "  f = (clamp(f, region, vec2(1.0) - region) - region) / (1.0 - 2.0 * region);\n"
    "  gl_FragColor = texture2D(uTex, (texelFloor + f) / uTexSize);\n"
    "}\n";

const char* kFragCrtSrc =
    "precision highp float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "uniform vec2 uOutSize;\n"
    "uniform float uCurve;\n"
    "uniform float uGlow;\n"
    "uniform float uScan;\n"
    "uniform float uMask;\n"
    "uniform float uVig;\n"
    "void main() {\n"
    "  vec2 p = vTex * 2.0 - 1.0;\n"
    "  float r2 = dot(p, p);\n"
    "  p *= 1.0 + uCurve * (0.045 * r2 + 0.025 * r2 * r2);\n"
    "  vec2 uv = p * 0.5 + 0.5;\n"
    "  vec2 lim = abs(uv * 2.0 - 1.0);\n"
    "  float edge = 1.0 - smoothstep(0.992, 1.0, max(lim.x, lim.y)) * uCurve;\n"
    "  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
    "    gl_FragColor = vec4(0.0);\n"
    "    return;\n"
    "  }\n"
    "  vec2 texel = uv * uTexSize;\n"
    "  vec2 tf = floor(texel) + 0.5;\n"
    "  vec2 f = clamp((texel - tf) * 1.7, -0.5, 0.5);\n"
    "  vec3 col = texture2D(uTex, (tf + f) / uTexSize).rgb;\n"
    "  vec2 px = 1.0 / uTexSize;\n"
    "  vec3 halo = texture2D(uTex, uv + vec2(px.x, 0.0)).rgb\n"
    "            + texture2D(uTex, uv - vec2(px.x, 0.0)).rgb\n"
    "            + texture2D(uTex, uv + vec2(0.0, px.y)).rgb\n"
    "            + texture2D(uTex, uv - vec2(0.0, px.y)).rgb\n"
    "            + texture2D(uTex, uv + px * vec2(1.0, -1.0)).rgb\n"
    "            + texture2D(uTex, uv - px * vec2(1.0, -1.0)).rgb;\n"
    "  halo /= 6.0;\n"
    "  col = mix(col, max(col, halo), uGlow * 0.5);\n"
    "  col += halo * halo * uGlow * 0.3;\n"
    "  float lum = dot(col, vec3(0.299, 0.587, 0.114));\n"
    "  float scanAmt = uScan * max(0.12, 0.38 - 0.24 * lum);\n"
    "  col *= 1.0 - scanAmt * (0.5 + 0.5 * cos(6.28318 * texel.y));\n"
    "  float m = mod(gl_FragCoord.x, 3.0);\n"
    "  vec3 mask;\n"
    "  if (m < 1.0)      mask = vec3(1.12, 0.92, 0.92);\n"
    "  else if (m < 2.0) mask = vec3(0.92, 1.12, 0.92);\n"
    "  else              mask = vec3(0.92, 0.92, 1.12);\n"
    "  col *= mix(vec3(1.0), mask, uMask);\n"
    "  col *= 1.0 - uVig * 0.32 * r2;\n"
    "  col *= 1.0 + 0.10 * uScan + 0.06 * uMask;\n"
    "  gl_FragColor = vec4(col * edge, edge);\n"
    "}\n";

const char* kFragBgSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform float uMode;\n"
    "void main() {\n"
    "  vec2 uv = vTex;\n"
    "  vec3 col;\n"
    "  float wave = sin((uv.x * 7.0) + uv.y * 3.0) * 0.5 + 0.5;\n"
    "  float band = smoothstep(0.35, 0.95, sin(uv.y * 6.0 - uv.x * 2.5) * 0.5 + 0.5);\n"
    "  if (uMode < 1.5) {\n"
    "    vec3 top = vec3(0.80, 0.92, 0.99);\n"
    "    vec3 bot = vec3(0.25, 0.60, 0.90);\n"
    "    col = mix(top, bot, uv.y);\n"
    "    col += (wave * 0.05 + band * 0.06) * vec3(0.9, 0.97, 1.0) * (1.0 - uv.y * 0.5);\n"
    "  } else {\n"
    "    vec3 top = vec3(0.50, 0.40, 0.79);\n"
    "    vec3 bot = vec3(0.27, 0.19, 0.49);\n"
    "    col = mix(top, bot, uv.y);\n"
    "    col += (wave * 0.03 + band * 0.04) * vec3(0.75, 0.65, 1.0);\n"
    "  }\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

const char* kFragBezelSrc =
    "precision highp float;\n"
    "varying vec2 vTex;\n"
    "uniform float uMat;\n"
    "uniform float uInner;\n"
    "float rrect(vec2 p, vec2 b, float r) {\n"
    "  vec2 d = abs(p) - b + r;\n"
    "  return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;\n"
    "}\n"
    "void main() {\n"
    "  vec2 p = vTex * 2.0 - 1.0;\n"
    "  float dOut = rrect(p, vec2(1.0), 0.14);\n"
    "  float dIn = rrect(p, vec2(uInner), 0.07);\n"
    "  float aOut = 1.0 - smoothstep(-0.012, 0.0, dOut);\n"
    "  if (aOut <= 0.0) { gl_FragColor = vec4(0.0); return; }\n"
    "  vec3 col;\n"
    "  if (dIn < 0.0) {\n"
    "    float sheen = smoothstep(0.4, 1.0, -p.y) * 0.05;\n"
    "    col = vec3(0.03 + sheen);\n"
    "  } else {\n"
    "    float v = vTex.y;\n"
    "    float lip = smoothstep(0.035, 0.0, dIn);\n"
    "    float rim = smoothstep(-0.05, 0.0, dOut);\n"
    "    if (uMat < 1.5) {\n"
    "      float base = 0.82 - 0.28 * v;\n"
    "      base += 0.10 * smoothstep(0.35, 0.0, abs(v - 0.18));\n"
    "      col = vec3(base) * vec3(0.97, 0.98, 1.0);\n"
    "      col += lip * 0.14;\n"
    "      col *= 1.0 - rim * 0.35;\n"
    "    } else {\n"
    "      float base = 0.17 - 0.06 * v;\n"
    "      col = vec3(base);\n"
    "      col += lip * 0.10;\n"
    "      col *= 1.0 - rim * 0.45;\n"
    "    }\n"
    "  }\n"
    "  gl_FragColor = vec4(col * aOut, aOut);\n"
    "}\n";

constexpr int kFbW = 320, kFbH = 240;

struct Vertex {
  float x, y, u, v;
};

struct Program {
  GLuint id = 0;
  GLint uTex = -1, uTexSize = -1, uOutSize = -1;
  GLint uCurve = -1, uGlow = -1, uScan = -1, uMask = -1, uVig = -1;  // CRT only
};

Program g_pixel, g_sharp, g_crt;
GLuint g_bg_program = 0, g_bezel_program = 0;
GLint g_bg_uMode = -1;
GLint g_bezel_uMat = -1, g_bezel_uInner = -1;

GLuint g_vao = 0, g_vbo = 0;
GLuint g_tex = 0;
int g_viewport_w = 1280, g_viewport_h = 720;

GLuint CompileShader(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    GLsizei len = 0;
    glGetShaderInfoLog(shader, sizeof(log), &len, log);
    printf("dsmile: shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(const char* vs_src, const char* fs_src) {
  GLuint vs = CompileShader(GL_VERTEX_SHADER, vs_src);
  GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fs_src);
  if (!vs || !fs) return 0;
  GLuint program = glCreateProgram();
  glBindAttribLocation(program, 0, "aPos");
  glBindAttribLocation(program, 1, "aTex");
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[1024];
    GLsizei len = 0;
    glGetProgramInfoLog(program, sizeof(log), &len, log);
    printf("dsmile: program link failed: %s\n", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

Program BuildDisplayProgram(const char* frag_src) {
  Program p;
  p.id = LinkProgram(kVertexSrc, frag_src);
  if (!p.id) return p;
  p.uTex = glGetUniformLocation(p.id, "uTex");
  p.uTexSize = glGetUniformLocation(p.id, "uTexSize");
  p.uOutSize = glGetUniformLocation(p.id, "uOutSize");
  p.uCurve = glGetUniformLocation(p.id, "uCurve");
  p.uGlow = glGetUniformLocation(p.id, "uGlow");
  p.uScan = glGetUniformLocation(p.id, "uScan");
  p.uMask = glGetUniformLocation(p.id, "uMask");
  p.uVig = glGetUniformLocation(p.id, "uVig");
  return p;
}

void UploadQuad(const Vertex verts[4]) {
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(Vertex) * 4, verts);
}

}  // namespace

bool switch_render_init(int viewport_w, int viewport_h) {
  g_viewport_w = viewport_w;
  g_viewport_h = viewport_h;

  g_pixel = BuildDisplayProgram(kFragPixelSrc);
  g_sharp = BuildDisplayProgram(kFragSharpSrc);
  g_crt = BuildDisplayProgram(kFragCrtSrc);
  if (!g_pixel.id || !g_sharp.id || !g_crt.id) return false;

  g_bg_program = LinkProgram(kVertexSrc, kFragBgSrc);
  g_bezel_program = LinkProgram(kVertexSrc, kFragBezelSrc);
  if (!g_bg_program || !g_bezel_program) return false;
  g_bg_uMode = glGetUniformLocation(g_bg_program, "uMode");
  g_bezel_uMat = glGetUniformLocation(g_bezel_program, "uMat");
  g_bezel_uInner = glGetUniformLocation(g_bezel_program, "uInner");

  glGenVertexArrays(1, &g_vao);
  glGenBuffers(1, &g_vbo);
  glBindVertexArray(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 4, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(sizeof(float) * 2));
  glBindVertexArray(0);

  glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kFbW, kFbH, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  return true;
}

void switch_render_frame(const uint16_t* framebuffer565) {
  glViewport(0, 0, g_viewport_w, g_viewport_h);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (!g_pixel.id) return;
  glBindVertexArray(g_vao);

  const std::string& bg = g_settings.background_mode;
  if (bg != "black") {
    glUseProgram(g_bg_program);
    static const Vertex kFullscreen[4] = {
        {-1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, -1.0f, 1.0f, 1.0f},
        {-1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f},
    };
    UploadQuad(kFullscreen);
    if (g_bg_uMode >= 0) glUniform1f(g_bg_uMode, bg == "blue" ? 1.0f : 2.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }

  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kFbW, kFbH, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
                   framebuffer565);

  const std::string& shader = g_settings.shader_mode;
  const GLenum filter = (shader == "pixel") ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

  // Aspect-corrected quad (Android's AspectMode).
  float qw = 1.0f, qh = 1.0f;
  const std::string& aspect = g_settings.aspect_mode;
  if (aspect == "stretch") {
    // qw = qh = 1
  } else if (aspect == "integer") {
    const int scale = std::max(1, std::min(g_viewport_w / kFbW, g_viewport_h / kFbH));
    qw = (float)(kFbW * scale) / (float)g_viewport_w;
    qh = (float)(kFbH * scale) / (float)g_viewport_h;
  } else {  // "four_three"
    const float target = 4.0f / 3.0f;
    const float view = (float)g_viewport_w / (float)g_viewport_h;
    if (view > target) qw = target / view;
    else qh = view / target;
  }

  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  const std::string& bezel = g_settings.bezel_mode;
  constexpr float kInner = 0.86f;
  if (bezel != "none") {
    const Vertex verts[4] = {
        {-qw, -qh, 0.0f, 1.0f}, {qw, -qh, 1.0f, 1.0f}, {-qw, qh, 0.0f, 0.0f}, {qw, qh, 1.0f, 0.0f},
    };
    UploadQuad(verts);
    glUseProgram(g_bezel_program);
    if (g_bezel_uMat >= 0) glUniform1f(g_bezel_uMat, bezel == "silver" ? 1.0f : 2.0f);
    if (g_bezel_uInner >= 0) glUniform1f(g_bezel_uInner, kInner);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    qw *= kInner;
    qh *= kInner;
  }

  const Vertex verts[4] = {
      {-qw, -qh, 0.0f, 1.0f}, {qw, -qh, 1.0f, 1.0f}, {-qw, qh, 0.0f, 0.0f}, {qw, qh, 1.0f, 0.0f},
  };
  UploadQuad(verts);

  const Program& prog = (shader == "pixel") ? g_pixel : (shader == "crt") ? g_crt : g_sharp;
  glUseProgram(prog.id);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  if (prog.uTex >= 0) glUniform1i(prog.uTex, 0);
  if (prog.uTexSize >= 0) glUniform2f(prog.uTexSize, (float)kFbW, (float)kFbH);
  if (prog.uOutSize >= 0) glUniform2f(prog.uOutSize, (float)g_viewport_w * qw, (float)g_viewport_h * qh);
  if (shader == "crt") {
    if (prog.uCurve >= 0) glUniform1f(prog.uCurve, g_settings.crt_curve);
    if (prog.uGlow >= 0) glUniform1f(prog.uGlow, g_settings.crt_glow);
    if (prog.uScan >= 0) glUniform1f(prog.uScan, g_settings.crt_scan);
    if (prog.uMask >= 0) glUniform1f(prog.uMask, g_settings.crt_mask);
    if (prog.uVig >= 0) glUniform1f(prog.uVig, g_settings.crt_vignette);
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glDisable(GL_BLEND);
  glBindVertexArray(0);
}

void switch_render_shutdown() {
  if (g_tex) glDeleteTextures(1, &g_tex);
  if (g_vbo) glDeleteBuffers(1, &g_vbo);
  if (g_vao) glDeleteVertexArrays(1, &g_vao);
  if (g_pixel.id) glDeleteProgram(g_pixel.id);
  if (g_sharp.id) glDeleteProgram(g_sharp.id);
  if (g_crt.id) glDeleteProgram(g_crt.id);
  if (g_bg_program) glDeleteProgram(g_bg_program);
  if (g_bezel_program) glDeleteProgram(g_bezel_program);
  g_tex = g_vbo = g_vao = 0;
  g_pixel = Program{};
  g_sharp = Program{};
  g_crt = Program{};
  g_bg_program = g_bezel_program = 0;
}
