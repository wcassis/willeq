// COGLES2Shaders.cpp — Built-in GLSL ES 1.0 shader programs for COpenGLES2Driver

#include "COGLES2Shaders.h"
#include "os.h"
#include <cstdio>
#include <cstring>

namespace irr
{
namespace video
{

// ============================================================================
// Shader source: Color2D (Phase 1) — solid color 2D rectangles, lines
// ============================================================================

static const char* COLOR2D_VS = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec4 aColor;
uniform mat4 mWorldViewProj;
varying vec4 vColor;
void main() {
    gl_Position = mWorldViewProj * vec4(aPosition, 1.0);
    vColor = aColor;
}
)";

static const char* COLOR2D_FS = R"(
precision mediump float;
varying vec4 vColor;
void main() {
    gl_FragColor = vColor;
}
)";

// ============================================================================
// Shader source: UI2D (Phase 2) — textured quads with color modulation
// ============================================================================

static const char* UI2D_VS = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec4 aColor;
attribute vec2 aTexCoord0;
uniform mat4 mWorldViewProj;
varying vec4 vColor;
varying vec2 vTexCoord;
void main() {
    gl_Position = mWorldViewProj * vec4(aPosition, 1.0);
    vColor = aColor;
    vTexCoord = aTexCoord0;
}
)";

static const char* UI2D_FS = R"(
precision mediump float;
uniform sampler2D uTexture;
varying vec4 vColor;
varying vec2 vTexCoord;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    gl_FragColor = texColor * vColor;
}
)";

// ============================================================================
// Shader source: Solid3D (Phase 3) — per-vertex lighting + fog
// ============================================================================

static const char* SOLID3D_VS = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;

uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec3 uLightAtten[8];

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos4 = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos4;
    vTexCoord = aTexCoord0;

    vec3 worldPos = (mWorld * pos4).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    // Directional sun light
    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    // Point lights (always iterate all 8)
    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        lighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(lighting * uTintColor, 1.0) * aColor;

    // Linear fog factor
    float fogDist = length((mWorldViewProj * pos4).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

static const char* SOLID3D_FS = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// Shader source: AlphaTest3D (Phase 3) — same as Solid3D + discard
// ============================================================================

static const char* ALPHA3D_FS = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    vec4 texColor = texture2D(uTexture, vTexCoord);
    if (texColor.a < 0.5) discard;
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// Shader source: AtlasSolid3D (Phase 4) — atlas UV from texcoord1
// ============================================================================

static const char* ATLAS_SOLID3D_VS = R"(
precision highp float;
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec4 aColor;
attribute vec2 aTexCoord0;
attribute vec2 aTexCoord1;

uniform mat4 mWorldViewProj;
uniform mat4 mWorld;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uTintColor;
uniform float uFogStart;
uniform float uFogEnd;

uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform vec3 uLightAtten[8];

varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;

void main() {
    vec4 pos4 = vec4(aPosition, 1.0);
    gl_Position = mWorldViewProj * pos4;

    // Pass precomputed atlas UV through varying
    vTexCoord = aTexCoord1;

    vec3 worldPos = (mWorld * pos4).xyz;
    vec3 worldN = normalize((mWorld * vec4(aNormal, 0.0)).xyz);

    vec3 sunL = normalize(-uSunDir);
    float sunNdotL = max(dot(worldN, sunL), 0.0);
    vec3 lighting = uAmbientColor + sunNdotL * uSunColor;

    for (int i = 0; i < 8; i++) {
        vec3 lVec = uLightPos[i] - worldPos;
        float d = length(lVec) + 0.001;
        float atten = 1.0 / (uLightAtten[i].x
                            + uLightAtten[i].y * d
                            + uLightAtten[i].z * d * d + 0.0001);
        float nl = max(dot(worldN, normalize(lVec)), 0.0);
        lighting += uLightColor[i] * nl * atten;
    }

    vColor = vec4(lighting * uTintColor, 1.0);

    float fogDist = length((mWorldViewProj * pos4).xyz);
    vFogFactor = clamp((uFogEnd - fogDist) / (uFogEnd - uFogStart), 0.0, 1.0);
}
)";

// AtlasSolid3D uses same FS as Solid3D (SOLID3D_FS)

// ============================================================================
// Shader source: AtlasAlpha3D (Phase 4) — dual sample RGB + alpha pages
// ============================================================================

static const char* ATLAS_ALPHA3D_FS = R"(
precision mediump float;
uniform sampler2D uTexture;
uniform sampler2D uAlphaTexture;
uniform vec4 uFogColor;
varying vec4 vColor;
varying vec2 vTexCoord;
varying float vFogFactor;
void main() {
    float alpha = texture2D(uAlphaTexture, vTexCoord).r;
    if (alpha < 0.5) discard;
    vec4 texColor = texture2D(uTexture, vTexCoord);
    vec4 lit = texColor * vColor;
    gl_FragColor = mix(uFogColor, lit, vFogFactor);
}
)";

// ============================================================================
// COGLES2ShaderManager implementation
// ============================================================================

COGLES2ShaderManager::COGLES2ShaderManager()
    : activeProgram_(EOGLES2SP_COLOR2D)
{
    memset(programs_, 0, sizeof(programs_));
    for (int i = 0; i < EOGLES2SP_COUNT; i++)
        uniforms_[i].invalidate();
}

COGLES2ShaderManager::~COGLES2ShaderManager()
{
    for (int i = 0; i < EOGLES2SP_COUNT; i++) {
        if (programs_[i])
            glDeleteProgram(programs_[i]);
    }
}

GLuint COGLES2ShaderManager::compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    if (!shader)
        return 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 0) {
            char log[512];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            char msg[640];
            snprintf(msg, sizeof(msg), "GLES2 shader compile error: %s", log);
            os::Printer::log(msg, ELL_ERROR);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint COGLES2ShaderManager::linkProgram(GLuint vs, GLuint fs)
{
    GLuint prog = glCreateProgram();
    if (!prog)
        return 0;

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    // Bind fixed vertex attribute locations before linking
    glBindAttribLocation(prog, EOGLES2VA_POSITION, "aPosition");
    glBindAttribLocation(prog, EOGLES2VA_NORMAL, "aNormal");
    glBindAttribLocation(prog, EOGLES2VA_COLOR, "aColor");
    glBindAttribLocation(prog, EOGLES2VA_TEXCOORD0, "aTexCoord0");
    glBindAttribLocation(prog, EOGLES2VA_TEXCOORD1, "aTexCoord1");

    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 0) {
            char log[512];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            char msg[640];
            snprintf(msg, sizeof(msg), "GLES2 program link error: %s", log);
            os::Printer::log(msg, ELL_ERROR);
        }
        glDeleteProgram(prog);
        return 0;
    }

    // Detach shaders (program retains them)
    glDetachShader(prog, vs);
    glDetachShader(prog, fs);

    return prog;
}

GLuint COGLES2ShaderManager::buildProgram(const char* vsSrc, const char* fsSrc)
{
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    if (!vs)
        return 0;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = linkProgram(vs, fs);

    // Shaders can be deleted after linking (program retains them)
    glDeleteShader(vs);
    glDeleteShader(fs);

    return prog;
}

void COGLES2ShaderManager::cacheUniforms(EOGLES2ShaderProgram prog)
{
    GLuint p = programs_[prog];
    SOGLES2ProgramUniforms& u = uniforms_[prog];
    u.invalidate();

    u.mWorldViewProj = glGetUniformLocation(p, "mWorldViewProj");
    u.mWorld = glGetUniformLocation(p, "mWorld");
    u.uSunDir = glGetUniformLocation(p, "uSunDir");
    u.uSunColor = glGetUniformLocation(p, "uSunColor");
    u.uAmbientColor = glGetUniformLocation(p, "uAmbientColor");
    u.uTintColor = glGetUniformLocation(p, "uTintColor");
    u.uFogStart = glGetUniformLocation(p, "uFogStart");
    u.uFogEnd = glGetUniformLocation(p, "uFogEnd");
    u.uLightPos = glGetUniformLocation(p, "uLightPos[0]");
    u.uLightColor = glGetUniformLocation(p, "uLightColor[0]");
    u.uLightAtten = glGetUniformLocation(p, "uLightAtten[0]");
    u.uFogColor = glGetUniformLocation(p, "uFogColor");
    u.uTexture = glGetUniformLocation(p, "uTexture");
    u.uAlphaTexture = glGetUniformLocation(p, "uAlphaTexture");
    u.uColor = glGetUniformLocation(p, "uColor");
}

bool COGLES2ShaderManager::init()
{
    // Phase 1: Color2D (required for loading screen)
    programs_[EOGLES2SP_COLOR2D] = buildProgram(COLOR2D_VS, COLOR2D_FS);
    if (programs_[EOGLES2SP_COLOR2D]) {
        cacheUniforms(EOGLES2SP_COLOR2D);
        os::Printer::log("GLES2: Color2D shader compiled", ELL_INFORMATION);
    } else {
        os::Printer::log("GLES2: Color2D shader FAILED", ELL_ERROR);
        return false;
    }

    // Phase 2: UI2D (textured 2D)
    programs_[EOGLES2SP_UI2D] = buildProgram(UI2D_VS, UI2D_FS);
    if (programs_[EOGLES2SP_UI2D]) {
        cacheUniforms(EOGLES2SP_UI2D);
        os::Printer::log("GLES2: UI2D shader compiled", ELL_INFORMATION);
    } else {
        os::Printer::log("GLES2: UI2D shader FAILED (2D texturing disabled)", ELL_WARNING);
    }

    // Phase 3: Solid3D and AlphaTest3D
    programs_[EOGLES2SP_SOLID3D] = buildProgram(SOLID3D_VS, SOLID3D_FS);
    if (programs_[EOGLES2SP_SOLID3D]) {
        cacheUniforms(EOGLES2SP_SOLID3D);
        os::Printer::log("GLES2: Solid3D shader compiled", ELL_INFORMATION);
    } else {
        os::Printer::log("GLES2: Solid3D shader FAILED (3D rendering disabled)", ELL_WARNING);
    }

    programs_[EOGLES2SP_ALPHA3D] = buildProgram(SOLID3D_VS, ALPHA3D_FS);
    if (programs_[EOGLES2SP_ALPHA3D]) {
        cacheUniforms(EOGLES2SP_ALPHA3D);
        os::Printer::log("GLES2: AlphaTest3D shader compiled", ELL_INFORMATION);
    } else {
        os::Printer::log("GLES2: AlphaTest3D shader FAILED", ELL_WARNING);
    }

    // Phase 4: Atlas shaders
    programs_[EOGLES2SP_ATLAS_SOLID3D] = buildProgram(ATLAS_SOLID3D_VS, SOLID3D_FS);
    if (programs_[EOGLES2SP_ATLAS_SOLID3D]) {
        cacheUniforms(EOGLES2SP_ATLAS_SOLID3D);
        os::Printer::log("GLES2: AtlasSolid3D shader compiled", ELL_INFORMATION);
    } else {
        os::Printer::log("GLES2: AtlasSolid3D shader FAILED", ELL_WARNING);
    }

    programs_[EOGLES2SP_ATLAS_ALPHA3D] = buildProgram(ATLAS_SOLID3D_VS, ATLAS_ALPHA3D_FS);
    if (programs_[EOGLES2SP_ATLAS_ALPHA3D]) {
        cacheUniforms(EOGLES2SP_ATLAS_ALPHA3D);
        os::Printer::log("GLES2: AtlasAlpha3D shader compiled", ELL_INFORMATION);
    } else {
        os::Printer::log("GLES2: AtlasAlpha3D shader FAILED", ELL_WARNING);
    }

    // Color2D is required
    return programs_[EOGLES2SP_COLOR2D] != 0;
}

bool COGLES2ShaderManager::useProgram(EOGLES2ShaderProgram prog)
{
    if (prog >= EOGLES2SP_COUNT || !programs_[prog])
        return false;

    if (activeProgram_ != prog || true) {  // Always bind — cheap on GLES2
        glUseProgram(programs_[prog]);
        activeProgram_ = prog;
    }
    return true;
}

} // end namespace video
} // end namespace irr
