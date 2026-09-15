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

// "DraStic" shaders below: ported from reference/DrasticDS_nx-main's
// third_party/drastic-ds-shaders bundle (jdgleaver's DraStic-format ports of
// classic RetroArch/libretro shaders - GPL-2-or-later, see switch/README.md
// for full per-shader authorship and the acknowledgement in the main
// README). Their `.dsd` source turned out to be plain GLSL ES 1.00 in a
// simple text wrapper - the same dialect the shaders above already use -
// so porting is a mechanical rename (their `a_vertex_coordinate`/
// `a_texture_coordinate`/`u_texture`/`u_texture_size` to this file's own
// `aPos`/`aTex`/`uTex`/`uTexSize`) rather than a new shader pipeline.
// `uTexSize` here is (width, height) in texels, matching this file's own
// convention - anywhere the original used `u_texture_size.zw` (the same
// pair, packed into a vec4 alongside its own reciprocal in .xy, which none
// of these actually read) it's just `uTexSize` here.
//
// Two of these (5xBR, SABR) normalize their edge-detection math to whatever
// "NDS_SCREEN_HEIGHT"-style native resolution the original had baked in for
// DraStic's fixed DS framebuffer; V.Smile's is a different fixed size
// (kFbW x kFbH below), so those constants are this file's own values, not
// copied from the originals - everything else is unchanged.
const char* kFragDrasticLcd1xSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "const float kPi = 3.141592654;\n"
    "const float kBrightenScanlines = 16.0;\n"
    "const float kBrightenLcd = 4.0;\n"
    "void main() {\n"
    "  vec2 angle = 2.0 * kPi * (vTex * uTexSize - 0.25);\n"
    "  float yf = (kBrightenScanlines + sin(angle.y)) / (kBrightenScanlines + 1.0);\n"
    "  float xf = (kBrightenLcd + sin(angle.x)) / (kBrightenLcd + 1.0);\n"
    "  vec3 col = texture2D(uTex, vTex).rgb * (yf * xf);\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// Vertex+fragment pair (unlike every shader above, which reuses kVertexSrc)
// since the perf trick here - precomputing the texel/scale varyings once
// per vertex instead of per fragment - needs its own vertex stage.
const char* kVertexDrasticSharpBilinearSrc =
    "attribute vec2 aPos;\n"
    "attribute vec2 aTex;\n"
    "uniform vec2 uTexSize;\n"
    "uniform vec2 uOutSize;\n"
    "varying vec2 vTexel;\n"
    "varying vec2 vScale;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vTexel = aTex * uTexSize;\n"
    "  vScale = floor(uOutSize / uTexSize) + vec2(1.0);\n"
    "}\n";
const char* kFragDrasticSharpBilinearSrc =
    "precision highp float;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "varying vec2 vTexel;\n"
    "varying vec2 vScale;\n"
    "void main() {\n"
    "  vec2 texelFloor = floor(vTexel);\n"
    "  vec2 s = fract(vTexel);\n"
    "  vec2 regionRange = vec2(0.5) - 0.5 / vScale;\n"
    "  vec2 centerDist = s - 0.5;\n"
    "  vec2 f = (centerDist - clamp(centerDist, -regionRange, regionRange)) * vScale + 0.5;\n"
    "  vec2 modTexel = texelFloor + f;\n"
    "  vec3 col = texture2D(uTex, modTexel / uTexSize).rgb;\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

const char* kFragDrasticZfastLcdSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "const float kBorderMult = 14.0;\n"
    "void main() {\n"
    "  vec2 texel = vTex * uTexSize;\n"
    "  vec2 center = floor(texel) + vec2(0.5);\n"
    "  vec2 dist = abs(center - texel);\n"
    "  float y = max(dist.x, dist.y);\n"
    "  y *= y;\n"
    "  float yy = y * y;\n"
    "  float weight = 1.0 - kBorderMult * (yy - 2.7 * yy * y);\n"
    "  vec3 col = texture2D(uTex, vTex).rgb * weight;\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

const char* kFragDrasticNaturalVisionSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "const float kGammaIn = 1.91;\n"
    "const float kGammaOut = 1.91;\n"
    "const float kY = 1.1;\n"
    "const float kI = 1.1;\n"
    "const float kQ = 1.1;\n"
    "const mat3 kRgbToYiq = mat3(0.299, 0.587, 0.114,\n"
    "                            0.595716, -0.274453, -0.321263,\n"
    "                            0.211456, -0.522591, 0.311135);\n"
    "const mat3 kYiqToRgb = mat3(1.0, 1.0, 1.0,\n"
    "                            0.95629572, -0.27212210, -1.10698902,\n"
    "                            0.62102442, -0.64738060, 1.70461500);\n"
    "const vec3 kYiqLo = vec3(0.0, -0.595716, -0.522591);\n"
    "const vec3 kYiqHi = vec3(1.0, 0.595716, 0.522591);\n"
    "void main() {\n"
    "  vec3 col = pow(texture2D(uTex, vTex).rgb, vec3(kGammaIn));\n"
    "  col = kRgbToYiq * col;\n"
    "  col = vec3(pow(col.x, kY), col.y * kI, col.z * kQ);\n"
    "  col = clamp(col, kYiqLo, kYiqHi);\n"
    "  col = kYiqToRgb * col;\n"
    "  col = pow(col, vec3(1.0 / kGammaOut));\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

// Hyllian's 5xBR v4.0 (Level 3, No Blending). Own vertex stage precomputes
// the 8 neighbor-texel varyings the fragment stage samples - straight from
// the original, just aPos/aTex/uTex/uTexSize renamed as described above.
const char* kVertexDrastic5xbrSrc = R"GLSL(
attribute vec2 aPos;
attribute vec2 aTex;
uniform vec2 uTexSize;
varying vec4 v0;
varying vec4 v1;
varying vec4 v2;
varying vec4 v3;
varying vec4 v4;
varying vec4 v5;
varying vec4 v6;
varying vec4 v7;
void main() {
  float dx = 1.0 / uTexSize.x;
  float dy = 1.0 / uTexSize.y;

  //     A1 B1 C1
  //  A0  A  B  C C4
  //  D0  D  E  F F4
  //  G0  G  H  I I4
  //     G5 H5 I5

  gl_Position = vec4(aPos, 0.0, 1.0);
  v0 = aTex.xyxy;
  v1 = aTex.xxxy + vec4(-dx, 0.0,  dx, -2.0*dy);  //  A1 B1 C1
  v2 = aTex.xxxy + vec4(-dx, 0.0,  dx,     -dy);  //   A  B  C
  v3 = aTex.xxxy + vec4(-dx, 0.0,  dx,     0.0);  //   D  E  F
  v4 = aTex.xxxy + vec4(-dx, 0.0,  dx,      dy);  //   G  H  I
  v5 = aTex.xxxy + vec4(-dx, 0.0,  dx,  2.0*dy);  //  G5 H5 I5
  v6 = aTex.xyyy + vec4(-2.0*dx,  -dy,  0.0, dy);  //  A0 D0 G0
  v7 = aTex.xyyy + vec4( 2.0*dx,  -dy,  0.0, dy);  //  C4 F4 I4
}
)GLSL";
const char* kFragDrastic5xbrSrc = R"GLSL(
precision highp float;
uniform sampler2D uTex;
uniform vec2 uTexSize;
varying vec4 v0;
varying vec4 v1;
varying vec4 v2;
varying vec4 v3;
varying vec4 v4;
varying vec4 v5;
varying vec4 v6;
varying vec4 v7;

const float coef = 2.0;
const float y_weight = 48.0;
const float u_weight = 7.0;
const float v_weight = 6.0;

const mat3 yuv = mat3(0.299, 0.587, 0.114, -0.169, -0.331, 0.499, 0.499, -0.418, -0.0813);
const mat3 yuv_weighted = mat3(y_weight * yuv[0], u_weight * yuv[1], v_weight * yuv[2]);

vec4 RGBtoYUV(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {
  float a = yuv_weighted[0].x * v0.x + yuv_weighted[0].y * v0.y + yuv_weighted[0].z * v0.z;
  float b = yuv_weighted[0].x * v1.x + yuv_weighted[0].y * v1.y + yuv_weighted[0].z * v1.z;
  float c = yuv_weighted[0].x * v2.x + yuv_weighted[0].y * v2.y + yuv_weighted[0].z * v2.z;
  float d = yuv_weighted[0].x * v3.x + yuv_weighted[0].y * v3.y + yuv_weighted[0].z * v3.z;
  return vec4(a, b, c, d);
}
bvec4 _and_(bvec4 A, bvec4 B) { return bvec4(A.x && B.x, A.y && B.y, A.z && B.z, A.w && B.w); }
bvec4 _or_(bvec4 A, bvec4 B) { return bvec4(A.x || B.x, A.y || B.y, A.z || B.z, A.w || B.w); }
vec4 df(vec4 A, vec4 B) { return vec4(abs(A - B)); }
bvec4 close(vec4 A, vec4 B) { return (lessThan(df(A, B), vec4(10.0, 10.0, 10.0, 10.0))); }
bvec4 eq2(vec4 A, vec4 B) { return (lessThan(df(A, B), vec4(2.0, 2.0, 2.0, 2.0))); }
float c_df(vec3 c1, vec3 c2) { vec3 df = abs(c1 - c2); return df.r + df.g + df.b; }
vec4 weighted_distance(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
  return (df(a, b) + df(a, c) + df(d, e) + df(d, f) + 4.0 * df(g, h));
}

void main() {
  vec2 fp = fract(v0.xy * uTexSize);

  vec3 A1 = texture2D(uTex, v1.xw).rgb;
  vec3 B1 = texture2D(uTex, v1.yw).rgb;
  vec3 C1 = texture2D(uTex, v1.zw).rgb;

  vec3 A  = texture2D(uTex, v2.xw).rgb;
  vec3 B  = texture2D(uTex, v2.yw).rgb;
  vec3 C  = texture2D(uTex, v2.zw).rgb;

  vec3 D  = texture2D(uTex, v3.xw).rgb;
  vec3 E  = texture2D(uTex, v3.yw).rgb;
  vec3 F  = texture2D(uTex, v3.zw).rgb;

  vec3 G  = texture2D(uTex, v4.xw).rgb;
  vec3 H  = texture2D(uTex, v4.yw).rgb;
  vec3 I  = texture2D(uTex, v4.zw).rgb;

  vec3 G5 = texture2D(uTex, v5.xw).rgb;
  vec3 H5 = texture2D(uTex, v5.yw).rgb;
  vec3 I5 = texture2D(uTex, v5.zw).rgb;

  vec3 A0 = texture2D(uTex, v6.xy).rgb;
  vec3 D0 = texture2D(uTex, v6.xz).rgb;
  vec3 G0 = texture2D(uTex, v6.xw).rgb;

  vec3 C4 = texture2D(uTex, v7.xy).rgb;
  vec3 F4 = texture2D(uTex, v7.xz).rgb;
  vec3 I4 = texture2D(uTex, v7.xw).rgb;

  vec4 b = RGBtoYUV(B, D, H, F);
  vec4 c = RGBtoYUV(C, A, G, I);
  vec4 e = RGBtoYUV(E, E, E, E);
  vec4 d = b.yzwx;
  vec4 f = b.wxyz;
  vec4 g = c.zwxy;
  vec4 h = b.zwxy;
  vec4 i = c.wxyz;

  vec4 i4 = RGBtoYUV(I4, C1, A0, G5);
  vec4 i5 = RGBtoYUV(I5, C4, A1, G0);
  vec4 h5 = RGBtoYUV(H5, F4, B1, D0);
  vec4 f4 = h5.yzwx;

  vec4 c1 = i4.yzwx;
  vec4 g0 = i5.wxyz;
  vec4 b1 = h5.zwxy;
  vec4 d0 = h5.wxyz;

  vec4 Ao = vec4( 1.0, -1.0, -1.0,  1.0 );
  vec4 Bo = vec4( 1.0,  1.0, -1.0, -1.0 );
  vec4 Co = vec4( 1.5,  0.5, -0.5,  0.5 );
  vec4 Ax = vec4( 1.0, -1.0, -1.0,  1.0 );
  vec4 Bx = vec4( 0.5,  2.0, -0.5, -2.0 );
  vec4 Cx = vec4( 1.0,  1.0, -0.5,  0.0 );
  vec4 Ay = vec4( 1.0, -1.0, -1.0,  1.0 );
  vec4 By = vec4( 2.0,  0.5, -2.0, -0.5 );
  vec4 Cy = vec4( 2.0,  0.0, -1.0,  0.5 );

  vec4 Az = vec4( 6.0, -2.0, -6.0, 2.0 );
  vec4 Bz = vec4( 2.0, 6.0, -2.0, -6.0 );
  vec4 Cz = vec4( 5.0, 3.0, -3.0, -1.0 );
  vec4 Aw = vec4( 2.0, -6.0, -2.0, 6.0 );
  vec4 Bw = vec4( 6.0, 2.0, -6.0,-2.0 );
  vec4 Cw = vec4( 5.0, -1.0, -3.0, 3.0 );

  bvec4 fx       = greaterThan(Ao * fp.y + Bo * fp.x, Co);
  bvec4 fx_left  = greaterThan(Ax * fp.y + Bx * fp.x, Cx);
  bvec4 fx_up    = greaterThan(Ay * fp.y + By * fp.x, Cy);
  bvec4 fx3_left = greaterThan(Az * fp.y + Bz * fp.x, Cz);
  bvec4 fx3_up   = greaterThan(Aw * fp.y + Bw * fp.x, Cw);

  bvec4 t1 = _and_( notEqual(e, f), notEqual(e, h) );
  bvec4 t2 = _and_( not(close(f, b)), not(close(h, d)) );
  bvec4 t3 = _and_( not(close(h, i4)), not(close(h, i5)) );
  bvec4 t4 = _and_( notEqual(f, f4), notEqual(f, i) );
  bvec4 t5 = _and_( notEqual(h, h5), notEqual(h, i) );
  bvec4 t6 = _and_( close(e, i), _or_(t4, t5) );
  bvec4 t7 = _or_( close(e, g), close(e, c) );
  bvec4 t8 = _and_( close(b, c1), close(d, g0));
  bvec4 t9 = _or_( notEqual(h, g), notEqual(f, c));
  bvec4 interp_restriction_lv1 = _and_( t1, _or_(_or_(_or_( _or_( _or_(t2, t3), t6 ), t7 ), t8), t9 ));

  bvec4 interp_restriction_lv2_left = _and_( notEqual(e, g), notEqual(d, g) );
  bvec4 interp_restriction_lv2_up   = _and_( notEqual(e, c), notEqual(b, c) );
  bvec4 interp_restriction_lv3_left = _and_( eq2(g,g0), not(eq2(d0,g0)) );
  bvec4 interp_restriction_lv3_up   = _and_( eq2(c,c1), not(eq2(b1,c1)) );

  bvec4 edr      = _and_( lessThan(weighted_distance(e, c, g, i, h5, f4, h, f),
                                   weighted_distance(h, d, i5, f, i4, b, e, i)), interp_restriction_lv1 );
  bvec4 edr_left = _and_( lessThanEqual(coef * df(f, g), df(h, c)), interp_restriction_lv2_left );
  bvec4 edr_up   = _and_( greaterThanEqual(df(f, g), coef * df(h, c)), interp_restriction_lv2_up );
  bvec4 edr3_left = interp_restriction_lv3_left;
  bvec4 edr3_up = interp_restriction_lv3_up;

  bvec4 t13 = _or_(fx_left, _and_(edr3_left, fx3_left));
  bvec4 t14 = _or_(fx_up, _and_(edr3_up, fx3_up));
  bvec4 nc = _and_(edr, _or_(fx,  _or_(_and_(edr_left, t13), _and_(edr_up, t14))));

  bvec4 px = lessThanEqual(df(e, f), df(e, h));

  vec3 res1 = nc.x ? px.x ? F : H : nc.y ? px.y ? B : F : nc.z ? px.z ? D : B : nc.w ? px.w ? H : D : E;
  vec3 res2 = nc.w ? px.w ? H : D : nc.z ? px.z ? D : B : nc.y ? px.y ? B : F : nc.x ? px.x ? F : H : E;

  vec3 res = mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));

  gl_FragColor = vec4(res, 1.0);
}
)GLSL";

// Joshua Street's SABR v3.0, "Optimized" variant (lowp-friendly, branchless,
// reordered texture reads - chosen over the plain v3.0 for a mobile Tegra
// GPU's sake). Same structure as 5xBR above: own vertex stage for the
// neighbor-texel varyings, mechanical aPos/aTex/uTex/uTexSize rename only.
const char* kVertexDrasticSabrSrc = R"GLSL(
attribute vec2 aPos;
attribute vec2 aTex;
uniform vec2 uTexSize;
varying vec2 v0;
varying vec4 v1;
varying vec4 v2;
varying vec4 v3;
varying vec4 v4;
varying vec4 v5;
varying vec4 v6;
varying vec4 v7;
void main() {
  gl_Position = vec4(aPos, 0.0, 1.0);
  v0 = aTex.xy;
  float x = 1.0 / uTexSize.x;
  float y = 1.0 / uTexSize.y;
  v1 = aTex.xxxy + vec4(      -x, 0.0,   x, -2.0 * y);
  v2 = aTex.xxxy + vec4(      -x, 0.0,   x,       -y);
  v3 = aTex.xxxy + vec4(      -x, 0.0,   x,      0.0);
  v4 = aTex.xxxy + vec4(      -x, 0.0,   x,        y);
  v5 = aTex.xxxy + vec4(      -x, 0.0,   x,  2.0 * y);
  v6 = aTex.xyyy + vec4(-2.0 * x,  -y, 0.0,        y);
  v7 = aTex.xyyy + vec4( 2.0 * x,  -y, 0.0,        y);
}
)GLSL";
const char* kFragDrasticSabrSrc = R"GLSL(
precision highp float;
uniform sampler2D uTex;
uniform vec2 uTexSize;
varying vec2 v0;
varying vec4 v1;
varying vec4 v2;
varying vec4 v3;
varying vec4 v4;
varying vec4 v5;
varying vec4 v6;
varying vec4 v7;

#define SCALE 4.0
const vec4 Ai  = vec4( 0.5 , -0.5 , -0.5 ,  0.5 );
const vec4 B45 = vec4( 0.5 ,  0.5 , -0.5 , -0.5 );
const vec4 C45 = vec4( 0.75,  0.25, -0.25,  0.25);
const vec4 B30 = vec4( 0.25,  1.0 , -0.25, -1.0 );
const vec4 C30 = vec4( 0.5 ,  0.5 , -0.25,  0.0 );
const vec4 B60 = vec4( 1.0 ,  0.25, -1.0 , -0.25);
const vec4 C60 = vec4( 1.0 ,  0.0 , -0.5 ,  0.25);
const vec4 M45 = vec4(1.0 / SCALE);
const vec4 M30 = vec4(0.5 / SCALE, 1.0 / SCALE, 0.5 / SCALE, 1.0 / SCALE);
const vec4 M60 = M30.yxwz;
const vec4 Mshift = vec4(0.5 / SCALE);
const vec4 coef = vec4(0.5);
const vec4 threshold = vec4(0.3125);
const vec3 lum = vec3(0.2126, 0.7152, 0.0722);

vec4 _not_(vec4 A) { return vec4(1.0) - A; }
vec4 _and_(vec4 A, vec4 B) { return A * B; }
vec4 _or_(vec4 A, vec4 B) { return max(A, B); }
vec4 _ne_(vec4 A, vec4 B) { return abs(sign(A - B)); }
vec4 _lte_(vec4 A, vec4 B) { return step(A, B); }
vec4 _gte_(vec4 A, vec4 B) { return _lte_(B, A); }
vec4 _lt_(vec4 A, vec4 B) { return _not_(_gte_(A, B)); }

vec4 lum_to(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {
  return vec4(dot(lum, v0), dot(lum, v1), dot(lum, v2), dot(lum, v3));
}
vec4 lum_df(vec4 A, vec4 B) { return abs(A - B); }
vec4 lum_eq(vec4 A, vec4 B) { return _lt_(lum_df(A, B), threshold); }
vec4 lum_wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
  return 0.125 * lum_df(a, b) + 0.125 * lum_df(a, c) + 0.125 * lum_df(d, e) + 0.125 * lum_df(d, f) + 0.5 * lum_df(g, h);
}
float c_df(vec3 c1, vec3 c2) { return dot(vec3(1.0/3.0), abs(c1 - c2)); }

void main() {
  vec3 P1  = texture2D(uTex, v1.xw).rgb;
  vec3 P2  = texture2D(uTex, v1.yw).rgb;
  vec3 P3  = texture2D(uTex, v1.zw).rgb;

  vec3 P6  = texture2D(uTex, v2.xw).rgb;
  vec3 P7  = texture2D(uTex, v2.yw).rgb;
  vec3 P8  = texture2D(uTex, v2.zw).rgb;

  vec3 P11 = texture2D(uTex, v3.xw).rgb;
  vec3 P12 = texture2D(uTex, v3.yw).rgb;
  vec3 P13 = texture2D(uTex, v3.zw).rgb;

  vec3 P16 = texture2D(uTex, v4.xw).rgb;
  vec3 P17 = texture2D(uTex, v4.yw).rgb;
  vec3 P18 = texture2D(uTex, v4.zw).rgb;

  vec3 P21 = texture2D(uTex, v5.xw).rgb;
  vec3 P22 = texture2D(uTex, v5.yw).rgb;
  vec3 P23 = texture2D(uTex, v5.zw).rgb;

  vec3 P5  = texture2D(uTex, v6.xy).rgb;
  vec3 P10 = texture2D(uTex, v6.xz).rgb;
  vec3 P15 = texture2D(uTex, v6.xw).rgb;

  vec3 P9  = texture2D(uTex, v7.xy).rgb;
  vec3 P14 = texture2D(uTex, v7.xz).rgb;
  vec3 P19 = texture2D(uTex, v7.xw).rgb;

  vec4 p7  = lum_to(P7,  P11, P17, P13);
  vec4 p8  = lum_to(P8,  P6,  P16, P18);
  vec4 p11 = p7.yzwx;
  vec4 p12 = lum_to(P12, P12, P12, P12);
  vec4 p13 = p7.wxyz;
  vec4 p14 = lum_to(P14, P2,  P10, P22);
  vec4 p16 = p8.zwxy;
  vec4 p17 = p7.zwxy;
  vec4 p18 = p8.wxyz;
  vec4 p19 = lum_to(P19, P3,  P5,  P21);
  vec4 p22 = p14.wxyz;
  vec4 p23 = lum_to(P23, P9,  P1,  P15);

  vec2 fp = fract(v0.xy * uTexSize);

  vec4 ma45 = smoothstep(C45 - M45, C45 + M45, Ai * fp.y + B45 * fp.x);
  vec4 ma30 = smoothstep(C30 - M30, C30 + M30, Ai * fp.y + B30 * fp.x);
  vec4 ma60 = smoothstep(C60 - M60, C60 + M60, Ai * fp.y + B60 * fp.x);
  vec4 marn = smoothstep(C45 - M45 + Mshift, C45 + M45 + Mshift, Ai * fp.y + B45 * fp.x);

  vec4 e45   = lum_wd(p12, p8, p16, p18, p22, p14, p17, p13);
  vec4 econt = lum_wd(p17, p11, p23, p13, p7, p19, p12, p18);
  vec4 e30   = lum_df(p13, p16);
  vec4 e60   = lum_df(p8, p17);

  vec4 r45 = _and_(
      _and_(_ne_(p12, p13), _ne_(p12, p17)),
      _or_(
          _or_(
              _and_(_not_(lum_eq(p13, p7)), _not_(lum_eq(p13, p8))),
              _and_(_not_(lum_eq(p17, p11)), _not_(lum_eq(p17, p16)))),
          _or_(
              _and_(
                  lum_eq(p12, p18),
                  _or_(
                      _and_(_not_(lum_eq(p13, p14)), _not_(lum_eq(p13, p19))),
                      _and_(_not_(lum_eq(p17, p22)), _not_(lum_eq(p17, p23))))),
              _or_(lum_eq(p12, p16), lum_eq(p12, p8)))));
  vec4 r30 = _and_(_ne_(p12, p16), _ne_(p11, p16));
  vec4 r60 = _and_(_ne_(p12, p8), _ne_(p7, p8));

  vec4 edr45 = _and_(_lt_(e45, econt), r45);
  vec4 edrrn = _lte_(e45, econt);
  vec4 edr30 = _and_(_lte_(e30, coef * e60), r30);
  vec4 edr60 = _and_(_lte_(e60, coef * e30), r60);
  vec4 final45 = _and_(_and_(_not_(edr30), _not_(edr60)), edr45);
  vec4 final30 = _and_(_and_(edr45, edr30), _not_(edr60));
  vec4 final60 = _and_(_and_(edr45, edr60), _not_(edr30));
  vec4 final36 = _and_(_and_(edr45, edr30), edr60);
  vec4 finalrn = _and_(_not_(edr45), edrrn);
  vec4 px = step(lum_df(p12, p17), lum_df(p12, p13));
  vec4 mac = final36 * max(ma30, ma60) + final30 * ma30 + final60 * ma60 + final45 * ma45 + finalrn * marn;

  vec3 res1 = P12;
  res1 = mix(res1, mix(P13, P17, px.x), mac.x);
  res1 = mix(res1, mix(P7 , P13, px.y), mac.y);
  res1 = mix(res1, mix(P11, P7 , px.z), mac.z);
  res1 = mix(res1, mix(P17, P11, px.w), mac.w);

  vec3 res2 = P12;
  res2 = mix(res2, mix(P17, P11, px.w), mac.w);
  res2 = mix(res2, mix(P11, P7 , px.z), mac.z);
  res2 = mix(res2, mix(P7 , P13, px.y), mac.y);
  res2 = mix(res2, mix(P13, P17, px.x), mac.x);

  gl_FragColor.rgb = mix(res1, res2, step(c_df(P12, res1), c_df(P12, res2)));
  gl_FragColor.a = 1.0;
}
)GLSL";

// Three more from the broader RetroArch/libretro shader ecosystem, this
// time via libretro/common-shaders directly rather than through DraStic's
// port of a subset of it (see switch/README.md for why NDS Color, the one
// DS-specific shader that bundle carried, was dropped rather than kept).
// Unlike that bundle, common-shaders is written in Cg (float4/float3,
// tex2D-style sampling via COMPAT_Sample*, semantic-bound struct fields)
// rather than already being GLSL - these three are hand-translated from
// their .cg sources rather than mechanically renamed, so treat them as a
// port of the *algorithm*, verified against the original logic line by
// line, not a byte-for-byte transcription the way the ones above were.
// All three are genuinely single-pass in the original (a couple of their
// upstream .cgp presets list 2 "passes", but the second is RetroArch's
// own generic final-output blit, not a real algorithmic pass - exactly
// what this file's own aspect-corrected final quad draw already is).
const char* kFragScale2xSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "bool eqv3(vec3 a, vec3 b) { return all(equal(a, b)); }\n"
    "bool neqv3(vec3 a, vec3 b) { return any(notEqual(a, b)); }\n"
    "void main() {\n"
    "  vec2 dx = vec2(1.0 / uTexSize.x, 0.0);\n"
    "  vec2 dy = vec2(0.0, 1.0 / uTexSize.y);\n"
    "  vec2 fp = floor(2.0 * fract(vTex * uTexSize));\n"
    "  vec3 B = texture2D(uTex, vTex - dy).rgb;\n"
    "  vec3 D = texture2D(uTex, vTex - dx).rgb;\n"
    "  vec3 E = texture2D(uTex, vTex).rgb;\n"
    "  vec3 F = texture2D(uTex, vTex + dx).rgb;\n"
    "  vec3 H = texture2D(uTex, vTex + dy).rgb;\n"
    "  vec3 res = E;\n"
    "  if (neqv3(B, H) && neqv3(D, F)) {\n"
    "    vec3 E0 = eqv3(B, D) ? B : E;\n"
    "    vec3 E1 = eqv3(B, F) ? B : E;\n"
    "    vec3 E2 = eqv3(H, D) ? H : E;\n"
    "    vec3 E3 = eqv3(H, F) ? H : E;\n"
    "    res = (fp.y < 0.5) ? (fp.x < 0.5 ? E0 : E1) : (fp.x < 0.5 ? E2 : E3);\n"
    "  }\n"
    "  gl_FragColor = vec4(res, 1.0);\n"
    "}\n";

const char* kFragScale3xSrc =
    "precision mediump float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "bool eqv3(vec3 a, vec3 b) { return all(equal(a, b)); }\n"
    "bool neqv3(vec3 a, vec3 b) { return any(notEqual(a, b)); }\n"
    "void main() {\n"
    "  vec2 dx = vec2(1.0 / uTexSize.x, 0.0);\n"
    "  vec2 dy = vec2(0.0, 1.0 / uTexSize.y);\n"
    "  vec2 fp = floor(3.0 * fract(vTex * uTexSize));\n"
    "  vec3 A = texture2D(uTex, vTex - dx - dy).rgb;\n"
    "  vec3 B = texture2D(uTex, vTex - dy).rgb;\n"
    "  vec3 C = texture2D(uTex, vTex + dx - dy).rgb;\n"
    "  vec3 D = texture2D(uTex, vTex - dx).rgb;\n"
    "  vec3 E = texture2D(uTex, vTex).rgb;\n"
    "  vec3 F = texture2D(uTex, vTex + dx).rgb;\n"
    "  vec3 G = texture2D(uTex, vTex - dx + dy).rgb;\n"
    "  vec3 H = texture2D(uTex, vTex + dy).rgb;\n"
    "  vec3 I = texture2D(uTex, vTex + dx + dy).rgb;\n"
    "  vec3 res = E;\n"
    "  if (neqv3(B, H) && neqv3(D, F)) {\n"
    "    bool eqBD = eqv3(B, D), eqBF = eqv3(B, F), eqHD = eqv3(H, D), eqHF = eqv3(H, F);\n"
    "    bool neqEA = neqv3(E, A), neqEC = neqv3(E, C), neqEG = neqv3(E, G), neqEI = neqv3(E, I);\n"
    "    vec3 E0 = eqBD ? B : E;\n"
    "    vec3 E1 = (eqBD && neqEC) || (eqBF && neqEA) ? B : E;\n"
    "    vec3 E2 = eqBF ? B : E;\n"
    "    vec3 E3 = (eqBD && neqEG) || (eqHD && neqEA) ? D : E;\n"
    "    vec3 E5 = (eqBF && neqEI) || (eqHF && neqEC) ? F : E;\n"
    "    vec3 E6 = eqHD ? H : E;\n"
    "    vec3 E7 = (eqHD && neqEI) || (eqHF && neqEG) ? H : E;\n"
    "    vec3 E8 = eqHF ? H : E;\n"
    "    if (fp.y < 0.5) res = (fp.x < 0.5) ? E0 : (fp.x < 1.5) ? E1 : E2;\n"
    "    else if (fp.y < 1.5) res = (fp.x < 0.5) ? E3 : (fp.x < 1.5) ? E : E5;\n"
    "    else res = (fp.x < 0.5) ? E6 : (fp.x < 1.5) ? E7 : E8;\n"
    "  }\n"
    "  gl_FragColor = vec4(res, 1.0);\n"
    "}\n";

// Super2xSaI (Derek Liauw Kie Fa's original GET_RESULT, DOSBox's own port
// of the rest - both GPL, see switch/README.md). `reduce()`'s odd-looking
// (65536,255,1) weights aren't a color-space conversion - c0..d6 below are
// only ever compared with == / !=, so this just needs to be injective
// enough that two different colors essentially never hash the same, not
// meaningful as a number on its own; kept as the original literal weights
// rather than re-derived.
const char* kFragSuper2xSaiSrc = R"GLSL(
precision mediump float;
varying vec2 vTex;
uniform sampler2D uTex;
uniform vec2 uTexSize;

const vec3 kDtt = vec3(65536.0, 255.0, 1.0);
float reduce(vec3 c) { return dot(c, kDtt); }

int getResult(float A, float B, float C, float D) {
  int x = 0;
  int y = 0;
  int r = 0;
  if (A == C) x += 1; else if (B == C) y += 1;
  if (A == D) x += 1; else if (B == D) y += 1;
  if (x <= 1) r += 1;
  if (y <= 1) r -= 1;
  return r;
}

void main() {
  vec2 ps = vec2(0.999 / uTexSize.x, 0.999 / uTexSize.y);
  vec2 dx = vec2(ps.x, 0.0);
  vec2 dy = vec2(0.0, ps.y);
  vec2 g1 = vec2(ps.x, ps.y);
  vec2 g2 = vec2(-ps.x, ps.y);

  vec2 pixcoord = vTex / ps;
  vec2 fp = fract(pixcoord);
  vec2 pC4 = vTex - fp * ps;
  vec2 pC8 = pC4 + g1;

  vec3 C0 = texture2D(uTex, pC4 - g1).rgb;
  vec3 C1 = texture2D(uTex, pC4 - dy).rgb;
  vec3 C2 = texture2D(uTex, pC4 - g2).rgb;
  vec3 D3 = texture2D(uTex, pC4 - g2 + dx).rgb;
  vec3 C3 = texture2D(uTex, pC4 - dx).rgb;
  vec3 C4 = texture2D(uTex, pC4).rgb;
  vec3 C5 = texture2D(uTex, pC4 + dx).rgb;
  vec3 D4 = texture2D(uTex, pC8 - g2).rgb;
  vec3 C6 = texture2D(uTex, pC4 + g2).rgb;
  vec3 C7 = texture2D(uTex, pC4 + dy).rgb;
  vec3 C8 = texture2D(uTex, pC4 + g1).rgb;
  vec3 D5 = texture2D(uTex, pC8 + dx).rgb;
  vec3 D0 = texture2D(uTex, pC4 + g2 + dy).rgb;
  vec3 D1 = texture2D(uTex, pC8 + g2).rgb;
  vec3 D2 = texture2D(uTex, pC8 + dy).rgb;
  vec3 D6 = texture2D(uTex, pC8 + g1).rgb;

  float c0 = reduce(C0); float c1 = reduce(C1);
  float c2 = reduce(C2); float c3 = reduce(C3);
  float c4 = reduce(C4); float c5 = reduce(C5);
  float c6 = reduce(C6); float c7 = reduce(C7);
  float c8 = reduce(C8); float d0 = reduce(D0);
  float d1 = reduce(D1); float d2 = reduce(D2);
  float d3 = reduce(D3); float d4 = reduce(D4);
  float d5 = reduce(D5); float d6 = reduce(D6);

  vec3 p11, p01, p10, p00;

  if (c7 == c5 && c4 != c8) {
    p11 = p01 = C7;
  } else if (c4 == c8 && c7 != c5) {
    p11 = p01 = C4;
  } else if (c4 == c8 && c7 == c5) {
    int r = 0;
    r += getResult(c5, c4, c6, d1);
    r += getResult(c5, c4, c3, c1);
    r += getResult(c5, c4, d2, d5);
    r += getResult(c5, c4, c2, d4);
    if (r > 0) p11 = p01 = C5;
    else if (r < 0) p11 = p01 = C4;
    else p11 = p01 = 0.5 * (C4 + C5);
  } else {
    if (c5 == c8 && c8 == d1 && c7 != d2 && c8 != d0) p11 = 0.25 * (3.0 * C8 + C7);
    else if (c4 == c7 && c7 == d2 && d1 != c8 && c7 != d6) p11 = 0.25 * (3.0 * C7 + C8);
    else p11 = 0.5 * (C7 + C8);

    if (c5 == c8 && c5 == c1 && c4 != c2 && c5 != c0) p01 = 0.25 * (3.0 * C5 + C4);
    else if (c4 == c7 && c4 == c2 && c1 != c5 && c4 != d3) p01 = 0.25 * (3.0 * C4 + C5);
    else p01 = 0.5 * (C4 + C5);
  }

  if (c4 == c8 && c7 != c5 && c3 == c4 && c4 != d2) p10 = 0.5 * (C7 + C4);
  else if (c4 == c6 && c5 == c4 && c3 != c7 && c4 != d0) p10 = 0.5 * (C7 + C4);
  else p10 = C7;

  if (c7 == c5 && c4 != c8 && c6 == c7 && c7 != c2) p00 = 0.5 * (C7 + C4);
  else if (c3 == c7 && c8 == c7 && c6 != c4 && c7 != c0) p00 = 0.5 * (C7 + C4);
  else p00 = C4;

  if (fp.x < 0.5) { if (fp.y < 0.5) p10 = p00; }
  else { if (fp.y < 0.5) p10 = p01; else p10 = p11; }

  gl_FragColor = vec4(p10, 1.0);
}
)GLSL";

// zfast-crt (Greg Hogan/SoltanGris42, same author as zFast LCD above) - a
// deliberately cheap CRT look, distinct from D.Smile CRT's own (this one
// leans on a "weighted linear" resample plus a per-column aperture mask;
// D.Smile CRT does barrel distortion + glow + a per-column RGB mask).
// Ported with its default (non-`#pragma parameter`-overridden) constants
// baked in, same treatment as the DraStic shaders' own `#define`s above.
// Uses real `gl_FragCoord.x` for the column mask instead of reconstructing
// an equivalent from texture/video/output sizes the way the original does
// - D.Smile CRT's own mask already does exactly that, and for this
// renderer (no padding between "texture size" and "video size" the way a
// libretro core with overscan might have) they're the same value anyway.
const char* kFragZfastCrtSrc =
    "precision highp float;\n"
    "varying vec2 vTex;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec2 uTexSize;\n"
    "const float kBlurScaleX = 0.45;\n"
    "const float kLowLumScan = 5.0;\n"
    "const float kHiLumScan = 10.0;\n"
    "const float kBrightBoost = 1.25;\n"
    "const float kMaskDark = 0.25;\n"
    "const float kMaskFade = 0.8 * 0.333;\n"
    "void main() {\n"
    "  vec2 invDims = 1.0 / uTexSize;\n"
    "  vec2 p = vTex * uTexSize;\n"
    "  vec2 i = floor(p) + 0.5;\n"
    "  vec2 f = p - i;\n"
    "  p = (i + 4.0 * f * f * f) * invDims;\n"
    "  p.x = mix(p.x, vTex.x, kBlurScaleX);\n"
    "  float Y = f.y * f.y;\n"
    "  float YY = Y * Y;\n"
    "  float whichmask = fract(gl_FragCoord.x * -0.4999);\n"
    "  float mask = 1.0 + float(whichmask < 0.5) * -kMaskDark;\n"
    "  vec3 colour = texture2D(uTex, p).rgb;\n"
    "  float scanLineWeight = kBrightBoost - kLowLumScan * (Y - 2.05 * YY);\n"
    "  float scanLineWeightB = 1.0 - kHiLumScan * (YY - 2.8 * YY * Y);\n"
    "  vec3 res = colour * mix(scanLineWeight * mask, scanLineWeightB,\n"
    "                          dot(colour, vec3(kMaskFade)));\n"
    "  gl_FragColor = vec4(res, 1.0);\n"
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
Program g_drastic_lcd1x, g_drastic_sharp_bilinear, g_drastic_zfast_lcd,
    g_drastic_natural_vision, g_drastic_5xbr, g_drastic_sabr;
Program g_scale2x, g_scale3x, g_super2xsai, g_zfast_crt;
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

Program BuildDisplayProgram(const char* frag_src, const char* vert_src = kVertexSrc) {
  Program p;
  p.id = LinkProgram(vert_src, frag_src);
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

  g_drastic_lcd1x = BuildDisplayProgram(kFragDrasticLcd1xSrc);
  g_drastic_sharp_bilinear =
      BuildDisplayProgram(kFragDrasticSharpBilinearSrc, kVertexDrasticSharpBilinearSrc);
  g_drastic_zfast_lcd = BuildDisplayProgram(kFragDrasticZfastLcdSrc);
  g_drastic_natural_vision = BuildDisplayProgram(kFragDrasticNaturalVisionSrc);
  g_drastic_5xbr = BuildDisplayProgram(kFragDrastic5xbrSrc, kVertexDrastic5xbrSrc);
  g_drastic_sabr = BuildDisplayProgram(kFragDrasticSabrSrc, kVertexDrasticSabrSrc);
  g_scale2x = BuildDisplayProgram(kFragScale2xSrc);
  g_scale3x = BuildDisplayProgram(kFragScale3xSrc);
  g_super2xsai = BuildDisplayProgram(kFragSuper2xSaiSrc);
  g_zfast_crt = BuildDisplayProgram(kFragZfastCrtSrc);
  // Deliberately not a hard failure like pixel/sharp/crt above: these 10
  // are ported shaders new to this project - the first 6 are
  // hardware-confirmed working (see switch/README.md), the last 4
  // (Scale2x, Scale3x, Super2xSaI, zfast-crt) aren't tested on real
  // hardware yet. Either way, CompileShader/LinkProgram already print
  // exactly which one and why to the log if a driver rejects it, and
  // switch_render_frame()'s own shader table falls back to g_pixel for any
  // of these whose .id is still 0, so one bad shader degrades to "that
  // menu entry looks wrong" rather than "nothing boots."

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
  // xBR/SABR/Scale2x/Scale3x/Super2xSaI all read specific neighbor texels
  // via their own precise fract()/floor() math and do their own blending -
  // hardware bilinear filtering would pre-blend those samples and break
  // the algorithm, so (like "pixel") they need GL_NEAREST. Everything else
  // already expects/wants hardware bilinear (D.Smile Sharp and CRT already
  // relied on this; Sharp Bilinear is *built around* it, per its own
  // name; zfast-crt's own header calls its resample "a weighted linear
  // filter").
  const bool point_sample = shader == "pixel" || shader == "5xbr" || shader == "sabr" ||
                             shader == "scale2x" || shader == "scale3x" || shader == "super2xsai";
  const GLenum filter = point_sample ? GL_NEAREST : GL_LINEAR;
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

  // "linear" deliberately has no Program of its own - its whole effect
  // (matching third_party/drastic-ds-shaders/xbr-sabr/linear.dsd) is "plain
  // bilinear-filtered passthrough", which is exactly what g_pixel's own
  // shader already does; only the filter mode above differs. Table (rather
  // than a chain of == comparisons) so a Program whose .id is still 0 -
  // one of the ported shaders switch_render_init() above allowed to fail
  // individually - falls back to g_pixel instead of calling glUseProgram(0)
  // and drawing nothing. Key strings here match kShaderKeys in
  // switch_menu.cpp - the g_drastic_* variable names are just this file's
  // own (these shaders were ported via DraStic's own port of them, hence
  // the name), not what settings.ini or the menu call them; see
  // switch_menu.cpp's kShaderKeys/kShaderNames comment for why they're
  // *displayed* under their original RetroArch/libretro names instead.
  struct ShaderEntry {
    const char* key;
    const Program* prog;
  };
  const ShaderEntry kShaderTable[] = {
      {"crt", &g_crt},
      {"sharp", &g_sharp},
      {"lcd1x", &g_drastic_lcd1x},
      {"sharp_bilinear", &g_drastic_sharp_bilinear},
      {"zfast_lcd", &g_drastic_zfast_lcd},
      {"natural_vision", &g_drastic_natural_vision},
      {"5xbr", &g_drastic_5xbr},
      {"sabr", &g_drastic_sabr},
      {"scale2x", &g_scale2x},
      {"scale3x", &g_scale3x},
      {"super2xsai", &g_super2xsai},
      {"zfast_crt", &g_zfast_crt},
  };
  const Program* prog = &g_pixel;  // "pixel", "linear", and the fallback
  for (const ShaderEntry& entry : kShaderTable) {
    if (shader == entry.key) {
      if (entry.prog->id) prog = entry.prog;
      break;
    }
  }
  glUseProgram(prog->id);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  if (prog->uTex >= 0) glUniform1i(prog->uTex, 0);
  if (prog->uTexSize >= 0) glUniform2f(prog->uTexSize, (float)kFbW, (float)kFbH);
  if (prog->uOutSize >= 0) glUniform2f(prog->uOutSize, (float)g_viewport_w * qw, (float)g_viewport_h * qh);
  if (shader == "crt") {
    if (prog->uCurve >= 0) glUniform1f(prog->uCurve, g_settings.crt_curve);
    if (prog->uGlow >= 0) glUniform1f(prog->uGlow, g_settings.crt_glow);
    if (prog->uScan >= 0) glUniform1f(prog->uScan, g_settings.crt_scan);
    if (prog->uMask >= 0) glUniform1f(prog->uMask, g_settings.crt_mask);
    if (prog->uVig >= 0) glUniform1f(prog->uVig, g_settings.crt_vignette);
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
  if (g_drastic_lcd1x.id) glDeleteProgram(g_drastic_lcd1x.id);
  if (g_drastic_sharp_bilinear.id) glDeleteProgram(g_drastic_sharp_bilinear.id);
  if (g_drastic_zfast_lcd.id) glDeleteProgram(g_drastic_zfast_lcd.id);
  if (g_drastic_natural_vision.id) glDeleteProgram(g_drastic_natural_vision.id);
  if (g_drastic_5xbr.id) glDeleteProgram(g_drastic_5xbr.id);
  if (g_drastic_sabr.id) glDeleteProgram(g_drastic_sabr.id);
  if (g_scale2x.id) glDeleteProgram(g_scale2x.id);
  if (g_scale3x.id) glDeleteProgram(g_scale3x.id);
  if (g_super2xsai.id) glDeleteProgram(g_super2xsai.id);
  if (g_zfast_crt.id) glDeleteProgram(g_zfast_crt.id);
  if (g_bg_program) glDeleteProgram(g_bg_program);
  if (g_bezel_program) glDeleteProgram(g_bezel_program);
  g_tex = g_vbo = g_vao = 0;
  g_pixel = Program{};
  g_sharp = Program{};
  g_crt = Program{};
  g_drastic_lcd1x = Program{};
  g_drastic_sharp_bilinear = Program{};
  g_drastic_zfast_lcd = Program{};
  g_drastic_natural_vision = Program{};
  g_drastic_5xbr = Program{};
  g_drastic_sabr = Program{};
  g_scale2x = Program{};
  g_scale3x = Program{};
  g_super2xsai = Program{};
  g_zfast_crt = Program{};
  g_bg_program = g_bezel_program = 0;
}
