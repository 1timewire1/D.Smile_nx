package com.dsmile.emulator.emu

import android.opengl.GLES20
import android.opengl.GLSurfaceView
import java.nio.ByteBuffer
import java.nio.ByteOrder
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

enum class ShaderMode { PIXEL, SHARP, CRT }
enum class AspectMode { FOUR_THREE, STRETCH, INTEGER }
enum class BackgroundMode { BLACK, VSMILE_BLUE, VSMILE_PURPLE }
enum class BezelMode { NONE, SILVER, BLACK }

class GameRenderer : GLSurfaceView.Renderer {
    @Volatile var shaderMode = ShaderMode.SHARP
    @Volatile var aspectMode = AspectMode.FOUR_THREE
    @Volatile var backgroundMode = BackgroundMode.BLACK
    @Volatile var bezelMode = BezelMode.NONE

    // CRT effect intensities 0..1 (only used by the CRT shader)
    @Volatile var crtCurve = 1f
    @Volatile var crtGlow = 1f
    @Volatile var crtScan = 1f
    @Volatile var crtMask = 1f
    @Volatile var crtVignette = 1f

    private val frameBuf: ByteBuffer =
        ByteBuffer.allocateDirect(320 * 240 * 2).order(ByteOrder.nativeOrder())
    private var lastSeq = -1
    private var tex = 0
    private var viewW = 1
    private var viewH = 1
    private val programs = HashMap<ShaderMode, Int>()
    private var bgProgram = 0
    private var bezelProgram = 0
    private var bezelQuad: ByteBuffer = ByteBuffer.allocateDirect(16 * 4).order(ByteOrder.nativeOrder())
    private var quad: ByteBuffer = ByteBuffer.allocateDirect(16 * 4).order(ByteOrder.nativeOrder())
    private var bgQuad: ByteBuffer = ByteBuffer.allocateDirect(16 * 4).order(ByteOrder.nativeOrder())

    private val vertexSrc = """
        attribute vec2 aPos;
        attribute vec2 aTex;
        varying vec2 vTex;
        void main() {
          gl_Position = vec4(aPos, 0.0, 1.0);
          vTex = aTex;
        }
    """

    private val fragPixel = """
        precision mediump float;
        varying vec2 vTex;
        uniform sampler2D uTex;
        void main() { gl_FragColor = texture2D(uTex, vTex); }
    """

    // Sharp-bilinear: nearest at integer zoom, bilinear only at pixel edges.
    private val fragSharp = """
        precision highp float;
        varying vec2 vTex;
        uniform sampler2D uTex;
        uniform vec2 uTexSize;
        uniform vec2 uOutSize;
        void main() {
          vec2 texel = vTex * uTexSize;
          vec2 scale = max(floor(uOutSize / uTexSize), vec2(1.0));
          vec2 texelFloor = floor(texel);
          vec2 f = texel - texelFloor;
          vec2 region = vec2(0.5) - 0.5 / scale;
          f = (clamp(f, region, vec2(1.0) - region) - region) / (1.0 - 2.0 * region);
          gl_FragColor = texture2D(uTex, (texelFloor + f) / uTexSize);
        }
    """

    // Single-pass CRT with individually toggleable effects:
    // barrel curvature, phosphor glow, luminance-aware scanlines,
    // aperture grille, corner vignette.
    private val fragCrt = """
        precision highp float;
        varying vec2 vTex;
        uniform sampler2D uTex;
        uniform vec2 uTexSize;
        uniform vec2 uOutSize;
        uniform float uCurve;
        uniform float uGlow;
        uniform float uScan;
        uniform float uMask;
        uniform float uVig;
        void main() {
          vec2 p = vTex * 2.0 - 1.0;
          float r2 = dot(p, p);
          p *= 1.0 + uCurve * (0.045 * r2 + 0.025 * r2 * r2);
          vec2 uv = p * 0.5 + 0.5;
          // black beyond the curved tube, with a soft edge
          vec2 lim = abs(uv * 2.0 - 1.0);
          float edge = 1.0 - smoothstep(0.992, 1.0, max(lim.x, lim.y)) * uCurve;
          if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            gl_FragColor = vec4(0.0);  // transparent: background/bezel shows through
            return;
          }
          vec2 texel = uv * uTexSize;
          vec2 tf = floor(texel) + 0.5;
          vec2 f = clamp((texel - tf) * 1.7, -0.5, 0.5);
          vec3 col = texture2D(uTex, (tf + f) / uTexSize).rgb;
          // phosphor glow: 6-tap halo, brightens neighbours of hot pixels
          vec2 px = 1.0 / uTexSize;
          vec3 halo = texture2D(uTex, uv + vec2(px.x, 0.0)).rgb
                    + texture2D(uTex, uv - vec2(px.x, 0.0)).rgb
                    + texture2D(uTex, uv + vec2(0.0, px.y)).rgb
                    + texture2D(uTex, uv - vec2(0.0, px.y)).rgb
                    + texture2D(uTex, uv + px * vec2(1.0, -1.0)).rgb
                    + texture2D(uTex, uv - px * vec2(1.0, -1.0)).rgb;
          halo /= 6.0;
          col = mix(col, max(col, halo), uGlow * 0.5);
          col += halo * halo * uGlow * 0.3;
          // scanlines: bright pixels bloom over the dark gaps
          float lum = dot(col, vec3(0.299, 0.587, 0.114));
          float scanAmt = uScan * max(0.12, 0.38 - 0.24 * lum);
          col *= 1.0 - scanAmt * (0.5 + 0.5 * cos(6.28318 * texel.y));
          // aperture grille
          float m = mod(gl_FragCoord.x, 3.0);
          vec3 mask;
          if (m < 1.0)      mask = vec3(1.12, 0.92, 0.92);
          else if (m < 2.0) mask = vec3(0.92, 1.12, 0.92);
          else              mask = vec3(0.92, 0.92, 1.12);
          col *= mix(vec3(1.0), mask, uMask);
          // vignette
          col *= 1.0 - uVig * 0.32 * r2;
          // brightness compensation for scan/mask losses
          col *= 1.0 + 0.10 * uScan + 0.06 * uMask;
          gl_FragColor = vec4(col * edge, edge);  // premultiplied edge fade
        }
    """

    // Letterbox background: V.Smile box-art blue (light wavy gradient) or console purple.
    private val fragBg = """
        precision mediump float;
        varying vec2 vTex;
        uniform float uMode;
        void main() {
          vec2 uv = vTex;
          vec3 col;
          float wave = sin((uv.x * 7.0) + uv.y * 3.0) * 0.5 + 0.5;
          float band = smoothstep(0.35, 0.95, sin(uv.y * 6.0 - uv.x * 2.5) * 0.5 + 0.5);
          if (uMode < 1.5) {
            vec3 top = vec3(0.80, 0.92, 0.99);
            vec3 bot = vec3(0.25, 0.60, 0.90);
            col = mix(top, bot, uv.y);
            col += (wave * 0.05 + band * 0.06) * vec3(0.9, 0.97, 1.0) * (1.0 - uv.y * 0.5);
          } else {
            vec3 top = vec3(0.50, 0.40, 0.79);
            vec3 bot = vec3(0.27, 0.19, 0.49);
            col = mix(top, bot, uv.y);
            col += (wave * 0.03 + band * 0.04) * vec3(0.75, 0.65, 1.0);
          }
          gl_FragColor = vec4(col, 1.0);
        }
    """

    // TV bezel: rounded frame with dark tube glass behind the picture.
    // Works with curvature (curved corners reveal glass, not background).
    private val fragBezel = """
        precision highp float;
        varying vec2 vTex;
        uniform float uMat;    // 1 = silver, 2 = black
        uniform float uInner;  // inner opening half-size (fraction of quad)
        float rrect(vec2 p, vec2 b, float r) {
          vec2 d = abs(p) - b + r;
          return length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0) - r;
        }
        void main() {
          vec2 p = vTex * 2.0 - 1.0;
          float dOut = rrect(p, vec2(1.0), 0.14);
          float dIn = rrect(p, vec2(uInner), 0.07);
          float aOut = 1.0 - smoothstep(-0.012, 0.0, dOut);
          if (aOut <= 0.0) { gl_FragColor = vec4(0.0); return; }
          vec3 col;
          if (dIn < 0.0) {
            // tube glass: near-black with a soft top sheen
            float sheen = smoothstep(0.4, 1.0, -p.y) * 0.05;
            col = vec3(0.03 + sheen);
          } else {
            float v = vTex.y;
            float lip = smoothstep(0.035, 0.0, dIn);      // inner edge highlight
            float rim = smoothstep(-0.05, 0.0, dOut);     // outer edge shading
            if (uMat < 1.5) {
              float base = 0.82 - 0.28 * v;
              base += 0.10 * smoothstep(0.35, 0.0, abs(v - 0.18));  // top specular band
              col = vec3(base) * vec3(0.97, 0.98, 1.0);
              col += lip * 0.14;
              col *= 1.0 - rim * 0.35;
            } else {
              float base = 0.17 - 0.06 * v;
              col = vec3(base);
              col += lip * 0.10;
              col *= 1.0 - rim * 0.45;
            }
          }
          gl_FragColor = vec4(col * aOut, aOut);
        }
    """

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        programs[ShaderMode.PIXEL] = buildProgram(vertexSrc, fragPixel)
        programs[ShaderMode.SHARP] = buildProgram(vertexSrc, fragSharp)
        programs[ShaderMode.CRT] = buildProgram(vertexSrc, fragCrt)
        bgProgram = buildProgram(vertexSrc, fragBg)
        bezelProgram = buildProgram(vertexSrc, fragBezel)
        bgQuad.position(0)
        bgQuad.asFloatBuffer().put(
            floatArrayOf(-1f, -1f, 0f, 1f, 1f, -1f, 1f, 1f, -1f, 1f, 0f, 0f, 1f, 1f, 1f, 0f)
        )

        val texs = IntArray(1)
        GLES20.glGenTextures(1, texs, 0)
        tex = texs[0]
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex)
        GLES20.glTexImage2D(
            GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGB, 320, 240, 0,
            GLES20.GL_RGB, GLES20.GL_UNSIGNED_SHORT_5_6_5, null
        )
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        lastSeq = -1
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        viewW = width
        viewH = height
        GLES20.glViewport(0, 0, width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

        if (backgroundMode != BackgroundMode.BLACK) {
            GLES20.glUseProgram(bgProgram)
            val bPos = GLES20.glGetAttribLocation(bgProgram, "aPos")
            val bTex = GLES20.glGetAttribLocation(bgProgram, "aTex")
            bgQuad.position(0)
            GLES20.glVertexAttribPointer(bPos, 2, GLES20.GL_FLOAT, false, 16, bgQuad)
            bgQuad.position(8)
            GLES20.glVertexAttribPointer(bTex, 2, GLES20.GL_FLOAT, false, 16, bgQuad)
            GLES20.glEnableVertexAttribArray(bPos)
            GLES20.glEnableVertexAttribArray(bTex)
            GLES20.glUniform1f(
                GLES20.glGetUniformLocation(bgProgram, "uMode"),
                if (backgroundMode == BackgroundMode.VSMILE_BLUE) 1f else 2f
            )
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }

        val seq = NativeCore.nativeGetFrame(frameBuf)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex)
        if (seq != lastSeq) {
            lastSeq = seq
            frameBuf.position(0)
            GLES20.glTexSubImage2D(
                GLES20.GL_TEXTURE_2D, 0, 0, 0, 320, 240,
                GLES20.GL_RGB, GLES20.GL_UNSIGNED_SHORT_5_6_5, frameBuf
            )
        }

        val filter = if (shaderMode == ShaderMode.PIXEL) GLES20.GL_NEAREST else GLES20.GL_LINEAR
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, filter)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, filter)

        // Aspect-corrected quad
        var qw = 1f
        var qh = 1f
        when (aspectMode) {
            AspectMode.STRETCH -> {}
            AspectMode.FOUR_THREE -> {
                val target = 4f / 3f
                val view = viewW.toFloat() / viewH
                if (view > target) qw = target / view else qh = view / target
            }
            AspectMode.INTEGER -> {
                val scale = maxOf(1, minOf(viewW / 320, viewH / 240))
                qw = 320f * scale / viewW
                qh = 240f * scale / viewH
            }
        }

        GLES20.glEnable(GLES20.GL_BLEND)
        GLES20.glBlendFunc(GLES20.GL_ONE, GLES20.GL_ONE_MINUS_SRC_ALPHA)

        val inner = 0.86f
        if (bezelMode != BezelMode.NONE) {
            bezelQuad.position(0)
            bezelQuad.asFloatBuffer().put(
                floatArrayOf(-qw, -qh, 0f, 1f, qw, -qh, 1f, 1f, -qw, qh, 0f, 0f, qw, qh, 1f, 0f)
            )
            GLES20.glUseProgram(bezelProgram)
            val zPos = GLES20.glGetAttribLocation(bezelProgram, "aPos")
            val zTex = GLES20.glGetAttribLocation(bezelProgram, "aTex")
            bezelQuad.position(0)
            GLES20.glVertexAttribPointer(zPos, 2, GLES20.GL_FLOAT, false, 16, bezelQuad)
            bezelQuad.position(8)
            GLES20.glVertexAttribPointer(zTex, 2, GLES20.GL_FLOAT, false, 16, bezelQuad)
            GLES20.glEnableVertexAttribArray(zPos)
            GLES20.glEnableVertexAttribArray(zTex)
            GLES20.glUniform1f(
                GLES20.glGetUniformLocation(bezelProgram, "uMat"),
                if (bezelMode == BezelMode.SILVER) 1f else 2f
            )
            GLES20.glUniform1f(GLES20.glGetUniformLocation(bezelProgram, "uInner"), inner)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
            // the picture sits inside the bezel opening
            qw *= inner
            qh *= inner
        }

        quad.position(0)
        quad.asFloatBuffer().put(
            floatArrayOf(
                -qw, -qh, 0f, 1f,
                qw, -qh, 1f, 1f,
                -qw, qh, 0f, 0f,
                qw, qh, 1f, 0f
            )
        )

        val prog = programs[shaderMode] ?: return
        GLES20.glUseProgram(prog)
        val aPos = GLES20.glGetAttribLocation(prog, "aPos")
        val aTex = GLES20.glGetAttribLocation(prog, "aTex")
        quad.position(0)
        GLES20.glVertexAttribPointer(aPos, 2, GLES20.GL_FLOAT, false, 16, quad)
        quad.position(8)
        GLES20.glVertexAttribPointer(aTex, 2, GLES20.GL_FLOAT, false, 16, quad)
        GLES20.glEnableVertexAttribArray(aPos)
        GLES20.glEnableVertexAttribArray(aTex)
        GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uTex"), 0)
        GLES20.glGetUniformLocation(prog, "uTexSize").let {
            if (it >= 0) GLES20.glUniform2f(it, 320f, 240f)
        }
        GLES20.glGetUniformLocation(prog, "uOutSize").let {
            if (it >= 0) GLES20.glUniform2f(it, viewW * qw, viewH * qh)
        }
        if (shaderMode == ShaderMode.CRT) {
            fun setF(name: String, v: Float) {
                GLES20.glGetUniformLocation(prog, name).let {
                    if (it >= 0) GLES20.glUniform1f(it, v)
                }
            }
            setF("uCurve", crtCurve)
            setF("uGlow", crtGlow)
            setF("uScan", crtScan)
            setF("uMask", crtMask)
            setF("uVig", crtVignette)
        }
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisable(GLES20.GL_BLEND)
    }

    private fun buildProgram(vs: String, fs: String): Int {
        fun compile(type: Int, src: String): Int {
            val s = GLES20.glCreateShader(type)
            GLES20.glShaderSource(s, src)
            GLES20.glCompileShader(s)
            return s
        }
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, compile(GLES20.GL_VERTEX_SHADER, vs))
        GLES20.glAttachShader(p, compile(GLES20.GL_FRAGMENT_SHADER, fs))
        GLES20.glLinkProgram(p)
        return p
    }
}
