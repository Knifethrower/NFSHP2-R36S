// citro2d on the R36S: the screen targets, images and rectangles, drawn
// through the same GL layer as citro3d's draws (c3d_gles.c).
//
// As citro2d does, the quads are batched until something else needs the
// GPU: a texture or tint mode change, C2D_Flush, a target change, any
// citro3d draw or clear, or the frame's end (r36sgl_set_flush2d). Each
// quad is citro2d's: the image's sub-texture corners, the draw params'
// centre and rotation, and per-corner tint colours and blends, mixed as
// citro2d's three tint modes mix them. C2D_Prepare leaves citro3d's state
// as citro2d leaves it -- its attribute and buffer layout, a GEQUAL depth
// test, no culling -- and the draws use citro3d's blend and depth state.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <citro2d.h>

#include "c3d_shaders.h"
#include "r36s_gl.h"
#include "glthread.h"

typedef struct {
    float pos[3];
    float uv[2];
    float blend;
    u32   color;
} C2DVertex;

static C2D_TintMode s_tintMode;
static u32          s_sceneW = 400, s_sceneH = 240;
static C3D_Mtx      s_proj;
static C2DVertex*   s_verts;
static int          s_count, s_cap;
static C3D_Tex*     s_batchTex;
static bool         s_batchTextured;
static C2D_TintMode s_batchMode;

// A shader binary of citro2d's, for C3D_BindProgram: the GL layer has no
// twin for it, so a citro3d draw left on it draws nothing, where the
// 3DS would draw with citro2d's shader.
static DVLE_s          s_dvle = { "render2d", 0, NULL, NULL };
static shaderInstance_s s_vsh = { &s_dvle, 0, 0, { 0 }, NULL, 0, 0 };
static shaderProgram_s s_program = { &s_vsh, NULL, { 0 }, 0 };

// Programs: untextured, then textured in the three tint modes. citro2d
// draws through texenv stage 0, whose scale the game doubles for the GS's
// colours (uirender.c's set_tev_scale), so the stage's scale is applied
// here too: uScale, clamped as the combiner clamps.
static GLuint s_prog[4];
static GLint  s_uMvp[4], s_uDepth[4], s_uScale[4];

static const char* const kFsC2D[4] = {
    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    "uniform vec2 uScale;\n"
    "void main() { gl_FragColor = clamp(vColor * uScale.xxxy, 0.0, 1.0); }\n",

    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    R36S_FS_HIGHP
    "varying HIGHP vec4 vTex0;\n"
    "varying float vBlend;\n"
    "uniform sampler2D uTex0;\n"
    "uniform vec2 uScale;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex0, vTex0.xy);\n"
    "    vec4 c = vec4(mix(t.rgb, vColor.rgb, vBlend), t.a * vColor.a);\n"
    "    gl_FragColor = clamp(c * uScale.xxxy, 0.0, 1.0);\n"
    "}\n",

    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    R36S_FS_HIGHP
    "varying HIGHP vec4 vTex0;\n"
    "varying float vBlend;\n"
    "uniform sampler2D uTex0;\n"
    "uniform vec2 uScale;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex0, vTex0.xy);\n"
    "    vec4 c = vec4(t.rgb * mix(vec3(1.0), vColor.rgb, vBlend), t.a * vColor.a);\n"
    "    gl_FragColor = clamp(c * uScale.xxxy, 0.0, 1.0);\n"
    "}\n",

    "precision mediump float;\n"
    "varying vec4 vColor;\n"
    R36S_FS_HIGHP
    "varying HIGHP vec4 vTex0;\n"
    "varying float vBlend;\n"
    "uniform sampler2D uTex0;\n"
    "uniform vec2 uScale;\n"
    "void main() {\n"
    "    vec4 t = texture2D(uTex0, vTex0.xy);\n"
    "    float l = dot(t.rgb, vec3(0.299, 0.587, 0.114));\n"
    "    vec4 c = vec4(mix(t.rgb, l * vColor.rgb, vBlend), t.a * vColor.a);\n"
    "    gl_FragColor = clamp(c * uScale.xxxy, 0.0, 1.0);\n"
    "}\n",
};

static void flush(void);

bool C2D_Init(size_t maxObjects)
{
    if (s_verts != NULL) return true;
    s_cap = (int)(maxObjects ? maxObjects : C2D_DEFAULT_MAX_OBJECTS) * 6;
    s_verts = (C2DVertex*)malloc((size_t)s_cap * sizeof(C2DVertex));
    if (s_verts == NULL) return false;
    r36sgl_set_flush2d(flush);
    C2D_SceneSize(400, 240, true);
    return true;
}

void C2D_Fini(void)
{
    flush();
    r36sgl_set_flush2d(NULL);
    free(s_verts);
    s_verts = NULL;
}

// citro2d's C2D_Prepare: its shader, attributes and buffer, a depth test
// that lets equal depths overwrite (overlapping sprites), no culling.
void C2D_Prepare(void)
{
    C3D_AttrInfo* ai;
    C3D_BufInfo* bi;
    C3D_TexEnv* env;
    flush();
    C3D_BindProgram(&s_program);
    ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 1);
    AttrInfo_AddLoader(ai, 3, GPU_UNSIGNED_BYTE, 4);
    bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, s_verts, sizeof(C2DVertex), 4, 0x3210);
    for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    env = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
}

void C2D_Flush(void) { flush(); }

void C2D_SceneSize(u32 width, u32 height, bool tilt)
{
    flush();
    // The 3DS's screen targets are portrait (240x400); the scene is drawn
    // in landscape coordinates, which on the R36S are the target's own.
    if (tilt) {
        u32 t = width;
        width = height;
        height = t;
    }
    s_sceneW = width;
    s_sceneH = height;
    if (width == 640 && height == 480) {
        // The top screen: its 640x480 in the middle of the whole picture
        // (screen.h), with nothing stretched.
        float ox = (r36s_screen_full_w() - 640.0f) * 0.5f;
        float oy = (r36s_screen_full_h() - 480.0f) * 0.5f;
        Mtx_OrthoTilt(&s_proj, -ox, 640.0f + ox, 480.0f + oy, -oy, 1.0f, -1.0f, true);
        return;
    }
    Mtx_OrthoTilt(&s_proj, 0.0f, (float)width, (float)height, 0.0f, 1.0f, -1.0f, true);
}

C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side)
{
    // The R36S's top screen is 640x480 (screen.h); the bottom keeps the
    // 3DS's 320x240 for the panels drawn on it. Portrait, as citro2d makes
    // them; the GL layer turns a screen's target landscape.
    int w = screen == GFX_TOP ? 640 : 320, h = screen == GFX_TOP ? 480 : 240;
    C3D_RenderTarget* t = C3D_RenderTargetCreate(h, w, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (t != NULL) C3D_RenderTargetSetOutput(t, screen, side, 0);
    return t;
}

// citro2d's colours are 0xAABBGGRR; the target's clear wants 0xRRGGBBAA.
void C2D_TargetClear(C3D_RenderTarget* target, u32 color)
{
    u32 rgba = ((color & 0xFF) << 24) | (((color >> 8) & 0xFF) << 16)
             | (((color >> 16) & 0xFF) << 8) | ((color >> 24) & 0xFF);
    flush();
    C3D_FrameBufClear(&target->frameBuf, C3D_CLEAR_ALL, rgba, 0);
}

void C2D_SetTintMode(C2D_TintMode mode) { s_tintMode = mode; }

void C2D_PlainImageTint(C2D_ImageTint* tint, u32 color, float blend)
{
    for (int i = 0; i < 4; i++) {
        tint->corners[i].color = color;
        tint->corners[i].blend = blend;
    }
}

void C2D_SetImageTint(C2D_ImageTint* tint, C2D_Corner corner, u32 color, float blend)
{
    tint->corners[corner].color = color;
    tint->corners[corner].blend = blend;
}

static bool ensure_programs(void)
{
    if (s_prog[0] != 0) return true;
    for (int i = 0; i < 4; i++) {
        s_prog[i] = r36sgl_link(kVsC2D, kFsC2D[i], "citro2d");
        if (s_prog[i] == 0) return false;
        s_uMvp[i] = glGetUniformLocation(s_prog[i], "mvp");
        s_uDepth[i] = glGetUniformLocation(s_prog[i], "uDepth");
        s_uScale[i] = glGetUniformLocation(s_prog[i], "uScale");
        r36sgl_use_program(s_prog[i]);
        if (i > 0) glUniform1i(glGetUniformLocation(s_prog[i], "uTex0"), 0);
    }
    return true;
}

static void flush(void)
{
    int n = s_count, which;
    float mvp[4][4], depth[2];
    if (n == 0) return;
    s_count = 0;
    // The 3D draws held for grouping were drawn before these quads.
    r36sgl_flush_held();
    if (!r36sgl_active() || !ensure_programs() || !r36sgl_begin_draw()) return;
    r36sgl_debug_2d(n / 6);
    which = s_batchTextured ? 1 + (int)s_batchMode : 0;
    // NFSHP2_C2DTEST=1: every quad magenta, no texture; =2: every image
    // as its texture is, untinted.
    {
        static int test = -1;
        if (test < 0) test = getenv("NFSHP2_C2DTEST") ? atoi(getenv("NFSHP2_C2DTEST")) : 0;
        if (test == 1) {
            which = 0;
            for (int i = 0; i < n; i++) s_verts[i].color = 0xFFFF00FFu;
        } else if ((test == 2 || test == 3) && s_batchTextured) {
            which = 1;
            for (int i = 0; i < n; i++) {
                s_verts[i].color = 0xFFFFFFFFu;
                s_verts[i].blend = 0.0f;
            }
            // =3: opaque too, the texture's colour whatever its alpha.
            if (test == 3) glDisable(GL_BLEND);
        }
    }
    if (which > 3) which = 1;
    if (r36sgl_debug_frame()) {
        const C2DVertex* v = s_verts;
        printf("c2d: %d quads, %s %dx%d fmt %d, mode %d; first (%.1f,%.1f) z %.2f uv (%.3f,%.3f)-(%.3f,%.3f)"
               " colour %08x blend %.2f\n", n / 6, s_batchTextured ? "tex" : "solid",
               s_batchTex ? s_batchTex->width : 0, s_batchTex ? s_batchTex->height : 0,
               s_batchTex ? (int)s_batchTex->fmt : -1, (int)s_batchMode, v[0].pos[0], v[0].pos[1],
               v[0].pos[2], v[0].uv[0], v[0].uv[1], v[5].uv[0], v[5].uv[1], (unsigned)v[0].color,
               v[0].blend);
    }
    r36sgl_use_program(s_prog[which]);
    for (int r = 0; r < 4; r++) {
        mvp[r][0] = s_proj.r[r].x; mvp[r][1] = s_proj.r[r].y;
        mvp[r][2] = s_proj.r[r].z; mvp[r][3] = s_proj.r[r].w;
    }
    glUniform4fv(s_uMvp[which], 4, &mvp[0][0]);
    r36sgl_depth_map(depth);
    glUniform2f(s_uDepth[which], depth[0], depth[1]);
    {
        static const float k[4] = { 1.0f, 2.0f, 4.0f, 1.0f };
        const C3D_TexEnv* e = C3D_GetTexEnv(0);
        glUniform2f(s_uScale[which], k[e->scaleRgb & 3], k[e->scaleAlpha & 3]);
    }
    if (s_batchTextured) r36sgl_bind_texture(0, s_batchTex);
    r36sgl_client_arrays();
    r36sgl_attrib_mask(0xF);
    r36sgl_attrib_pointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(C2DVertex), s_verts[0].pos);
    r36sgl_attrib_pointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(C2DVertex), s_verts[0].uv);
    r36sgl_attrib_pointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(C2DVertex), &s_verts[0].blend);
    r36sgl_attrib_pointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(C2DVertex), &s_verts[0].color);
    if (r36sgl_debug_frame()) {
        // How many pixels of the batch's bounding box the draw changed,
        // with the depth and blend state it ran with.
        GLint vp[4], df = 0, src = 0, dst = 0;
        GLboolean bl = glIsEnabled(GL_BLEND), dt = glIsEnabled(GL_DEPTH_TEST);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        int bx0, by0, bw, bh;
        unsigned changed = 0;
        glGetIntegerv(GL_VIEWPORT, vp);
        glGetIntegerv(GL_DEPTH_FUNC, &df);
        glGetIntegerv(GL_BLEND_SRC_RGB, &src);
        glGetIntegerv(GL_BLEND_DST_RGB, &dst);
        for (int i = 0; i < n; i++) {
            if (s_verts[i].pos[0] < x0) x0 = s_verts[i].pos[0];
            if (s_verts[i].pos[0] > x1) x1 = s_verts[i].pos[0];
            if (s_verts[i].pos[1] < y0) y0 = s_verts[i].pos[1];
            if (s_verts[i].pos[1] > y1) y1 = s_verts[i].pos[1];
        }
        bx0 = x0 < 0 ? 0 : (int)x0;
        bw = (int)(x1 > vp[2] ? vp[2] : x1) - bx0;
        by0 = vp[3] - (int)(y1 > vp[3] ? vp[3] : y1);
        bh = vp[3] - (y0 < 0 ? 0 : (int)y0) - by0;
        if (bw > 0 && bh > 0) {
            unsigned char* a = (unsigned char*)malloc((size_t)bw * bh * 4);
            unsigned char* b = (unsigned char*)malloc((size_t)bw * bh * 4);
            glReadPixels(bx0, by0, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, a);
            glDrawArrays(GL_TRIANGLES, 0, n);
            glReadPixels(bx0, by0, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, b);
            for (int i = 0; i < bw * bh; i++) changed += memcmp(a + i * 4, b + i * 4, 3) != 0;
            free(a);
            free(b);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, n);
        }
        printf("c2d:   box (%.0f,%.0f)-(%.0f,%.0f): %u of %d pixels changed; depth %d func %04x,"
               " blend %d %04x/%04x; error 0x%x\n", x0, y0, x1, y1, changed,
               bw > 0 && bh > 0 ? bw * bh : 0, dt, df, bl, src, dst, glGetError());
        return;
    }
    glDrawArrays(GL_TRIANGLES, 0, n);
}

// Appends one quad: corners top-left, top-right, bottom-left,
// bottom-right, as two triangles.
static bool add_quad(C3D_Tex* tex, const float xy[4][2], float z, const float uv[4][2],
                     const u32 color[4], const float blend[4])
{
    static const int order[6] = { 0, 2, 1, 1, 2, 3 };
    bool textured = tex != NULL;
    if (s_verts == NULL && !C2D_Init(0)) return false;
    if (s_count > 0 && (textured != s_batchTextured || (textured && tex != s_batchTex)
                        || (textured && s_tintMode != s_batchMode)))
        flush();
    if (s_count + 6 > s_cap) flush();
    s_batchTex = tex;
    s_batchTextured = textured;
    s_batchMode = s_tintMode;
    for (int i = 0; i < 6; i++) {
        int c = order[i];
        C2DVertex* v = &s_verts[s_count++];
        v->pos[0] = xy[c][0];
        v->pos[1] = xy[c][1];
        v->pos[2] = z;
        v->uv[0] = uv ? uv[c][0] : 0.0f;
        v->uv[1] = uv ? uv[c][1] : 0.0f;
        v->blend = blend ? blend[c] : 0.0f;
        v->color = color[c];
    }
    return true;
}

bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams* params, const C2D_ImageTint* tint)
{
    const Tex3DS_SubTexture* st = img.subtex;
    float xy[4][2], uv[4][2], blend[4];
    u32 color[4];
    float x = -params->center.x, y = -params->center.y;
    float w = params->pos.w, h = params->pos.h;
    float c = cosf(params->angle), sn = sinf(params->angle);
    const float corner[4][2] = { { x, y }, { x + w, y }, { x, y + h }, { x + w, y + h } };
    if (img.tex == NULL || st == NULL) return false;
    for (int i = 0; i < 4; i++) {
        float px = corner[i][0], py = corner[i][1];
        if (params->angle != 0.0f) {
            float rx = px * c - py * sn, ry = px * sn + py * c;
            px = rx;
            py = ry;
        }
        xy[i][0] = params->pos.x + px;
        xy[i][1] = params->pos.y + py;
    }
    uv[0][0] = st->left;  uv[0][1] = st->top;
    uv[1][0] = st->right; uv[1][1] = st->top;
    uv[2][0] = st->left;  uv[2][1] = st->bottom;
    uv[3][0] = st->right; uv[3][1] = st->bottom;
    for (int i = 0; i < 4; i++) {
        color[i] = tint ? tint->corners[i].color : 0xFFFFFFFFu;
        blend[i] = tint ? tint->corners[i].blend : 0.0f;
    }
    return add_quad(img.tex, (const float(*)[2])xy, params->depth, (const float(*)[2])uv, color, blend);
}

bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 clr0, u32 clr1,
                       u32 clr2, u32 clr3)
{
    const float xy[4][2] = { { x, y }, { x + w, y }, { x, y + h }, { x + w, y + h } };
    const u32 color[4] = { clr0, clr1, clr2, clr3 };
    return add_quad(NULL, xy, z, NULL, color, NULL);
}
