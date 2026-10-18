// Headless OpenGL 3.3 backend for the PC test build (EGL surfaceless, Mesa). It compiles the
// original Balatro .fs shaders with a preamble equivalent to LOVE 11.5's, so it serves as the
// reference renderer for screenshot comparisons. The "screen" is an offscreen framebuffer.
#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gfx/backend.h"
#include "platform/platform.h"

namespace vl::gfx {
namespace {

int g_w = 960, g_h = 544;
EGLDisplay g_dpy;
EGLContext g_ctx;
GLuint g_vao, g_vbo, g_ibo;
GLuint g_screen_fbo, g_screen_tex;

struct GLTex { GLuint id; };
struct GLCanvas { GLuint fbo; };

struct Program {
    GLuint prog = 0;
    GLint loc_proj = -1, loc_screen = -1;
    std::vector<GLint> locs;  // per uniform desc
    float cache[kMaxUniformFloats];
    bool cache_valid = false;
};
Program g_progs[(int)ShaderId::Count];
Program g_clear_prog;

const char *kSyntax = R"(
#define number float
#define Image sampler2D
#define extern uniform
#define LOVE_HIGHP_OR_MEDIUMP highp
#define MY_HIGHP_OR_MEDIUMP highp
uniform highp vec4 love_ScreenSize;
)";

const char *kPixelFuncs = R"(
vec4 Texel(sampler2D s, vec2 uv) { return texture(s, uv); }
#define love_PixelCoord (vec2(gl_FragCoord.x, (gl_FragCoord.y * love_ScreenSize.z) + love_ScreenSize.w))
)";

const char *kVertexMain = R"(
layout(location = 0) in vec4 VertexPosition;
layout(location = 1) in vec4 VertexTexCoord;
layout(location = 2) in vec4 VertexColor;
out vec4 VaryingTexCoord;
out vec4 VaryingColor;
uniform mat4 vl_Proj;
vec4 position(mat4 clipSpaceFromLocal, vec4 localPosition);
void main() {
    VaryingTexCoord = VertexTexCoord;
    VaryingColor = VertexColor;
    gl_Position = position(vl_Proj, VertexPosition);
}
)";

const char *kVertexDefaultPosition = R"(
vec4 position(mat4 m, vec4 p) { return m * p; }
)";

const char *kPixelMain = R"(
in highp vec4 VaryingTexCoord;
in mediump vec4 VaryingColor;
out vec4 love_PixelColor;
uniform sampler2D MainTex;
vec4 effect(vec4 vcolor, Image tex, vec2 texcoord, vec2 pixcoord);
void main() {
    love_PixelColor = effect(VaryingColor, MainTex, VaryingTexCoord.st, love_PixelCoord);
}
)";

const char *kDefaultEffect = R"(
vec4 effect(vec4 vcolor, Image tex, vec2 texcoord, vec2 pixcoord) {
    return Texel(tex, texcoord) * vcolor;
}
)";

GLuint compile(GLenum type, const std::string &src, const char *name) {
    GLuint s = glCreateShader(type);
    const char *p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::fprintf(stderr, "[gl] %s %s shader failed:\n%s\n", name,
                     type == GL_VERTEX_SHADER ? "vertex" : "pixel", log);
        std::exit(1);
    }
    return s;
}

void build_program(Program &P, ShaderId id, const std::string &body, const char *name) {
    bool has_position = body.find("vec4 position(") != std::string::npos;
    std::string vs = std::string("#version 330 core\n#define VERTEX 1\n") + kSyntax +
                     "vec4 Texel(sampler2D s, vec2 uv) { return texture(s, uv); }\n" + body +
                     (has_position ? "" : kVertexDefaultPosition) + kVertexMain;
    // The .fs files are written to compile as both stages (LOVE does the same).
    bool has_effect = body.find("vec4 effect(") != std::string::npos;
    std::string fs = std::string("#version 330 core\n#define PIXEL 1\n") + kSyntax + kPixelFuncs +
                     body + (has_effect ? "" : kDefaultEffect) + kPixelMain;
    GLuint v = compile(GL_VERTEX_SHADER, vs, name), f = compile(GL_FRAGMENT_SHADER, fs, name);
    P.prog = glCreateProgram();
    glAttachShader(P.prog, v);
    glAttachShader(P.prog, f);
    glLinkProgram(P.prog);
    GLint ok = 0;
    glGetProgramiv(P.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(P.prog, sizeof log, nullptr, log);
        std::fprintf(stderr, "[gl] link %s failed: %s\n", name, log);
        std::exit(1);
    }
    P.loc_proj = glGetUniformLocation(P.prog, "vl_Proj");
    P.loc_screen = glGetUniformLocation(P.prog, "love_ScreenSize");
    glUseProgram(P.prog);
    glUniform1i(glGetUniformLocation(P.prog, "MainTex"), 0);
    glUniform1i(glGetUniformLocation(P.prog, "vl_Aux1"), 1);
    glUniform1i(glGetUniformLocation(P.prog, "vl_Aux2"), 2);
    if (id != ShaderId::Count) {
        const ShaderInfo &si = shader_info(id);
        for (int i = 0; i < si.uniform_count; i++)
            P.locs.push_back(glGetUniformLocation(P.prog, si.uniforms[i].name));
    }
}

void ortho(float *m, float w, float h, bool flip_y) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / w;
    m[5] = flip_y ? 2.0f / h : -2.0f / h;
    m[10] = -1;
    m[12] = -1;
    m[13] = flip_y ? -1 : 1;
    m[15] = 1;
}

}  // namespace

bool backend_init() {
    g_dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (!g_dpy || !eglInitialize(g_dpy, nullptr, nullptr)) {
        std::fprintf(stderr, "[gl] EGL init failed\n");
        return false;
    }
    eglBindAPI(EGL_OPENGL_API);
    const EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    eglChooseConfig(g_dpy, cfg_attr, &cfg, 1, &n);
    const EGLint ctx_attr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                               EGL_CONTEXT_OPENGL_PROFILE_MASK,
                               EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctx_attr);
    if (!g_ctx || !eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx)) {
        std::fprintf(stderr, "[gl] context failed\n");
        return false;
    }

    glGenTextures(1, &g_screen_tex);
    glBindTexture(GL_TEXTURE_2D, g_screen_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_w, g_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &g_screen_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, g_screen_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_screen_tex, 0);

    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ibo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    for (int i = 0; i < (int)ShaderId::Count; i++) {
        ShaderId id = (ShaderId)i;
        std::string body;
        if (id == ShaderId::Default) {
            body = kDefaultEffect;
        } else {
            std::string path = std::string("shaders/") + shader_info(id).name + ".fs";
            if (!platform::read_file(path.c_str(), body)) {
                std::fprintf(stderr, "[gl] missing %s\n", path.c_str());
                return false;
            }
        }
        build_program(g_progs[i], id, body, shader_info(id).name);
    }
    build_program(g_clear_prog, ShaderId::Count,
                  "uniform vec4 vl_Clear;\nvec4 effect(vec4 c, Image t, vec2 tc, vec2 sc) { return vl_Clear; }\n",
                  "clear");

    glEnable(GL_BLEND);
    // love 'alpha' blend mode with 'alphamultiply'
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    return true;
}

void backend_set_vsync(bool) {}
int backend_screen_width() { return g_w; }
int backend_screen_height() { return g_h; }

void backend_create_texture(Texture *t, const TextureDesc &d) {
    GLTex *gt = new GLTex();
    glGenTextures(1, &gt->id);
    glBindTexture(GL_TEXTURE_2D, gt->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, d.width, d.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 d.rgba);
    GLenum mag = d.filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
    GLenum min = mag;
    if (d.mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        min = d.filter == Filter::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
    GLenum wrap = d.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    t->mip_levels = d.mipmaps ? 2 : 1;
    t->impl = gt;
}

void backend_destroy_texture(Texture *t) {
    GLTex *gt = (GLTex *)t->impl;
    glDeleteTextures(1, &gt->id);
    delete gt;
}

void backend_create_canvas(Canvas *c) {
    GLTex *gt = new GLTex();
    glGenTextures(1, &gt->id);
    glBindTexture(GL_TEXTURE_2D, gt->id);
    bool f16 = c->format == CanvasFormat::RGBA16F;
    glTexImage2D(GL_TEXTURE_2D, 0, f16 ? GL_RGBA16F : GL_RGBA8, c->w, c->h, 0, GL_RGBA,
                 f16 ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    GLenum filt = c->filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    c->tex.impl = gt;
    GLCanvas *gc = new GLCanvas();
    glGenFramebuffers(1, &gc->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, gc->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gt->id, 0);
    c->impl = gc;
}

void backend_submit(const FrameData &f) {
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, f.vertices.size() * sizeof(Vertex), f.vertices.data(),
                 GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, f.indices.size() * sizeof(uint16_t), f.indices.data(),
                 GL_STREAM_DRAW);

    float proj[16];
    float screen[4];
    int tw = g_w, th = g_h;
    for (Program &p : g_progs) p.cache_valid = false;

    for (const Cmd &c : f.cmds) {
        if (c.type == CmdType::Target) {
            if (c.target) {
                glBindFramebuffer(GL_FRAMEBUFFER, ((GLCanvas *)c.target->impl)->fbo);
                tw = c.target->w;
                th = c.target->h;
                // Canvases: y is flipped in the projection so texture row 0 is the top.
                ortho(proj, (float)tw, (float)th, true);
                screen[0] = (float)tw; screen[1] = (float)th; screen[2] = 1; screen[3] = 0;
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, g_screen_fbo);
                tw = g_w;
                th = g_h;
                // Offscreen stand-in for the window: same orientation as a canvas, and
                // pixel coords top-left based like LOVE's backbuffer.
                ortho(proj, (float)tw, (float)th, true);
                screen[0] = (float)tw; screen[1] = (float)th; screen[2] = 1; screen[3] = 0;
            }
            glViewport(0, 0, tw, th);
            continue;
        }
        if (c.type == CmdType::Clear) {
            glClearColor(c.clear_color.r, c.clear_color.g, c.clear_color.b, c.clear_color.a);
            glClear(GL_COLOR_BUFFER_BIT);
            continue;
        }
        Program &P = g_progs[(int)c.shader];
        glUseProgram(P.prog);
        if (shader_info(c.shader).blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glUniformMatrix4fv(P.loc_proj, 1, GL_FALSE, proj);
        glUniform4fv(P.loc_screen, 1, screen);
        const Texture *t = c.texture;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ((GLTex *)t->impl)->id);
        for (int a = 0; a < 2; a++)
            if (c.aux[a]) {
                glActiveTexture(GL_TEXTURE1 + a);
                glBindTexture(GL_TEXTURE_2D, ((GLTex *)c.aux[a]->impl)->id);
                glActiveTexture(GL_TEXTURE0);
            }
        const ShaderInfo &si = shader_info(c.shader);
        const float *u = &f.uniforms[c.uniforms];
        if (!P.cache_valid || std::memcmp(P.cache, u, si.block_floats * sizeof(float)) != 0) {
            for (int i = 0; i < si.uniform_count; i++) {
                GLint loc = P.locs[i];
                if (loc < 0) continue;
                const float *v = u + si.uniforms[i].offset;
                switch (si.uniforms[i].components) {
                case 1: glUniform1f(loc, v[0]); break;
                case 2: glUniform2fv(loc, 1, v); break;
                case 3: glUniform3fv(loc, 1, v); break;
                case 4: glUniform4fv(loc, 1, v); break;
                }
            }
            std::memcpy(P.cache, u, si.block_floats * sizeof(float));
            P.cache_valid = true;
        }
        size_t vo = c.first_vertex * sizeof(Vertex);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(vo + 0));
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)(vo + 8));
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void *)(vo + 16));
        glDrawElements(GL_TRIANGLES, (GLsizei)c.index_count, GL_UNSIGNED_SHORT,
                       (void *)(c.first_index * sizeof(uint16_t)));
    }
    glFinish();
}

// Reads the offscreen "screen" (top-left origin RGBA rows).
void backend_read_screen(std::vector<uint8_t> &out) {
    out.resize((size_t)g_w * g_h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, g_screen_fbo);
    glReadPixels(0, 0, g_w, g_h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    // rows were rendered y-flipped (row 0 = top) so memory order is already top-down
}

}  // namespace vl::gfx
