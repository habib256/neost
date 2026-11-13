// NeoST — pile d'effets CRT universelle (portée de POM2, VERHILLE Arnaud 2026).

#include "gui/CrtEffectStack.h"
#include "util/Log.hpp"
#include "gui/OpenGLShader.h"

#include <algorithm>
#include <cstdio>
#include <string>

#if defined(__EMSCRIPTEN__)
#  include <GLES3/gl3.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GLFW/glfw3.h>

// Points d'entrée GL 2.0+ chargés paresseusement (Linux/Windows). Même
// stratégie que OpenGLShader.cpp — gardés locaux au fichier pour que les deux
// unités restent indépendantes.
namespace {
PFNGLGENFRAMEBUFFERSPROC        glGenFramebuffers_        = nullptr;
PFNGLBINDFRAMEBUFFERPROC        glBindFramebuffer_        = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC   glFramebufferTexture2D_   = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC     glDeleteFramebuffers_     = nullptr;
PFNGLGENVERTEXARRAYSPROC        glGenVertexArrays_        = nullptr;
PFNGLBINDVERTEXARRAYPROC        glBindVertexArray_        = nullptr;
PFNGLDELETEVERTEXARRAYSPROC     glDeleteVertexArrays_     = nullptr;
PFNGLGENBUFFERSPROC             glGenBuffers_             = nullptr;
PFNGLBINDBUFFERPROC             glBindBuffer_             = nullptr;
PFNGLBUFFERDATAPROC             glBufferData_             = nullptr;
PFNGLDELETEBUFFERSPROC          glDeleteBuffers_          = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC    glVertexAttribPointer_    = nullptr;
PFNGLUSEPROGRAMPROC             glUseProgram_             = nullptr;
PFNGLGETUNIFORMLOCATIONPROC     glGetUniformLocation_     = nullptr;
PFNGLUNIFORM1IPROC              glUniform1i_              = nullptr;
PFNGLUNIFORM1FPROC              glUniform1f_              = nullptr;
PFNGLUNIFORM2FPROC              glUniform2f_              = nullptr;
PFNGLACTIVETEXTUREPROC          glActiveTexture_          = nullptr;
bool entryPointsLoaded_ = false;
bool loadEntryPoints()
{
    if (entryPointsLoaded_) return true;
    auto get = [](const char* n) {
        return reinterpret_cast<void*>(glfwGetProcAddress(n));
    };
#define LOAD(t, v, n) v = reinterpret_cast<t>(get(n))
    LOAD(PFNGLGENFRAMEBUFFERSPROC,        glGenFramebuffers_,        "glGenFramebuffers");
    LOAD(PFNGLBINDFRAMEBUFFERPROC,        glBindFramebuffer_,        "glBindFramebuffer");
    LOAD(PFNGLFRAMEBUFFERTEXTURE2DPROC,   glFramebufferTexture2D_,   "glFramebufferTexture2D");
    LOAD(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus_, "glCheckFramebufferStatus");
    LOAD(PFNGLDELETEFRAMEBUFFERSPROC,     glDeleteFramebuffers_,     "glDeleteFramebuffers");
    LOAD(PFNGLGENVERTEXARRAYSPROC,        glGenVertexArrays_,        "glGenVertexArrays");
    LOAD(PFNGLBINDVERTEXARRAYPROC,        glBindVertexArray_,        "glBindVertexArray");
    LOAD(PFNGLDELETEVERTEXARRAYSPROC,     glDeleteVertexArrays_,     "glDeleteVertexArrays");
    LOAD(PFNGLGENBUFFERSPROC,             glGenBuffers_,             "glGenBuffers");
    LOAD(PFNGLBINDBUFFERPROC,             glBindBuffer_,             "glBindBuffer");
    LOAD(PFNGLBUFFERDATAPROC,             glBufferData_,             "glBufferData");
    LOAD(PFNGLDELETEBUFFERSPROC,          glDeleteBuffers_,          "glDeleteBuffers");
    LOAD(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray_, "glEnableVertexAttribArray");
    LOAD(PFNGLVERTEXATTRIBPOINTERPROC,    glVertexAttribPointer_,    "glVertexAttribPointer");
    LOAD(PFNGLUSEPROGRAMPROC,             glUseProgram_,             "glUseProgram");
    LOAD(PFNGLGETUNIFORMLOCATIONPROC,     glGetUniformLocation_,     "glGetUniformLocation");
    LOAD(PFNGLUNIFORM1IPROC,              glUniform1i_,              "glUniform1i");
    LOAD(PFNGLUNIFORM1FPROC,              glUniform1f_,              "glUniform1f");
    LOAD(PFNGLUNIFORM2FPROC,              glUniform2f_,              "glUniform2f");
    LOAD(PFNGLACTIVETEXTUREPROC,          glActiveTexture_,          "glActiveTexture");
#undef LOAD
    entryPointsLoaded_ =
        glGenFramebuffers_ && glBindFramebuffer_ && glFramebufferTexture2D_ &&
        glCheckFramebufferStatus_ && glDeleteFramebuffers_ &&
        glGenVertexArrays_ && glBindVertexArray_ && glDeleteVertexArrays_ &&
        glGenBuffers_ && glBindBuffer_ && glBufferData_ && glDeleteBuffers_ &&
        glEnableVertexAttribArray_ && glVertexAttribPointer_ &&
        glUseProgram_ && glGetUniformLocation_ &&
        glUniform1i_ && glUniform1f_ && glUniform2f_ && glActiveTexture_;
    return entryPointsLoaded_;
}
} // namespace
#  define glGenFramebuffers        glGenFramebuffers_
#  define glBindFramebuffer        glBindFramebuffer_
#  define glFramebufferTexture2D   glFramebufferTexture2D_
#  define glCheckFramebufferStatus glCheckFramebufferStatus_
#  define glDeleteFramebuffers     glDeleteFramebuffers_
#  define glGenVertexArrays        glGenVertexArrays_
#  define glBindVertexArray        glBindVertexArray_
#  define glDeleteVertexArrays     glDeleteVertexArrays_
#  define glGenBuffers             glGenBuffers_
#  define glBindBuffer             glBindBuffer_
#  define glBufferData             glBufferData_
#  define glDeleteBuffers          glDeleteBuffers_
#  define glEnableVertexAttribArray glEnableVertexAttribArray_
#  define glVertexAttribPointer    glVertexAttribPointer_
#  define glUseProgram             glUseProgram_
#  define glGetUniformLocation     glGetUniformLocation_
#  define glUniform1i              glUniform1i_
#  define glUniform1f              glUniform1f_
#  define glUniform2f              glUniform2f_
#  define glActiveTexture          glActiveTexture_
#endif

namespace neost {

#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
namespace { bool loadEntryPoints() { return true; } }
#endif

namespace {

#if defined(__APPLE__)
// NeoST conserve sur macOS le contexte OpenGL 2.1 de compatibilité requis par
// ImGui OpenGL2 et le rendu immédiat du kiosk. Apple n'y expose les FBO qu'avec
// les noms EXT (les noms core ne deviennent valides qu'avec un contexte 3.2).
bool g_appleLegacyGl = false;
void genFramebuffersCompat(GLsizei n, GLuint* ids) {
    g_appleLegacyGl ? glGenFramebuffersEXT(n, ids) : glGenFramebuffers(n, ids);
}
void bindFramebufferCompat(GLenum target, GLuint id) {
    g_appleLegacyGl ? glBindFramebufferEXT(target, id) : glBindFramebuffer(target, id);
}
void framebufferTexture2DCompat(GLenum target, GLenum attachment, GLenum texTarget,
                                GLuint tex, GLint level) {
    g_appleLegacyGl ? glFramebufferTexture2DEXT(target, attachment, texTarget, tex, level)
                    : glFramebufferTexture2D(target, attachment, texTarget, tex, level);
}
GLenum checkFramebufferStatusCompat(GLenum target) {
    return g_appleLegacyGl ? glCheckFramebufferStatusEXT(target)
                           : glCheckFramebufferStatus(target);
}
void deleteFramebuffersCompat(GLsizei n, const GLuint* ids) {
    g_appleLegacyGl ? glDeleteFramebuffersEXT(n, ids) : glDeleteFramebuffers(n, ids);
}
#  define glGenFramebuffers        genFramebuffersCompat
#  define glBindFramebuffer        bindFramebufferCompat
#  define glFramebufferTexture2D   framebufferTexture2DCompat
#  define glCheckFramebufferStatus checkFramebufferStatusCompat
#  define glDeleteFramebuffers     deleteFramebuffersCompat
// Ces noms core ne sont pas déclarés par <OpenGL/gl.h>. Ils restent présents
// dans les branches mortes du code quand le contexte Apple legacy n'emploie
// aucun VAO ; les aliases permettent néanmoins de compiler cette TU.
#  define glGenVertexArrays        glGenVertexArraysAPPLE
#  define glBindVertexArray        glBindVertexArrayAPPLE
#  define glDeleteVertexArrays     glDeleteVertexArraysAPPLE
#endif

const char* kVertexShader = R"GLSL(
#if __VERSION__ < 130
attribute vec2 aPos;
varying vec2 vUv;
#else
in vec2 aPos;
out vec2 vUv;
#endif
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

// Shader d'effets seuls. Entrée = framebuffer RGBA déjà rendu (l'écran ST) ;
// on applique la façade « verre » du CRT. La teinte est appliquée ici aussi
// (rotation chroma sur du RGB).
const char* kFragmentShader = R"GLSL(
#if __VERSION__ < 130
varying vec2 vUv;
#define texture texture2D
#define FRAG_COLOR gl_FragColor
#else
in vec2 vUv;
out vec4 fragColor;
#define FRAG_COLOR fragColor
#endif

uniform sampler2D uSrc;        // framebuffer RGBA source
uniform sampler2D uPrev;       // sortie précédente (persistance)
uniform vec2  uSrcSize;        // (largeur, hauteur) de uSrc
uniform vec2  uOutSize;        // (largeur, hauteur) de cette passe
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uHue;            // -0.5..+0.5 → rotation chroma ±π
uniform float uSharpness;      // 0.5 = neutre ; >0.5 accentue, <0.5 adoucit
uniform float uPersistence;
uniform float uScanlines;
uniform float uBarrel;
uniform int   uShadowMask;     // 0=off,1=triade,2=grille,3=points
uniform float uShadowStrength; // 0..1
uniform float uLuminanceGain;  // re-brillance post-verre, 1.0 = neutre
uniform float uCenterLighting; // vignette : 1.0 = plat (off), <1 assombrit les bords
uniform float uPhosphorGamma;  // réponse phosphore γ : 1.0 = plat (off)

// Poids cubique Catmull-Rom (4 taps/axe). Utilisé quand la passe agrandit le
// framebuffer basse-rés pour que scanlines/masque reposent sur une couleur
// lisse plutôt que des blocs NEAREST.
float cubicWeight(float x)
{
    x = abs(x);
    if (x < 1.0) return x * x * (1.5 * x - 2.5) + 1.0;
    if (x < 2.0) return x * (x * (-0.5 * x + 2.5) - 4.0) + 2.0;
    return 0.0;
}

vec3 sampleSrc(vec2 uv)
{
    uv = clamp(uv, 0.0, 1.0);
    float mag = max(uOutSize.x / uSrcSize.x, uOutSize.y / uSrcSize.y);
    if (mag <= 1.25)
        return texture(uSrc, uv).rgb;

    vec2 coord = uv * uSrcSize - 0.5;
    vec2 f = fract(coord);
    coord = floor(coord);
    vec3 col = vec3(0.0);
    float wsum = 0.0;
    for (int j = -1; j <= 2; ++j) {
        for (int i = -1; i <= 2; ++i) {
            vec2 offs = vec2(float(i), float(j));
            vec2 samp = (coord + offs + 0.5) / uSrcSize;
            float w = cubicWeight(offs.x - f.x) * cubicWeight(offs.y - f.y);
            col += texture(uSrc, clamp(samp, 0.0, 1.0)).rgb * w;
            wsum += w;
        }
    }
    return col / max(wsum, 1e-4);
}

void main()
{
    // ── Distorsion de baril ───────────────────────────────────────
    vec2 cuv = vUv * 2.0 - 1.0;
    float r2 = dot(cuv, cuv);
    vec2 buv = cuv * (1.0 + uBarrel * r2);
    vec2 uv  = buv * 0.5 + 0.5;
    // Bord anti-aliasé : fond en noir sur un pixel de sortie au bord déformé.
    vec2  edge     = min(uv, 1.0 - uv);
    vec2  edgeFw   = max(fwidth(uv), vec2(1e-4));
    float edgeMask = clamp(min(edge.x / edgeFw.x, edge.y / edgeFw.y), 0.0, 1.0);
    vec3 rgb = sampleSrc(uv);

    // ── Sharpness (unsharp mask / adoucissement, neutre à 0.5) ────
    {
        float amt = (uSharpness - 0.5) * 2.0;   // -1 (doux) .. +1 (net)
        if (amt != 0.0) {
            vec2 t = 1.0 / uSrcSize;
            vec3 blur = (
                sampleSrc(uv + vec2(-t.x, 0.0)) +
                sampleSrc(uv + vec2( t.x, 0.0)) +
                sampleSrc(uv + vec2(0.0, -t.y)) +
                sampleSrc(uv + vec2(0.0,  t.y))) * 0.25;
            rgb = clamp(rgb + amt * (rgb - blur), 0.0, 1.0);
        }
    }

    // ── Rotation de teinte ────────────────────────────────────────
    // RGB→YUV (BT.601), rotation U/V de uHue·π, YUV→RGB (matrice OpenEmulator).
    if (uHue != 0.0) {
        float Y = dot(rgb, vec3( 0.299,    0.587,    0.114));
        float U = dot(rgb, vec3(-0.14713, -0.28886,  0.436));
        float V = dot(rgb, vec3( 0.615,   -0.51499, -0.10001));
        float a  = uHue * 3.14159265;
        float cs = cos(a), sn = sin(a);
        float Ur = U * cs - V * sn;
        float Vr = U * sn + V * cs;
        rgb = vec3(Y                 + 1.139883 * Vr,
                   Y - 0.394642 * Ur - 0.580622 * Vr,
                   Y + 2.032062 * Ur);
    }

    // ── Luminosité / contraste / saturation ───────────────────────
    rgb = (rgb - 0.5) * uContrast + 0.5 + uBrightness;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(luma), rgb, clamp(uSaturation, 0.0, 4.0));
    rgb = clamp(rgb, 0.0, 1.0);

    // ── Courbe de réponse phosphore (gamma CRT) ───────────────────
    if (uPhosphorGamma != 1.0) {
        rgb = pow(max(rgb, vec3(0.0)), vec3(uPhosphorGamma));
    }

    // ── Scanlines (faisceau doux, anti-alias analytique) ──────────
    float outRow = uv.y * (uSrcSize.y * 2.0);
    float rowFw  = max(fwidth(outRow), 1e-4);
    float scanAA = clamp(1.0 - (rowFw - 0.5) / 0.5, 0.0, 1.0); // 1 net → 0 alias
    float beam   = 0.5 + 0.5 * cos(3.14159265 * outRow);       // période 2, doux
    rgb *= 1.0 - uScanlines * (1.0 - beam) * scanAA;

    // ── Shadow mask (procédural, anti-alias analytique) ───────────
    if (uShadowMask != 0 && uShadowStrength > 0.0) {
        float oxBase = uv.x * (uSrcSize.x * 2.0);
        float maskFw   = max(fwidth(oxBase), 1e-4);
        float maskAA   = clamp(1.0 - (maskFw - 1.0) / 2.0, 0.0, 1.0);
        float ox = oxBase;
        if (uShadowMask == 3) {
            ox += (mod(floor(outRow * 0.5), 2.0) < 1.0) ? 0.0 : 1.5;
        }
        float strength = uShadowStrength * maskAA;
        int phase = int(mod(floor(ox), 3.0));
        // Triplet sombre/clair de Lottes : préserve la luma moyenne.
        const float maskDark = 0.5, maskLight = 1.5;
        vec3 mask = vec3(maskDark);
        if      (phase == 0) mask.r = maskLight;
        else if (phase == 1) mask.g = maskLight;
        else                 mask.b = maskLight;
        vec3 atten = mix(vec3(1.0), mask, strength);
        if (uShadowMask == 1 || uShadowMask == 3) {
            float vrow = mod(floor(outRow), 3.0);
            if (vrow < 1.0) atten *= mix(1.0, 0.7, strength);
        }
        rgb *= atten;
    }

    // ── Center lighting / vignette (ordre OpenEmulator : après le masque) ──
    {
        // max() défensif : l'UI borne à 0.5..1.0, mais un neost.cfg chargeant
        // crt_center=0 donnerait 1/0 → inf. On garde une vignette bien définie.
        vec2 lighting = cuv * (1.0 / max(uCenterLighting, 0.01) - 1.0);
        rgb *= exp(-dot(lighting, lighting));
    }

    // ── Luminance gain (post-verre) ───────────────────────────────
    rgb *= uLuminanceGain;

    // ── Persistance (rémanence phosphore) ─────────────────────────
    // Sur la couleur finale corrigée. Le plancher -0.5/256 traîne les rémanences
    // faibles jusqu'au noir en temps fini. `prev` est la sortie masquée de la trame
    // précédente (edgeMask appliqué EN DERNIER, ci-dessous) → elle est déjà nulle
    // hors du cadre courbé, donc la rémanence ne bave pas au-delà du bord baril.
    vec3 prev = texture(uPrev, vUv).rgb;
    rgb = max(rgb, prev * clamp(uPersistence, 0.0, 0.98) - 0.5 / 256.0);

    // Masque de bord appliqué EN TOUT DERNIER : le résultat écrit (= `prev` de la
    // trame suivante) est noir hors du cadre déformé, sans halo de rémanence débordant.
    FRAG_COLOR = vec4(rgb * edgeMask, 1.0);
}
)GLSL";

} // namespace

CrtEffectStack::CrtEffectStack() = default;
CrtEffectStack::~CrtEffectStack() = default;

bool CrtEffectStack::initialize()
{
    if (initialized) return ready;
    initialized = true;

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
    if (!loadEntryPoints()) {
        errorMsg = "GL 3.x entry points unavailable";
        return false;
    }
#endif

#if defined(__APPLE__)
    int glMajor = 0;
    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (glVersion) std::sscanf(glVersion, "%d", &glMajor);
    g_appleLegacyGl = glMajor < 3;
    useVertexArray = !g_appleLegacyGl;
#else
    useVertexArray = true;
#endif

    program = compileShaderProgram(kVertexShader, kFragmentShader, &errorMsg);
    if (!program) return false;

    uSrc         = glGetUniformLocation(program, "uSrc");
    uPrevFrame   = glGetUniformLocation(program, "uPrev");
    uSrcSize     = glGetUniformLocation(program, "uSrcSize");
    uOutSize     = glGetUniformLocation(program, "uOutSize");
    uBrightness  = glGetUniformLocation(program, "uBrightness");
    uContrast    = glGetUniformLocation(program, "uContrast");
    uSaturation  = glGetUniformLocation(program, "uSaturation");
    uHue         = glGetUniformLocation(program, "uHue");
    uSharpness   = glGetUniformLocation(program, "uSharpness");
    uPersistence = glGetUniformLocation(program, "uPersistence");
    uScanlines   = glGetUniformLocation(program, "uScanlines");
    uBarrel      = glGetUniformLocation(program, "uBarrel");
    uShadowMask  = glGetUniformLocation(program, "uShadowMask");
    uShadowStr   = glGetUniformLocation(program, "uShadowStrength");
    uLuminanceGain = glGetUniformLocation(program, "uLuminanceGain");
    uCenterLighting = glGetUniformLocation(program, "uCenterLighting");
    uPhosphorGamma = glGetUniformLocation(program, "uPhosphorGamma");

    const float verts[] = {
        -1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f,
    };
    if (useVertexArray) {
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    if (useVertexArray) glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);   // ne pas laisser le VBO lié (cf. process())

    ready = true;
    if (neost::log::verbose()) std::fprintf(stderr, "[CRT] CRT effect stack ready\n");
    return true;
}

bool CrtEffectStack::createTextures(int w, int h)
{
    // w,h = dims de SORTIE (écran). Rendre la passe à la résolution native de
    // l'écran permet l'anti-alias analytique (fwidth) des scanlines/masque.
    outW = w;
    outH = h;

    glGenFramebuffers(2, fbo);
    glGenTextures(2, outputTex);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, outputTex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, outputTex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            errorMsg = "FBO incomplete";
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(2, fbo);
            glDeleteTextures(2, outputTex);
            fbo[0] = fbo[1] = 0;
            outputTex[0] = outputTex[1] = 0;
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    firstFrame = true;
    return true;
}

unsigned int CrtEffectStack::process(unsigned int srcTex, int srcW, int srcH,
                                     int dstW, int dstH)
{
    if (!ready || srcTex == 0) return 0;

    dstW = std::max(1, dstW);
    dstH = std::max(1, dstH);

    if (outputTex[0] == 0) {
        // Échec d'allocation NON définitif : on repasse simplement en rendu direct
        // pour cette trame (return 0 = passthrough côté appelant) et on retentera
        // dès que la taille demandée change. `ready = false` condamnait l'effet
        // pour toute la session sur un échec transitoire.
        if (dstW == failedW && dstH == failedH) return 0;
        if (!createTextures(dstW, dstH)) { failedW = dstW; failedH = dstH; return 0; }
        failedW = failedH = -1;
    } else if (dstW != outW || dstH != outH) {
        // Fenêtre/zoom changé — redimensionne la paire ping-pong. La réallocation
        // est VÉRIFIÉE (complétude FBO) : sans ce contrôle, un glTexImage2D refusé
        // (FBO énorme après zoom sur GPU limité) laissait un attachement périmé
        // plus petit que le viewport — sortie corrompue sans le moindre message.
        outW = dstW; outH = dstH;
        bool resizeOk = true;
        for (int i = 0; i < 2; ++i) {
            glBindTexture(GL_TEXTURE_2D, outputTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                resizeOk = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!resizeOk) {
            errorMsg = "FBO incomplete after resize";
            glDeleteFramebuffers(2, fbo);
            glDeleteTextures(2, outputTex);
            fbo[0] = fbo[1] = 0;
            outputTex[0] = outputTex[1] = 0;
            outW = outH = 0;
            failedW = dstW; failedH = dstH;      // re-tenté au prochain changement de taille
            return 0;
        }
        firstFrame = true;
    }

    // Sauve l'état GL pour ne pas perturber le rendu ImGui / immediate mode.
    int prevFbo = 0, prevViewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);

    const int writeIdx = pingPongIdx;
    const int readIdx  = 1 - pingPongIdx;
    pingPongIdx = readIdx;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo[writeIdx]);
    glViewport(0, 0, outW, outH);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // La source (écran ST) est uploadée en GL_NEAREST pour le 1:1 intègre sans
    // CRT. Pour la passe on bascule temporairement en LINEAR ; sampleSrc()
    // ajoute le bicubique quand on agrandit davantage.
    GLint prevMinFilter = GL_NEAREST;
    GLint prevMagFilter = GL_NEAREST;
    // Fixer l'unité AVANT le premier bind : sinon ce bind (et les glTexParameteri qui
    // suivent) atterrissent sur l'unité de texture laissée active par l'appelant, que
    // le nettoyage de fin ne restaure pas. Inoffensif tant que les appelants laissent
    // l'unité 0, mais c'est la classe de fuite d'état GL qui a déjà coûté le
    // glUseProgram(0)/glBindBuffer(0) obligatoires plus bas.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &prevMinFilter);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &prevMagFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glUniform1i(uSrc, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, firstFrame ? srcTex : outputTex[readIdx]);
    glUniform1i(uPrevFrame, 1);

    if (uSrcSize     >= 0) glUniform2f(uSrcSize, float(srcW), float(srcH));
    if (uOutSize     >= 0) glUniform2f(uOutSize, float(outW), float(outH));
    if (uBrightness  >= 0) glUniform1f(uBrightness,  params.brightness);
    if (uContrast    >= 0) glUniform1f(uContrast,    params.contrast);
    if (uSaturation  >= 0) glUniform1f(uSaturation,  params.saturation);
    if (uHue         >= 0) glUniform1f(uHue,         params.hue);
    if (uSharpness   >= 0) glUniform1f(uSharpness,   params.sharpness);
    if (uPersistence >= 0) glUniform1f(uPersistence, params.persistence);
    if (uScanlines   >= 0) glUniform1f(uScanlines,   params.scanlines);
    if (uBarrel      >= 0) glUniform1f(uBarrel,      params.barrel);
    if (uShadowMask  >= 0) glUniform1i(uShadowMask,  static_cast<int>(params.shadowMask));
    if (uShadowStr   >= 0) glUniform1f(uShadowStr,   params.shadowMaskStrength);
    if (uLuminanceGain >= 0) glUniform1f(uLuminanceGain, params.luminanceGain);
    if (uCenterLighting >= 0) glUniform1f(uCenterLighting, params.centerLighting);
    if (uPhosphorGamma >= 0) glUniform1f(uPhosphorGamma, params.phosphorGamma);

    if (useVertexArray) {
        glBindVertexArray(vao);
    } else {
        // En OpenGL 2.1 il n'y a pas d'état VAO core : rattacher explicitement
        // l'attribut au VBO avant chaque dessin.
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (useVertexArray) glBindVertexArray(0);

    // CRUCIAL pour NeoST (≠ POM2 core-profile) : l'écran ST et ImGui sont
    // dessinés en pipeline FIXE (immediate mode + imgui_impl_opengl2). Laisser
    // notre programme lié corromprait tous les blits suivants → on revient au
    // pipeline fixe (programme 0).
    glUseProgram(0);
    // Et SURTOUT débinder notre VBO : imgui_impl_opengl2 dessine avec des
    // tableaux de sommets CÔTÉ CLIENT (glVertexPointer…), qui exigent
    // GL_ARRAY_BUFFER = 0. Un VBO resté lié ferait interpréter ses pointeurs
    // comme des offsets → toute l'UI ImGui disparaît (fenêtre vide).
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Restaure le filtre NEAREST de l'appelant (chemin sans CRT).
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, prevMinFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, prevMagFilter);

    // Ne laisse pas nos textures privées liées sur une unité.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(prevFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);

    firstFrame = false;
    return outputTex[writeIdx];
}

} // namespace neost
