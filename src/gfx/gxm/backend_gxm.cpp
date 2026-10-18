// Native sceGxm backend for PS Vita. Shaders are Cg programs precompiled to .gxp with psp2cgc and
// loaded from app0:shaders/. Per-frame vertex/index data lives in a ring of frame arenas guarded
// by GPU notifications, so the CPU never stalls on the GPU unless it is a full frame ahead.
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "gfx/backend.h"
#include "platform/platform.h"

namespace vl::gfx {
namespace {

constexpr int kW = 960, kH = 544, kStride = 960;
constexpr int kDisplayBuffers = 3;
constexpr int kMaxPendingSwaps = 2;
constexpr int kFrameArenas = 3;
constexpr size_t kArenaVertexBytes = 4 * 1024 * 1024;
constexpr size_t kArenaIndexBytes = 1 * 1024 * 1024;
constexpr int kTexRowAlign = 16;

#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

// ---------------------------------------------------------------------------- memory
void *gpu_alloc(SceKernelMemBlockType type, size_t size, size_t align, unsigned attribs, SceUID *uid) {
    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW)
        size = ALIGN(size, 256 * 1024);
    else
        size = ALIGN(size, 4 * 1024);
    (void)align;
    *uid = sceKernelAllocMemBlock("vl_gpu", type, size, nullptr);
    if (*uid < 0) return nullptr;
    void *mem = nullptr;
    sceKernelGetMemBlockBase(*uid, &mem);
    if (sceGxmMapMemory(mem, size, (SceGxmMemoryAttribFlags)attribs) < 0) {
        sceKernelFreeMemBlock(*uid);
        return nullptr;
    }
    return mem;
}

void gpu_free(SceUID uid) {
    void *mem = nullptr;
    sceKernelGetMemBlockBase(uid, &mem);
    sceGxmUnmapMemory(mem);
    sceKernelFreeMemBlock(uid);
}

void *usse_alloc(bool fragment, size_t size, unsigned *offset, SceUID *uid) {
    size = ALIGN(size, 4096);
    *uid = sceKernelAllocMemBlock("vl_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, nullptr);
    void *mem = nullptr;
    sceKernelGetMemBlockBase(*uid, &mem);
    if (fragment)
        sceGxmMapFragmentUsseMemory(mem, size, offset);
    else
        sceGxmMapVertexUsseMemory(mem, size, offset);
    return mem;
}

void *patcher_host_alloc(void *, unsigned size) { return std::malloc(size); }
void patcher_host_free(void *, void *mem) { std::free(mem); }

// ---------------------------------------------------------------------------- state
struct DisplayData { void *address; };
volatile bool g_vsync = true;

void display_callback(const void *cb_data) {
    const DisplayData *d = (const DisplayData *)cb_data;
    SceDisplayFrameBuf fb{};
    fb.size = sizeof(fb);
    fb.base = d->address;
    fb.pitch = kStride;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width = kW;
    fb.height = kH;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (g_vsync) sceDisplayWaitVblankStart();
}

struct Target {  // a sceGxm render target + depth surface shared by all surfaces of one size
    SceGxmRenderTarget *rt;
    SceGxmDepthStencilSurface depth;
    SceUID depth_uid;
};

struct GxmTex {
    SceGxmTexture tex;
    SceUID uid;
};

struct GxmCanvas {
    SceGxmColorSurface surface;
    SceUID uid;
    void *mem;
};

struct ProgramParams {
    const SceGxmProgramParameter *u_vert[16];  // per uniform desc, vertex stage
    const SceGxmProgramParameter *u_frag[16];  // per uniform desc, fragment stage
    const SceGxmProgramParameter *proj, *screen_v, *screen_f, *clear;
    int tex_unit, aux_unit[kAuxSlots];  // sampler resource indices, -1 if unused
};

struct Program {
    SceGxmShaderPatcherId vid, fid;
    SceGxmVertexProgram *vp;
    SceGxmFragmentProgram *fp;
    ProgramParams p;
};

SceGxmContext *g_ctx;
SceGxmShaderPatcher *g_patcher;
void *g_ctx_host;
SceUID g_vdm_uid, g_vert_ring_uid, g_frag_ring_uid, g_frag_usse_uid;
SceUID g_patcher_buf_uid, g_patcher_vusse_uid, g_patcher_fusse_uid;

void *g_disp_mem[kDisplayBuffers];
SceUID g_disp_uid[kDisplayBuffers];
SceGxmColorSurface g_disp_surf[kDisplayBuffers];
SceGxmSyncObject *g_disp_sync[kDisplayBuffers];
int g_back = 0, g_front = 0;

std::map<uint32_t, Target> g_targets;
Program g_progs[(int)ShaderId::Count];
Program g_clear;

struct Arena {
    void *vtx;
    void *idx;
    SceUID vtx_uid, idx_uid;
    SceGxmNotification notif;  // signalled when the GPU finished the frame that used it
    bool pending;
};
Arena g_arenas[kFrameArenas];
int g_arena = 0;
volatile unsigned int *g_notif_region;

Target &target_for(int w, int h) {
    uint32_t key = ((uint32_t)w << 16) | (uint32_t)h;
    auto it = g_targets.find(key);
    if (it != g_targets.end()) return it->second;
    Target t{};
    SceGxmRenderTargetParams p{};
    p.flags = 0;
    p.width = w;
    p.height = h;
    p.scenesPerFrame = 4;
    p.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    p.multisampleLocations = 0;
    p.driverMemBlock = -1;
    sceGxmCreateRenderTarget(&p, &t.rt);
    unsigned dw = ALIGN(w, SCE_GXM_TILE_SIZEX), dh = ALIGN(h, SCE_GXM_TILE_SIZEY);
    void *dmem = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * dw * dh,
                           SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT,
                           SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &t.depth_uid);
    sceGxmDepthStencilSurfaceInit(&t.depth, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
                                  SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, dw, dmem, nullptr);
    return g_targets[key] = t;
}

const SceGxmProgram *load_gxp(const char *name, SceGxmShaderPatcherId *id) {
    std::string data;
    std::string path = std::string("shaders/") + name + ".gxp";
    if (!platform::read_file(path.c_str(), data)) {
        platform::fatal("missing shader %s", path.c_str());
    }
    // the patcher keeps a reference: the blob must outlive it
    void *blob = std::malloc(data.size());
    std::memcpy(blob, data.data(), data.size());
    const SceGxmProgram *prog = (const SceGxmProgram *)blob;
    if (sceGxmProgramCheck(prog) < 0) platform::fatal("bad gxp %s", name);
    sceGxmShaderPatcherRegisterProgram(g_patcher, prog, id);
    return prog;
}

void build_program(Program &P, const char *vname, const char *fname, ShaderId id, bool blend,
                   bool f16 = false) {
    const SceGxmProgram *vg = load_gxp(vname, &P.vid);
    const SceGxmProgram *fg = load_gxp(fname, &P.fid);

    const SceGxmProgramParameter *a_pos = sceGxmProgramFindParameterByName(vg, "aPosition");
    const SceGxmProgramParameter *a_tex = sceGxmProgramFindParameterByName(vg, "aTexCoord");
    const SceGxmProgramParameter *a_col = sceGxmProgramFindParameterByName(vg, "aColor");
    SceGxmVertexAttribute attrs[3];
    int na = 0;
    if (a_pos) attrs[na++] = {0, 0, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2,
                              (unsigned short)sceGxmProgramParameterGetResourceIndex(a_pos)};
    if (a_tex) attrs[na++] = {0, 8, SCE_GXM_ATTRIBUTE_FORMAT_F32, 2,
                              (unsigned short)sceGxmProgramParameterGetResourceIndex(a_tex)};
    if (a_col) attrs[na++] = {0, 16, SCE_GXM_ATTRIBUTE_FORMAT_U8N, 4,
                              (unsigned short)sceGxmProgramParameterGetResourceIndex(a_col)};
    SceGxmVertexStream stream{sizeof(Vertex), SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    if (sceGxmShaderPatcherCreateVertexProgram(g_patcher, P.vid, attrs, na, &stream, 1, &P.vp) < 0)
        platform::fatal("vertex program %s", vname);

    SceGxmBlendInfo bi{};
    bi.colorMask = SCE_GXM_COLOR_MASK_ALL;
    bi.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    bi.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    bi.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    bi.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    bi.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
    bi.alphaDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (sceGxmShaderPatcherCreateFragmentProgram(
            g_patcher, P.fid, f16 ? SCE_GXM_OUTPUT_REGISTER_FORMAT_HALF4 : SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
            SCE_GXM_MULTISAMPLE_NONE, blend ? &bi : nullptr, vg, &P.fp) < 0)
        platform::fatal("fragment program %s", fname);

    std::memset(&P.p, 0, sizeof P.p);
    P.p.proj = sceGxmProgramFindParameterByName(vg, "vl_Proj");
    P.p.screen_v = sceGxmProgramFindParameterByName(vg, "love_ScreenSize");
    P.p.screen_f = sceGxmProgramFindParameterByName(fg, "love_ScreenSize");
    P.p.clear = sceGxmProgramFindParameterByName(fg, "vl_Clear");
    auto sampler = [&](const char *n) {
        const SceGxmProgramParameter *s = sceGxmProgramFindParameterByName(fg, n);
        return s ? (int)sceGxmProgramParameterGetResourceIndex(s) : -1;
    };
    P.p.tex_unit = sampler("MainTex");
    P.p.aux_unit[0] = sampler("vl_Aux1");
    P.p.aux_unit[1] = sampler("vl_Aux2");
    P.p.aux_unit[2] = sampler("vl_Aux3");
    if (id != ShaderId::Count) {
        const ShaderInfo &si = shader_info(id);
        for (int i = 0; i < si.uniform_count && i < 16; i++) {
            P.p.u_vert[i] = sceGxmProgramFindParameterByName(vg, si.uniforms[i].name);
            P.p.u_frag[i] = sceGxmProgramFindParameterByName(fg, si.uniforms[i].name);
        }
    }
}

void ortho(float *m, float w, float h) {
    // row-vector convention: shaders do mul(float4(pos,0,1), vl_Proj)
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / w;
    m[5] = -2.0f / h;
    m[10] = -1;
    m[12] = -1;
    m[13] = 1;
    m[15] = 1;
}

}  // namespace

bool backend_init() {
    SceGxmInitializeParams ip{};
    ip.flags = 0;
    ip.displayQueueMaxPendingCount = kMaxPendingSwaps;
    ip.displayQueueCallback = display_callback;
    ip.displayQueueCallbackDataSize = sizeof(DisplayData);
    ip.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if (sceGxmInitialize(&ip) < 0) return false;

    void *vdm = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE,
                          4, SCE_GXM_MEMORY_ATTRIB_READ, &g_vdm_uid);
    const size_t vring_size = 4 * SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    const size_t fring_size = 4 * SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    void *vring = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, vring_size, 4,
                            SCE_GXM_MEMORY_ATTRIB_READ, &g_vert_ring_uid);
    void *fring = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, fring_size, 4,
                            SCE_GXM_MEMORY_ATTRIB_READ, &g_frag_ring_uid);
    unsigned fusse_off;
    void *fusse = usse_alloc(true, SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &fusse_off,
                             &g_frag_usse_uid);

    g_ctx_host = std::malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    SceGxmContextParams cp{};
    cp.hostMem = g_ctx_host;
    cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = vdm;
    cp.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem = vring;
    cp.vertexRingBufferMemSize = vring_size;
    cp.fragmentRingBufferMem = fring;
    cp.fragmentRingBufferMemSize = fring_size;
    cp.fragmentUsseRingBufferMem = fusse;
    cp.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset = fusse_off;
    if (sceGxmCreateContext(&cp, &g_ctx) < 0) return false;

    for (int i = 0; i < kDisplayBuffers; i++) {
        g_disp_mem[i] = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * kStride * kH,
                                  SCE_GXM_COLOR_SURFACE_ALIGNMENT,
                                  SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &g_disp_uid[i]);
        std::memset(g_disp_mem[i], 0, 4 * kStride * kH);
        sceGxmColorSurfaceInit(&g_disp_surf[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8,
                               SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                               SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, kW, kH, kStride, g_disp_mem[i]);
        sceGxmSyncObjectCreate(&g_disp_sync[i]);
    }

    const unsigned patcher_buf = 256 * 1024, patcher_vusse = 256 * 1024, patcher_fusse = 512 * 1024;
    void *pbuf = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, patcher_buf, 4,
                           SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &g_patcher_buf_uid);
    unsigned vusse_off, fusse_off2;
    void *pvusse = usse_alloc(false, patcher_vusse, &vusse_off, &g_patcher_vusse_uid);
    void *pfusse = usse_alloc(true, patcher_fusse, &fusse_off2, &g_patcher_fusse_uid);
    SceGxmShaderPatcherParams pp{};
    pp.userData = nullptr;
    pp.hostAllocCallback = patcher_host_alloc;
    pp.hostFreeCallback = patcher_host_free;
    pp.bufferMem = pbuf;
    pp.bufferMemSize = patcher_buf;
    pp.vertexUsseMem = pvusse;
    pp.vertexUsseMemSize = patcher_vusse;
    pp.vertexUsseOffset = vusse_off;
    pp.fragmentUsseMem = pfusse;
    pp.fragmentUsseMemSize = patcher_fusse;
    pp.fragmentUsseOffset = fusse_off2;
    if (sceGxmShaderPatcherCreate(&pp, &g_patcher) < 0) return false;

    // Each Balatro shader compiles to <name>_v.gxp / <name>_f.gxp; shaders without a custom
    // position() share the default vertex program.
    for (int i = 0; i < (int)ShaderId::Count; i++) {
        const ShaderInfo &si = shader_info((ShaderId)i);
        std::string v = si.custom_vertex ? std::string(si.name) + "_v" : std::string("default_v");
        std::string f = std::string(si.name) + "_f";
        build_program(g_progs[i], v.c_str(), f.c_str(), (ShaderId)i, si.blend && !si.f16_target, si.f16_target);
    }
    build_program(g_clear, "default_v", "clear_f", ShaderId::Count, false);

    g_notif_region = sceGxmGetNotificationRegion();
    for (int i = 0; i < kFrameArenas; i++) {
        Arena &a = g_arenas[i];
        a.vtx = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, kArenaVertexBytes, 4,
                          SCE_GXM_MEMORY_ATTRIB_READ, &a.vtx_uid);
        a.idx = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, kArenaIndexBytes, 4,
                          SCE_GXM_MEMORY_ATTRIB_READ, &a.idx_uid);
        a.notif.address = g_notif_region + i;
        a.notif.value = 0;
        *a.notif.address = 0;
        a.pending = false;
    }
    return true;
}

void backend_set_vsync(bool on) { g_vsync = on; }
int backend_screen_width() { return kW; }
int backend_screen_height() { return kH; }

void backend_create_texture(Texture *t, const TextureDesc &d) {
    GxmTex *g = new GxmTex();
    if (d.half_float) {
        // RGBA16F, single level, explicit stride
        int stride = ALIGN(d.width, kTexRowAlign) * 8;
        uint8_t *mem = (uint8_t *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, (size_t)stride * d.height,
                                            SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &g->uid);
        if (!mem) platform::fatal("out of GPU memory (%dx%d f16)", d.width, d.height);
        for (int y = 0; y < d.height; y++)
            std::memcpy(mem + (size_t)y * stride, d.rgba + (size_t)y * d.width * 8, (size_t)d.width * 8);
        sceGxmTextureInitLinearStrided(&g->tex, mem, SCE_GXM_TEXTURE_FORMAT_F16F16F16F16_ABGR, d.width, d.height,
                                       stride);
        SceGxmTextureFilter f = d.filter == Filter::Linear ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
        sceGxmTextureSetMinFilter(&g->tex, f);
        sceGxmTextureSetMagFilter(&g->tex, f);
        SceGxmTextureAddrMode wrap = d.repeat ? SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP;
        sceGxmTextureSetUAddrMode(&g->tex, wrap);
        sceGxmTextureSetVAddrMode(&g->tex, wrap);
        t->mip_levels = 1;
        t->impl = g;
        return;
    }
    // Single-level textures: explicit stride, no layout rule involved.
    // Diagnostics (d.gxm_layout): 1 = InitLinear rows aligned to 8, 2 = rows aligned to 16
    // (both mipmapped when d.mipmaps), 3 = strided single level.
    int row_align = d.gxm_layout == 1 ? 8 : 16;
    if ((!d.mipmaps && d.gxm_layout == 0) || d.gxm_layout == 3) {
        int stride = ALIGN(d.width, 16) * 4;
        uint8_t *mem = (uint8_t *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, (size_t)stride * d.height,
                                            SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &g->uid);
        if (!mem)
            mem = (uint8_t *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, (size_t)stride * d.height,
                                       SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &g->uid);
        if (!mem) platform::fatal("out of GPU memory (%dx%d)", d.width, d.height);
        for (int y = 0; y < d.height; y++)
            std::memcpy(mem + (size_t)y * stride, d.rgba + (size_t)y * d.width * 4, (size_t)d.width * 4);
        sceGxmTextureInitLinearStrided(&g->tex, mem, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, d.width, d.height,
                                       stride);
        SceGxmTextureFilter f = d.filter == Filter::Linear ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
        sceGxmTextureSetMinFilter(&g->tex, f);
        sceGxmTextureSetMagFilter(&g->tex, f);
        SceGxmTextureAddrMode wrap = d.repeat ? SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP;
        sceGxmTextureSetUAddrMode(&g->tex, wrap);
        sceGxmTextureSetVAddrMode(&g->tex, wrap);
        t->mip_levels = 1;
        t->impl = g;
        return;
    }
    // Mipmapped linear layout: rows aligned to row_align texels, levels packed back to back.
    const int kTexRowAlign = row_align;
    int levels = 1;
    if (d.mipmaps) {
        int w = d.width, h = d.height;
        while (w > 1 || h > 1) { w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1; levels++; }
    }
    size_t total = 0;
    {
        int w = d.width, h = d.height;
        for (int l = 0; l < levels; l++) {
            total += (size_t)ALIGN(w, kTexRowAlign) * h * 4;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
    }
    // Prefer CDRAM (VRAM); the 2x atlases do not all fit, so overflow goes to main memory,
    // which the GPU can sample from directly.
    uint8_t *mem = (uint8_t *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, total,
                                        SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &g->uid);
    if (!mem)
        mem = (uint8_t *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, total,
                                   SCE_GXM_TEXTURE_ALIGNMENT, SCE_GXM_MEMORY_ATTRIB_READ, &g->uid);
    if (!mem) platform::fatal("out of GPU memory (%dx%d)", d.width, d.height);

    // level 0
    int w = d.width, h = d.height;
    uint8_t *dst = mem;
    for (int y = 0; y < h; y++) std::memcpy(dst + (size_t)y * ALIGN(w, kTexRowAlign) * 4, d.rgba + (size_t)y * w * 4, (size_t)w * 4);
    // remaining levels: 2x2 box filter of the previous level (what glGenerateMipmap does)
    for (int l = 1; l < levels; l++) {
        const uint8_t *src = dst;
        int sw = w, sh = h, sstride = ALIGN(sw, kTexRowAlign);
        dst += (size_t)sstride * sh * 4;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        int dstride = ALIGN(w, kTexRowAlign);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int x0 = x * 2, y0 = y * 2;
                int x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
                for (int c = 0; c < 4; c++) {
                    int s = src[((size_t)y0 * sstride + x0) * 4 + c] + src[((size_t)y0 * sstride + x1) * 4 + c] +
                            src[((size_t)y1 * sstride + x0) * 4 + c] + src[((size_t)y1 * sstride + x1) * 4 + c];
                    dst[((size_t)y * dstride + x) * 4 + c] = (uint8_t)((s + 2) / 4);
                }
            }
    }

    sceGxmTextureInitLinear(&g->tex, mem, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, d.width, d.height,
                            levels - 1);
    SceGxmTextureFilter f = d.filter == Filter::Linear ? SCE_GXM_TEXTURE_FILTER_LINEAR
                                                       : SCE_GXM_TEXTURE_FILTER_POINT;
    sceGxmTextureSetMinFilter(&g->tex, f);
    sceGxmTextureSetMagFilter(&g->tex, f);
    sceGxmTextureSetMipFilter(&g->tex, d.mipmaps ? SCE_GXM_TEXTURE_MIP_FILTER_ENABLED
                                                 : SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
    SceGxmTextureAddrMode wrap = d.repeat ? SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP;
    sceGxmTextureSetUAddrMode(&g->tex, wrap);
    sceGxmTextureSetVAddrMode(&g->tex, wrap);
    t->mip_levels = levels;
    t->impl = g;
}

void backend_destroy_texture(Texture *t) {
    GxmTex *g = (GxmTex *)t->impl;
    sceGxmFinish(g_ctx);
    gpu_free(g->uid);
    delete g;
}

void backend_create_canvas(Canvas *c) {
    GxmCanvas *g = new GxmCanvas();
    bool f16 = c->format == CanvasFormat::RGBA16F;
    int bpp = f16 ? 8 : 4;
    int stride = ALIGN(c->w, 16);
    g->mem = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, (size_t)bpp * stride * c->h,
                       SCE_GXM_COLOR_SURFACE_ALIGNMENT,
                       SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, &g->uid);
    sceGxmColorSurfaceInit(&g->surface, f16 ? SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR : SCE_GXM_COLOR_FORMAT_A8B8G8R8,
                           SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                           f16 ? SCE_GXM_OUTPUT_REGISTER_SIZE_64BIT : SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
                           c->w, c->h, stride, g->mem);
    GxmTex *gt = new GxmTex();
    sceGxmTextureInitLinearStrided(&gt->tex, g->mem,
                                   f16 ? SCE_GXM_TEXTURE_FORMAT_F16F16F16F16_ABGR : SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR,
                                   c->w, c->h, stride * bpp);
    SceGxmTextureFilter filt = c->filter == Filter::Linear ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
    sceGxmTextureSetMinFilter(&gt->tex, filt);
    sceGxmTextureSetMagFilter(&gt->tex, filt);
    sceGxmTextureSetUAddrMode(&gt->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&gt->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
    gt->uid = -1;
    c->tex.impl = gt;
    c->impl = g;
    target_for(c->w, c->h);
}

void backend_submit(const FrameData &f) {
    Arena &A = g_arenas[g_arena];
    if (A.pending) {
        sceGxmNotificationWait(&A.notif);
        A.pending = false;
    }
    if (f.vertices.size() * sizeof(Vertex) > kArenaVertexBytes || f.indices.size() * 2 > kArenaIndexBytes)
        platform::fatal("frame too large: %u vertices", (unsigned)f.vertices.size());
    std::memcpy(A.vtx, f.vertices.data(), f.vertices.size() * sizeof(Vertex));
    std::memcpy(A.idx, f.indices.data(), f.indices.size() * sizeof(uint16_t));
    const Vertex *vbase = (const Vertex *)A.vtx;
    const uint16_t *ibase = (const uint16_t *)A.idx;

    // Find the last scene so it can signal the arena notification.
    int last_target = -1;
    for (int i = 0; i < (int)f.cmds.size(); i++)
        if (f.cmds[i].type == CmdType::Target) last_target = i;

    bool in_scene = false;
    int tw = kW, th = kH;
    float proj[16];
    float screen[4];
    const Program *cur = nullptr;
    const Texture *cur_tex = nullptr;

    auto end_scene = [&](bool last) {
        if (!in_scene) return;
        if (last) {
            A.notif.value++;
            sceGxmEndScene(g_ctx, nullptr, &A.notif);
            A.pending = true;
        } else {
            sceGxmEndScene(g_ctx, nullptr, nullptr);
        }
        in_scene = false;
    };

    for (int ci = 0; ci < (int)f.cmds.size(); ci++) {
        const Cmd &c = f.cmds[ci];
        if (c.type == CmdType::Target) {
            end_scene(false);
            if (c.target) {
                GxmCanvas *gc = (GxmCanvas *)c.target->impl;
                Target &T = target_for(c.target->w, c.target->h);
                sceGxmBeginScene(g_ctx, 0, T.rt, nullptr, nullptr, nullptr, &gc->surface, &T.depth);
                tw = c.target->w;
                th = c.target->h;
            } else {
                Target &T = target_for(kW, kH);
                sceGxmBeginScene(g_ctx, 0, T.rt, nullptr, nullptr, g_disp_sync[g_back],
                                 &g_disp_surf[g_back], &T.depth);
                tw = kW;
                th = kH;
            }
            in_scene = true;
            sceGxmSetViewport(g_ctx, tw * 0.5f, tw * 0.5f, th * 0.5f, -th * 0.5f, 0.5f, 0.5f);
            sceGxmSetRegionClip(g_ctx, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, tw - 1, th - 1);
            sceGxmSetFrontDepthFunc(g_ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
            sceGxmSetFrontDepthWriteEnable(g_ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
            ortho(proj, (float)tw, (float)th);
            screen[0] = (float)tw; screen[1] = (float)th; screen[2] = 1; screen[3] = 0;
            cur = nullptr;
            cur_tex = nullptr;
            (void)last_target;
            continue;
        }
        if (!in_scene) continue;
        if (c.type == CmdType::Clear) {
            // Full-target quad with blending disabled. Constant geometry (4096x4096 px covers any
            // target), created once and never modified, so it is safe while frames overlap.
            static Vertex *gq = nullptr;
            static uint16_t *gi = nullptr;
            if (!gq) {
                SceUID quad_uid, idx_uid;
                gq = (Vertex *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4096, 4,
                                         SCE_GXM_MEMORY_ATTRIB_READ, &quad_uid);
                gi = (uint16_t *)gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4096, 4,
                                           SCE_GXM_MEMORY_ATTRIB_READ, &idx_uid);
                const uint16_t qidx[6] = {0, 1, 2, 2, 1, 3};
                std::memcpy(gi, qidx, sizeof qidx);
                gq[0] = {0, 0, 0, 0, 0};
                gq[1] = {0, 4096, 0, 0, 0};
                gq[2] = {4096, 0, 0, 0, 0};
                gq[3] = {4096, 4096, 0, 0, 0};
            }
            sceGxmSetVertexProgram(g_ctx, g_clear.vp);
            sceGxmSetFragmentProgram(g_ctx, g_clear.fp);
            void *ub;
            sceGxmReserveVertexDefaultUniformBuffer(g_ctx, &ub);
            sceGxmSetUniformDataF(ub, g_clear.p.proj, 0, 16, proj);
            sceGxmReserveFragmentDefaultUniformBuffer(g_ctx, &ub);
            float col[4] = {c.clear_color.r, c.clear_color.g, c.clear_color.b, c.clear_color.a};
            sceGxmSetUniformDataF(ub, g_clear.p.clear, 0, 4, col);
            sceGxmSetVertexStream(g_ctx, 0, gq);
            sceGxmDraw(g_ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, gi, 6);
            cur = nullptr;
            continue;
        }
        const Program &P = g_progs[(int)c.shader];
        if (&P != cur) {
            sceGxmSetVertexProgram(g_ctx, P.vp);
            sceGxmSetFragmentProgram(g_ctx, P.fp);
            cur = &P;
            cur_tex = nullptr;
        }
        if (c.texture != cur_tex && P.p.tex_unit >= 0) {
            sceGxmSetFragmentTexture(g_ctx, P.p.tex_unit, &((GxmTex *)c.texture->impl)->tex);
            cur_tex = c.texture;
        }
        for (int a = 0; a < kAuxSlots; a++)
            if (c.aux[a] && P.p.aux_unit[a] >= 0) {
                sceGxmSetFragmentTexture(g_ctx, P.p.aux_unit[a], &((GxmTex *)c.aux[a]->impl)->tex);
                cur_tex = nullptr;  // unit numbering differs per program
            }
        const ShaderInfo &si = shader_info(c.shader);
        const float *u = &f.uniforms[c.uniforms];
        void *vub, *fub;
        sceGxmReserveVertexDefaultUniformBuffer(g_ctx, &vub);
        sceGxmSetUniformDataF(vub, P.p.proj, 0, 16, proj);
        if (P.p.screen_v) sceGxmSetUniformDataF(vub, P.p.screen_v, 0, 4, screen);
        sceGxmReserveFragmentDefaultUniformBuffer(g_ctx, &fub);
        if (P.p.screen_f) sceGxmSetUniformDataF(fub, P.p.screen_f, 0, 4, screen);
        for (int i = 0; i < si.uniform_count && i < 16; i++) {
            const UniformDesc &ud = si.uniforms[i];
            if (P.p.u_vert[i]) sceGxmSetUniformDataF(vub, P.p.u_vert[i], 0, ud.components, u + ud.offset);
            if (P.p.u_frag[i]) sceGxmSetUniformDataF(fub, P.p.u_frag[i], 0, ud.components, u + ud.offset);
        }
        sceGxmSetVertexStream(g_ctx, 0, vbase + c.first_vertex);
        sceGxmDraw(g_ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, ibase + c.first_index,
                   c.index_count);
    }
    end_scene(true);

    sceGxmPadHeartbeat(&g_disp_surf[g_back], g_disp_sync[g_back]);
    DisplayData dd{g_disp_mem[g_back]};
    sceGxmDisplayQueueAddEntry(g_disp_sync[g_front], g_disp_sync[g_back], &dd);
    g_front = g_back;
    g_back = (g_back + 1) % kDisplayBuffers;
    g_arena = (g_arena + 1) % kFrameArenas;
}

}  // namespace vl::gfx

namespace vl::gfx {

// Last presented frame, top-down RGBA rows (waits for the GPU to finish it).
void backend_read_screen(std::vector<uint8_t> &out) {
    sceGxmFinish(g_ctx);
    out.resize((size_t)kW * kH * 4);
    const uint8_t *src = (const uint8_t *)g_disp_mem[g_front];
    for (int y = 0; y < kH; y++) std::memcpy(&out[(size_t)y * kW * 4], src + (size_t)y * kStride * 4, (size_t)kW * 4);
}

}  // namespace vl::gfx
