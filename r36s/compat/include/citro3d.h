// The subset of citro3d the port uses, declared for the R36S build and
// implemented over GLES 2 in r36s/compat/src/c3d_gles.c.
//
// Types, field names and enum values follow citro3d and libctru's GPU
// enums, so the 3DS paths compile unchanged. What differs is underneath:
// the PICA200's shader binaries become GLSL programs chosen by the
// binary's name (r36s/compat/shaders), the texture combiner becomes a
// fragment shader generated from the six stages' state, textures stay in
// the PICA's tiled layout in memory and are untiled on upload, and the
// projection matrices skip the 3DS screen's 90-degree tilt.

#ifndef R36S_COMPAT_CITRO3D_H
#define R36S_COMPAT_CITRO3D_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "3ds.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------- GPU enums

typedef enum {
    GPU_RGBA8 = 0x0, GPU_RGB8 = 0x1, GPU_RGBA5551 = 0x2, GPU_RGB565 = 0x3,
    GPU_RGBA4 = 0x4, GPU_LA8 = 0x5, GPU_HILO8 = 0x6, GPU_L8 = 0x7,
    GPU_A8 = 0x8, GPU_LA4 = 0x9, GPU_L4 = 0xA, GPU_A4 = 0xB,
    GPU_ETC1 = 0xC, GPU_ETC1A4 = 0xD,
} GPU_TEXCOLOR;

typedef enum {
    GPU_TEX_2D = 0x0, GPU_TEX_CUBE_MAP = 0x1, GPU_TEX_SHADOW_2D = 0x2,
    GPU_TEX_PROJECTION = 0x3, GPU_TEX_SHADOW_CUBE = 0x4, GPU_TEX_DISABLED = 0x5,
} GPU_TEXTURE_MODE_PARAM;

typedef enum {
    GPU_TEXFACE_2D = 0, GPU_POSITIVE_X = 0, GPU_NEGATIVE_X = 1,
    GPU_POSITIVE_Y = 2, GPU_NEGATIVE_Y = 3, GPU_POSITIVE_Z = 4,
    GPU_NEGATIVE_Z = 5,
} GPU_TEXFACE;

typedef enum { GPU_NEAREST = 0x0, GPU_LINEAR = 0x1 } GPU_TEXTURE_FILTER_PARAM;
typedef enum {
    GPU_CLAMP_TO_EDGE = 0x0, GPU_CLAMP_TO_BORDER = 0x1, GPU_REPEAT = 0x2,
    GPU_MIRRORED_REPEAT = 0x3,
} GPU_TEXTURE_WRAP_PARAM;

#define GPU_TEXTURE_MAG_FILTER(v) (((v) & 0x1) << 1)
#define GPU_TEXTURE_MIN_FILTER(v) (((v) & 0x1) << 2)
#define GPU_TEXTURE_MIP_FILTER(v) (((v) & 0x1) << 24)
#define GPU_TEXTURE_WRAP_S(v)     (((v) & 0x3) << 12)
#define GPU_TEXTURE_WRAP_T(v)     (((v) & 0x3) << 8)
#define GPU_TEXTURE_MODE(v)       (((v) & 0x7) << 28)
#define GPU_TEXTURE_ETC1_PARAM    BIT(5)

typedef enum {
    GPU_TEXUNIT0 = 0x1, GPU_TEXUNIT1 = 0x2, GPU_TEXUNIT2 = 0x4,
} GPU_TEXUNIT;

typedef enum {
    GPU_RB_RGBA8 = 0, GPU_RB_RGB8 = 1, GPU_RB_RGBA5551 = 2,
    GPU_RB_RGB565 = 3, GPU_RB_RGBA4 = 4,
} GPU_COLORBUF;

typedef enum {
    GPU_RB_DEPTH16 = 0, GPU_RB_DEPTH24 = 2, GPU_RB_DEPTH24_STENCIL8 = 3,
} GPU_DEPTHBUF;

typedef enum {
    GPU_NEVER = 0, GPU_ALWAYS = 1, GPU_EQUAL = 2, GPU_NOTEQUAL = 3,
    GPU_LESS = 4, GPU_LEQUAL = 5, GPU_GREATER = 6, GPU_GEQUAL = 7,
} GPU_TESTFUNC;

typedef enum {
    GPU_EARLYDEPTH_GEQUAL = 0, GPU_EARLYDEPTH_GREATER = 1,
    GPU_EARLYDEPTH_LEQUAL = 2, GPU_EARLYDEPTH_LESS = 3,
} GPU_EARLYDEPTHFUNC;

typedef enum {
    GPU_SCISSOR_DISABLE = 0, GPU_SCISSOR_INVERT = 1, GPU_SCISSOR_NORMAL = 3,
} GPU_SCISSORMODE;

typedef enum {
    GPU_STENCIL_KEEP = 0, GPU_STENCIL_ZERO = 1, GPU_STENCIL_REPLACE = 2,
    GPU_STENCIL_INCR = 3, GPU_STENCIL_DECR = 4, GPU_STENCIL_INVERT = 5,
    GPU_STENCIL_INCR_WRAP = 6, GPU_STENCIL_DECR_WRAP = 7,
} GPU_STENCILOP;

typedef enum {
    GPU_WRITE_RED = 0x01, GPU_WRITE_GREEN = 0x02, GPU_WRITE_BLUE = 0x04,
    GPU_WRITE_ALPHA = 0x08, GPU_WRITE_DEPTH = 0x10,
    GPU_WRITE_COLOR = 0x0F, GPU_WRITE_ALL = 0x1F,
} GPU_WRITEMASK;

typedef enum {
    GPU_BLEND_ADD = 0, GPU_BLEND_SUBTRACT = 1,
    GPU_BLEND_REVERSE_SUBTRACT = 2, GPU_BLEND_MIN = 3, GPU_BLEND_MAX = 4,
} GPU_BLENDEQUATION;

typedef enum {
    GPU_ZERO = 0, GPU_ONE = 1, GPU_SRC_COLOR = 2,
    GPU_ONE_MINUS_SRC_COLOR = 3, GPU_DST_COLOR = 4,
    GPU_ONE_MINUS_DST_COLOR = 5, GPU_SRC_ALPHA = 6,
    GPU_ONE_MINUS_SRC_ALPHA = 7, GPU_DST_ALPHA = 8,
    GPU_ONE_MINUS_DST_ALPHA = 9, GPU_CONSTANT_COLOR = 10,
    GPU_ONE_MINUS_CONSTANT_COLOR = 11, GPU_CONSTANT_ALPHA = 12,
    GPU_ONE_MINUS_CONSTANT_ALPHA = 13, GPU_SRC_ALPHA_SATURATE = 14,
} GPU_BLENDFACTOR;

typedef enum {
    GPU_LOGICOP_CLEAR = 0, GPU_LOGICOP_COPY = 3,
} GPU_LOGICOP;

typedef enum {
    GPU_FRAGOPMODE_GL = 0, GPU_FRAGOPMODE_GAS_ACC = 1,
    GPU_FRAGOPMODE_SHADOW = 3,
} GPU_FRAGOPMODE;

typedef enum {
    GPU_BYTE = 0, GPU_UNSIGNED_BYTE = 1, GPU_SHORT = 2, GPU_FLOAT = 3,
} GPU_FORMATS;

typedef enum {
    GPU_CULL_NONE = 0, GPU_CULL_FRONT_CCW = 1, GPU_CULL_BACK_CCW = 2,
} GPU_CULLMODE;

#define GPU_ATTRIBFMT(i, n, f) (((((n) - 1) << 2) | ((f) & 3)) << ((i) * 4))

typedef enum {
    GPU_PRIMARY_COLOR = 0x00, GPU_FRAGMENT_PRIMARY_COLOR = 0x01,
    GPU_FRAGMENT_SECONDARY_COLOR = 0x02, GPU_TEXTURE0 = 0x03,
    GPU_TEXTURE1 = 0x04, GPU_TEXTURE2 = 0x05, GPU_TEXTURE3 = 0x06,
    GPU_PREVIOUS_BUFFER = 0x0D, GPU_CONSTANT = 0x0E, GPU_PREVIOUS = 0x0F,
} GPU_TEVSRC;

typedef enum {
    GPU_TEVOP_RGB_SRC_COLOR = 0x00, GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR = 0x01,
    GPU_TEVOP_RGB_SRC_ALPHA = 0x02, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA = 0x03,
    GPU_TEVOP_RGB_SRC_R = 0x04, GPU_TEVOP_RGB_ONE_MINUS_SRC_R = 0x05,
    GPU_TEVOP_RGB_SRC_G = 0x08, GPU_TEVOP_RGB_ONE_MINUS_SRC_G = 0x09,
    GPU_TEVOP_RGB_SRC_B = 0x0C, GPU_TEVOP_RGB_ONE_MINUS_SRC_B = 0x0D,
} GPU_TEVOP_RGB;

typedef enum {
    GPU_TEVOP_A_SRC_ALPHA = 0x00, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA = 0x01,
    GPU_TEVOP_A_SRC_R = 0x02, GPU_TEVOP_A_ONE_MINUS_SRC_R = 0x03,
    GPU_TEVOP_A_SRC_G = 0x04, GPU_TEVOP_A_ONE_MINUS_SRC_G = 0x05,
    GPU_TEVOP_A_SRC_B = 0x06, GPU_TEVOP_A_ONE_MINUS_SRC_B = 0x07,
} GPU_TEVOP_A;

typedef enum {
    GPU_REPLACE = 0x00, GPU_MODULATE = 0x01, GPU_ADD = 0x02,
    GPU_ADD_SIGNED = 0x03, GPU_INTERPOLATE = 0x04, GPU_SUBTRACT = 0x05,
    GPU_DOT3_RGB = 0x06, GPU_DOT3_RGBA = 0x07, GPU_MULTIPLY_ADD = 0x08,
    GPU_ADD_MULTIPLY = 0x09,
} GPU_COMBINEFUNC;

typedef enum {
    GPU_TEVSCALE_1 = 0x0, GPU_TEVSCALE_2 = 0x1, GPU_TEVSCALE_4 = 0x2,
} GPU_TEVSCALE;

#define GPU_TEVSOURCES(a, b, c) (((a)) | ((b) << 4) | ((c) << 8))
#define GPU_TEVOPERANDS(a, b, c) (((a)) | ((b) << 4) | ((c) << 8))

typedef enum {
    GPU_TRIANGLES = 0x0000, GPU_TRIANGLE_STRIP = 0x0100,
    GPU_TRIANGLE_FAN = 0x0200, GPU_GEOMETRY_PRIM = 0x0300,
} GPU_Primitive_t;

typedef enum {
    GPU_VERTEX_SHADER = 0x0, GPU_GEOMETRY_SHADER = 0x1,
} GPU_SHADER_TYPE;

// Register numbers the port writes through GPUCMD on the 3DS. Here the
// command buffer goes nowhere, except a write of GPUREG_ATTRIBBUFFER0_OFFSET
// (georender.c moves buffer 0 with it), which the GL layer applies.
enum {
    GPUREG_FRAMEBUFFER_DIM = 0x011E, GPUREG_COLORBUFFER_LOC = 0x011D,
    GPUREG_DEPTHBUFFER_LOC = 0x011C, GPUREG_EARLYDEPTH_FUNC = 0x0061,
    GPUREG_EARLYDEPTH_TEST1 = 0x0062, GPUREG_EARLYDEPTH_CLEAR = 0x0063,
    GPUREG_EARLYDEPTH_DATA = 0x006A, GPUREG_EARLYDEPTH_TEST2 = 0x0118,
    GPUREG_SCISSORTEST_MODE = 0x0065, GPUREG_FRAGOP_ALPHA_TEST = 0x0104,
    GPUREG_TEXUNIT_CONFIG = 0x0080, GPUREG_ATTRIBBUFFER0_OFFSET = 0x0203,
    GPUREG_INDEXBUFFER_CONFIG = 0x0227, GPUREG_NUMVERTICES = 0x0228,
    GPUREG_GEOSTAGE_CONFIG = 0x0229, GPUREG_VERTEX_OFFSET = 0x022A,
    GPUREG_START_DRAW_FUNC0 = 0x022E, GPUREG_DRAWARRAYS = 0x022E,
    GPUREG_DRAWELEMENTS = 0x022F, GPUREG_VTX_FUNC = 0x0232,
    GPUREG_GEOSTAGE_CONFIG2 = 0x0245, GPUREG_PRIMITIVE_CONFIG = 0x025E,
    GPUREG_RESTART_PRIMITIVE = 0x025F,
    GPUREG_GSH_FLOATUNIFORM_CONFIG = 0x0290,
    GPUREG_VSH_FLOATUNIFORM_CONFIG = 0x02C0,
};

#define GPUCMD_HEADER(incremental, mask, reg) \
    (((incremental) << 31) | (((mask) & 0xF) << 16) | ((reg) & 0x3FF))
void GPUCMD_Add(u32 header, const u32* param, u32 paramlength);
#define GPUCMD_AddWrite(reg, val) \
    do { u32 _v = (val); GPUCMD_Add(GPUCMD_HEADER(0, 0xF, (reg)), &_v, 1); } while (0)
#define GPUCMD_AddMaskedWrite(reg, mask, val) \
    do { u32 _v = (val); GPUCMD_Add(GPUCMD_HEADER(0, (mask), (reg)), &_v, 1); } while (0)
#define GPUCMD_AddIncrementalWrites(reg, vals, num) GPUCMD_Add(GPUCMD_HEADER(1, 0xF, (reg)), (vals), (num))
void GPUCMD_SetBuffer(u32* adr, u32 size, u32 offset);
void GPUCMD_GetBuffer(u32** adr, u32* size, u32* offset);

// ------------------------------------------------------------ maths

typedef union {
    struct { float w, z, y, x; };
    struct { float r, k, j, i; };
    float c[4];
} C3D_FVec;
typedef C3D_FVec C3D_FQuat;
typedef union { C3D_FVec r[4]; float m[4 * 4]; } C3D_Mtx;

#define M_TAU (6.28318530717958647692528676655900576)
#define C3D_Angle(_angle) ((_angle) * (float)M_TAU)
#define C3D_AngleFromDegrees(_angle) ((_angle) * (float)M_TAU / 360.0f)
#define C3D_AspectRatioTop (400.0f / 240.0f)
#define C3D_AspectRatioBot (320.0f / 240.0f)

static inline C3D_FVec FVec4_New(float x, float y, float z, float w)
{
    C3D_FVec v; v.x = x; v.y = y; v.z = z; v.w = w; return v;
}
static inline C3D_FVec FVec3_New(float x, float y, float z)
{
    return FVec4_New(x, y, z, 0.0f);
}
static inline void Mtx_Zeros(C3D_Mtx* out) { memset(out, 0, sizeof(*out)); }
static inline void Mtx_Copy(C3D_Mtx* out, const C3D_Mtx* in) { *out = *in; }
void Mtx_Identity(C3D_Mtx* out);
void Mtx_Multiply(C3D_Mtx* out, const C3D_Mtx* a, const C3D_Mtx* b);
void Mtx_Translate(C3D_Mtx* mtx, float x, float y, float z, bool bRightSide);
void Mtx_Scale(C3D_Mtx* mtx, float x, float y, float z);
void Mtx_RotateX(C3D_Mtx* mtx, float angle, bool bRightSide);
void Mtx_RotateY(C3D_Mtx* mtx, float angle, bool bRightSide);
void Mtx_RotateZ(C3D_Mtx* mtx, float angle, bool bRightSide);
void Mtx_Ortho(C3D_Mtx* mtx, float left, float right, float bottom, float top,
               float near, float far, bool isLeftHanded);
void Mtx_Persp(C3D_Mtx* mtx, float fovy, float aspect, float near, float far,
               bool isLeftHanded);
// The 3DS's screens are portrait panels turned on their side; these add
// that quarter turn there. The R36S's are not, so here they are the plain
// projections with fovx and fovy swapped back (see c3d_maths.c).
void Mtx_OrthoTilt(C3D_Mtx* mtx, float left, float right, float bottom,
                   float top, float near, float far, bool isLeftHanded);
void Mtx_PerspTilt(C3D_Mtx* mtx, float fovy, float aspect, float near,
                   float far, bool isLeftHanded);

// ------------------------------------------------------------ shaders

// A "shader binary" on the R36S (r36s/scripts/gen_embedded.py): per DVLE,
// the .v.pica's name, which picks its GLSL twin, and the uniforms it
// declares with the registers picasso would give them.
typedef struct {
    char kind;          // 'f', 'i' or 'b'
    u8   reg;
    u8   count;
    char name[29];
} R36SPicaUniform;

typedef struct DVLE_s {
    char             name[32];
    int              numUniforms;
    R36SPicaUniform* uniforms;
    void*            r36s_program;   // the GL layer's, made on first bind
} DVLE_s;

typedef struct DVLB_s {
    u32     numDVLE;
    DVLE_s* DVLE;
} DVLB_s;

typedef struct {
    C3D_FVec v;
} float24Uniform_s;

typedef struct shaderInstance_s {
    DVLE_s*           dvle;
    u16               boolUniforms;
    u16               boolUniformMask;
    u32               intUniforms[4];
    float24Uniform_s* float24Uniforms;
    u8                intUniformMask;
    u8                numFloat24Uniforms;
} shaderInstance_s;

typedef struct shaderProgram_s {
    shaderInstance_s* vertexShader;
    shaderInstance_s* geometryShader;
    u32               geoShaderInputPermutation[2];
    u8                geoShaderInputStride;
} shaderProgram_s;

DVLB_s* DVLB_ParseFile(u32* shbinData, u32 shbinSize);
void    DVLB_Free(DVLB_s* dvlb);
Result  shaderProgramInit(shaderProgram_s* sp);
Result  shaderProgramFree(shaderProgram_s* sp);
Result  shaderProgramSetVsh(shaderProgram_s* sp, DVLE_s* dvle);
Result  shaderProgramSetGsh(shaderProgram_s* sp, DVLE_s* dvle, u8 stride);
s8      shaderInstanceGetUniformLocation(shaderInstance_s* si, const char* name);

// --------------------------------------------------------- uniforms

#define C3D_FVUNIF_COUNT 96
#define C3D_IVUNIF_COUNT 4

extern C3D_FVec C3D_FVUnif[2][C3D_FVUNIF_COUNT];
extern C3D_FVec C3D_IVUnif[2][C3D_IVUNIF_COUNT];
extern u16      C3D_BoolUnifs[2];
extern bool     C3D_FVUnifDirty[2][C3D_FVUNIF_COUNT];
extern bool     C3D_IVUnifDirty[2][C3D_IVUNIF_COUNT];
extern bool     C3D_BoolUnifsDirty[2];

static inline C3D_FVec* C3D_FVUnifWritePtr(GPU_SHADER_TYPE type, int id, int size)
{
    int i;
    for (i = 0; i < size; i++)
        C3D_FVUnifDirty[type][id + i] = true;
    return &C3D_FVUnif[type][id];
}
static inline void C3D_FVUnifMtxNx4(GPU_SHADER_TYPE type, int id,
                                    const C3D_Mtx* mtx, int num)
{
    int i;
    C3D_FVec* ptr = C3D_FVUnifWritePtr(type, id, num);
    for (i = 0; i < num; i++)
        ptr[i] = mtx->r[i];
}
static inline void C3D_FVUnifMtx4x4(GPU_SHADER_TYPE type, int id, const C3D_Mtx* mtx)
{
    C3D_FVUnifMtxNx4(type, id, mtx, 4);
}
static inline void C3D_FVUnifMtx3x4(GPU_SHADER_TYPE type, int id, const C3D_Mtx* mtx)
{
    C3D_FVUnifMtxNx4(type, id, mtx, 3);
}
static inline void C3D_FVUnifSet(GPU_SHADER_TYPE type, int id, float x, float y,
                                 float z, float w)
{
    C3D_FVec* ptr = C3D_FVUnifWritePtr(type, id, 1);
    ptr->x = x; ptr->y = y; ptr->z = z; ptr->w = w;
}
// As citro3d's: a location from shaderInstanceGetUniformLocation is the
// register less 0x10, so c0 is 0, i0 0x60 and b0 0x68.
static inline void C3D_BoolUnifSet(GPU_SHADER_TYPE type, int id, bool value)
{
    id -= 0x68;
    C3D_BoolUnifsDirty[type] = true;
    if (value)
        C3D_BoolUnifs[type] |= BIT(id);
    else
        C3D_BoolUnifs[type] &= ~BIT(id);
}
void C3D_UpdateUniforms(GPU_SHADER_TYPE type);

// --------------------------------------------------- attributes, buffers

typedef struct {
    u32 flags[2];
    u64 permutation;
    int attrCount;
} C3D_AttrInfo;
void AttrInfo_Init(C3D_AttrInfo* info);
int  AttrInfo_AddLoader(C3D_AttrInfo* info, int regId, GPU_FORMATS format, int count);
int  AttrInfo_AddFixed(C3D_AttrInfo* info, int regId);
C3D_AttrInfo* C3D_GetAttrInfo(void);
void C3D_SetAttrInfo(C3D_AttrInfo* info);

typedef struct {
    u32 offset;
    u32 flags[2];
} C3D_BufCfg;
typedef struct {
    u32        base_paddr;
    int        bufCount;
    C3D_BufCfg buffers[12];
    // The R36S's: the virtual address of each buffer, which the GL layer
    // reads vertices from (on the 3DS the GPU reads physical memory).
    const void* r36s_vaddr[12];
} C3D_BufInfo;
void BufInfo_Init(C3D_BufInfo* info);
int  BufInfo_Add(C3D_BufInfo* info, const void* data, ptrdiff_t stride,
                 int attribCount, u64 permutation);
C3D_BufInfo* C3D_GetBufInfo(void);
void C3D_SetBufInfo(C3D_BufInfo* info);

// ------------------------------------------------------- texture env

typedef struct {
    u16 srcRgb, srcAlpha;
    union {
        u32 opAll;
        struct { u32 opRgb : 12, opAlpha : 12; };
    };
    u16 funcRgb, funcAlpha;
    u32 color;
    u16 scaleRgb, scaleAlpha;
} C3D_TexEnv;

typedef enum { C3D_RGB = BIT(0), C3D_Alpha = BIT(1), C3D_Both = C3D_RGB | C3D_Alpha } C3D_TexEnvMode;

C3D_TexEnv* C3D_GetTexEnv(int id);
void C3D_SetTexEnv(int id, C3D_TexEnv* env);
void C3D_DirtyTexEnv(C3D_TexEnv* env);
void C3D_TexEnvBufUpdate(int mode, int mask);
void C3D_TexEnvBufColor(u32 color);

static inline void C3D_TexEnvInit(C3D_TexEnv* env)
{
    env->srcRgb = GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0);
    env->srcAlpha = env->srcRgb;
    env->opAll = 0;
    env->funcRgb = GPU_REPLACE;
    env->funcAlpha = env->funcRgb;
    env->color = 0xFFFFFFFF;
    env->scaleRgb = GPU_TEVSCALE_1;
    env->scaleAlpha = GPU_TEVSCALE_1;
}
#define C3D_TexEnvSrc(env, mode, s1, ...) _C3D_TexEnvSrc((env), (mode), (s1), ##__VA_ARGS__, 0, 0)
#define _C3D_TexEnvSrc(env, mode, s1, s2, s3, ...) __C3D_TexEnvSrc((env), (mode), (s1), (s2), (s3))
static inline void __C3D_TexEnvSrc(C3D_TexEnv* env, C3D_TexEnvMode mode,
                                   int s1, int s2, int s3)
{
    int param = GPU_TEVSOURCES(s1, s2, s3);
    if (mode & C3D_RGB)
        env->srcRgb = (u16)param;
    if (mode & C3D_Alpha)
        env->srcAlpha = (u16)param;
}
#define C3D_TexEnvOpRgb(env, o1, ...) _C3D_TexEnvOpRgb((env), (o1), ##__VA_ARGS__, 0, 0)
#define _C3D_TexEnvOpRgb(env, o1, o2, o3, ...) __C3D_TexEnvOpRgb((env), (o1), (o2), (o3))
static inline void __C3D_TexEnvOpRgb(C3D_TexEnv* env, int o1, int o2, int o3)
{
    env->opRgb = GPU_TEVOPERANDS(o1, o2, o3);
}
#define C3D_TexEnvOpAlpha(env, o1, ...) _C3D_TexEnvOpAlpha((env), (o1), ##__VA_ARGS__, 0, 0)
#define _C3D_TexEnvOpAlpha(env, o1, o2, o3, ...) __C3D_TexEnvOpAlpha((env), (o1), (o2), (o3))
static inline void __C3D_TexEnvOpAlpha(C3D_TexEnv* env, int o1, int o2, int o3)
{
    env->opAlpha = GPU_TEVOPERANDS(o1, o2, o3);
}
static inline void C3D_TexEnvFunc(C3D_TexEnv* env, C3D_TexEnvMode mode,
                                  GPU_COMBINEFUNC param)
{
    if (mode & C3D_RGB)
        env->funcRgb = (u16)param;
    if (mode & C3D_Alpha)
        env->funcAlpha = (u16)param;
}
static inline void C3D_TexEnvColor(C3D_TexEnv* env, u32 color)
{
    env->color = color;
}
static inline void C3D_TexEnvScale(C3D_TexEnv* env, int mode, GPU_TEVSCALE param)
{
    if (mode & C3D_RGB)
        env->scaleRgb = (u16)param;
    if (mode & C3D_Alpha)
        env->scaleAlpha = (u16)param;
}

// ----------------------------------------------------------- textures

typedef struct C3D_TexCube { void* data[6]; } C3D_TexCube;

typedef struct C3D_Tex {
    union {
        void*        data;
        C3D_TexCube* cube;
    };
    GPU_TEXCOLOR fmt : 4;
    size_t       size : 28;
    union {
        u32 dim;
        struct { u16 height; u16 width; };
    };
    u32 param;
    u32 border;
    union {
        u32 lodParam;
        struct { u16 lodBias; u8 maxLevel; u8 minLevel; };
    };
    // The R36S's: the GL texture name, whether `data` changed since it
    // was uploaded (C3D_TexFlush sets it), the `param` last applied to
    // the GL texture, and whether a render target draws into it (then GL
    // holds the image and `data` is not read).
    u32  r36s_gl;
    bool r36s_dirty;
    bool r36s_rt;
    u32  r36s_param;
    // Which owner of r36s_gl this is: the game copies C3D_Tex structs, and
    // a copy whose name was since deleted (and reused) must not touch it.
    u32  r36s_gen;
} C3D_Tex;

typedef struct {
    u16 width;
    u16 height;
    u8  maxLevel : 4;
    GPU_TEXCOLOR format : 4;
    GPU_TEXTURE_MODE_PARAM type : 3;
    bool onVram : 1;
} C3D_TexInitParams;

bool  C3D_TexInitWithParams(C3D_Tex* tex, C3D_TexCube* cube, C3D_TexInitParams p);
void  C3D_TexLoadImage(C3D_Tex* tex, const void* data, GPU_TEXFACE face, int level);
void  C3D_TexGenerateMipmap(C3D_Tex* tex, GPU_TEXFACE face);
void  C3D_TexBind(int unitId, C3D_Tex* tex);
void  C3D_TexFlush(C3D_Tex* tex);
void  C3D_TexDelete(C3D_Tex* tex);
void  C3D_TexShadowParams(bool perspective, float bias);
u32   C3D_TexCalcMaxLevel(u32 width, u32 height);
u32   C3D_TexCalcLevelSize(u32 size, int level);
u32   C3D_TexCalcTotalSize(u32 size, int maxLevel);
void* C3D_TexGetImagePtr(C3D_Tex* tex, void* data, int level, u32* size);

static inline int C3D_TexFmtBpp(GPU_TEXCOLOR fmt)
{
    static const u8 bpp[] = { 32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8 };
    return bpp[fmt & 0xF];
}
static inline bool C3D_TexInit(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format)
{
    C3D_TexInitParams p = { width, height, 0, format, GPU_TEX_2D, false };
    return C3D_TexInitWithParams(tex, NULL, p);
}
static inline bool C3D_TexInitMipmap(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format)
{
    C3D_TexInitParams p = { width, height, (u8)C3D_TexCalcMaxLevel(width, height), format, GPU_TEX_2D, false };
    return C3D_TexInitWithParams(tex, NULL, p);
}
static inline bool C3D_TexInitVRAM(C3D_Tex* tex, u16 width, u16 height, GPU_TEXCOLOR format)
{
    C3D_TexInitParams p = { width, height, 0, format, GPU_TEX_2D, true };
    return C3D_TexInitWithParams(tex, NULL, p);
}
static inline void C3D_TexUpload(C3D_Tex* tex, const void* data)
{
    C3D_TexLoadImage(tex, data, GPU_TEXFACE_2D, 0);
}
static inline void C3D_TexSetFilter(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM magFilter,
                                    GPU_TEXTURE_FILTER_PARAM minFilter)
{
    tex->param &= ~(GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR));
    tex->param |= GPU_TEXTURE_MAG_FILTER(magFilter) | GPU_TEXTURE_MIN_FILTER(minFilter);
}
static inline void C3D_TexSetFilterMipmap(C3D_Tex* tex, GPU_TEXTURE_FILTER_PARAM filter)
{
    tex->param &= ~GPU_TEXTURE_MIP_FILTER(GPU_LINEAR);
    tex->param |= GPU_TEXTURE_MIP_FILTER(filter);
}
static inline void C3D_TexSetWrap(C3D_Tex* tex, GPU_TEXTURE_WRAP_PARAM wrapS,
                                  GPU_TEXTURE_WRAP_PARAM wrapT)
{
    tex->param &= ~(GPU_TEXTURE_WRAP_S(3) | GPU_TEXTURE_WRAP_T(3));
    tex->param |= GPU_TEXTURE_WRAP_S(wrapS) | GPU_TEXTURE_WRAP_T(wrapT);
}
static inline void C3D_TexSetLodBias(C3D_Tex* tex, float lodBias)
{
    int iLodBias = (int)(lodBias * 0x100);
    if (iLodBias > 0xFFF) iLodBias = 0xFFF;
    else if (iLodBias < -0x1000) iLodBias = -0x1000;
    tex->lodBias = (u16)(iLodBias & 0x1FFF);
}
static inline void* C3D_Tex2DGetImagePtr(C3D_Tex* tex, int level, u32* size)
{
    return C3D_TexGetImagePtr(tex, tex->data, level, size);
}

// ------------------------------------------------------ framebuffers

typedef struct {
    void*        colorBuf;
    void*        depthBuf;
    u16          width;
    u16          height;
    GPU_COLORBUF colorFmt;
    GPU_DEPTHBUF depthFmt;
    bool         block32;
    u8           colorMask : 4;
    u8           depthMask : 4;
    // The R36S's: the GL framebuffer object (made on first use), and the
    // render target this is the frame buffer of.
    u32          r36s_fbo;
    void*        r36s_target;
} C3D_FrameBuf;

enum {
    C3D_CLEAR_COLOR = BIT(0), C3D_CLEAR_DEPTH = BIT(1),
    C3D_CLEAR_ALL = C3D_CLEAR_COLOR | C3D_CLEAR_DEPTH,
};
typedef u32 C3D_ClearBits;

u32  C3D_CalcColorBufSize(u32 width, u32 height, GPU_COLORBUF fmt);
u32  C3D_CalcDepthBufSize(u32 width, u32 height, GPU_DEPTHBUF fmt);
C3D_FrameBuf* C3D_GetFrameBuf(void);
void C3D_SetFrameBuf(C3D_FrameBuf* fb);
void C3D_FrameBufClear(C3D_FrameBuf* fb, C3D_ClearBits clearBits, u32 clearColor,
                       u32 clearDepth);
void C3D_FrameBufTransfer(C3D_FrameBuf* fb, gfxScreen_t screen, gfx3dSide_t side,
                          u32 transferFlags);

typedef struct C3D_RenderTarget_tag C3D_RenderTarget;
struct C3D_RenderTarget_tag {
    C3D_RenderTarget *next, *prev;
    C3D_FrameBuf      frameBuf;
    bool              used;
    bool              ownsColor, ownsDepth;
    bool              linked;
    gfxScreen_t       screen;
    gfx3dSide_t       side;
    u32               transferFlags;
    // The R36S's: a texture the target renders into, when made from one
    // (and the cube face, -1 for a 2D one); the GL colour texture and
    // depth renderbuffer; the GL size, which is landscape for a screen's
    // target (the 3DS's are portrait, 240x400 as 400x240).
    C3D_Tex*          r36s_tex;
    int               r36s_face;
    u32               r36s_color;
    u32               r36s_depth;
    int               r36s_w, r36s_h;
};

enum {
    C3D_FRAME_SYNCDRAW = BIT(0),
    C3D_FRAME_NONBLOCK = BIT(1),
};

float C3D_FrameRate(float fps);
bool  C3D_FrameBegin(u8 flags);
bool  C3D_FrameDrawOn(C3D_RenderTarget* target);
void  C3D_FrameSplit(u8 flags);
void  C3D_FrameEnd(u8 flags);
void  C3D_FrameEndHook(void (*hook)(void*), void* param);
void  C3D_FrameSync(void);
u32   C3D_FrameCounter(int id);
float C3D_GetDrawingTime(void);
float C3D_GetProcessingTime(void);

C3D_RenderTarget* C3D_RenderTargetCreate(int width, int height, GPU_COLORBUF colorFmt,
                                         GPU_DEPTHBUF depthFmt);
C3D_RenderTarget* C3D_RenderTargetCreateFromTex(C3D_Tex* tex, GPU_TEXFACE face, int level,
                                                GPU_DEPTHBUF depthFmt);
void C3D_RenderTargetDelete(C3D_RenderTarget* target);
void C3D_RenderTargetSetOutput(C3D_RenderTarget* target, gfxScreen_t screen,
                               gfx3dSide_t side, u32 transferFlags);
static inline void C3D_RenderTargetDetachOutput(C3D_RenderTarget* target)
{
    C3D_RenderTargetSetOutput(NULL, target->screen, target->side, 0);
}
static inline void C3D_RenderTargetClear(C3D_RenderTarget* target, C3D_ClearBits clearBits,
                                         u32 clearColor, u32 clearDepth)
{
    C3D_FrameBufClear(&target->frameBuf, clearBits, clearColor, clearDepth);
}
void C3D_SyncDisplayTransfer(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 flags);
void C3D_SyncTextureCopy(u32* inadr, u32 indim, u32* outadr, u32 outdim, u32 size,
                         u32 flags);
void C3D_SyncMemoryFill(u32* buf0a, u32 buf0v, u32* buf0e, u16 control0, u32* buf1a,
                        u32 buf1v, u32* buf1e, u16 control1);

// ------------------------------------------------------- base state

#define C3D_DEFAULT_CMDBUF_SIZE 0x40000

enum {
    C3D_UNSIGNED_BYTE = 0,
    C3D_UNSIGNED_SHORT = 1,
};

bool C3D_Init(size_t cmdBufSize);
void C3D_Fini(void);
float C3D_GetCmdBufUsage(void);
void C3D_BindProgram(shaderProgram_s* program);
void C3D_SetViewport(u32 x, u32 y, u32 w, u32 h);
void C3D_SetScissor(GPU_SCISSORMODE mode, u32 left, u32 top, u32 right, u32 bottom);
void C3D_DrawArrays(GPU_Primitive_t primitive, int first, int size);
void C3D_DrawElements(GPU_Primitive_t primitive, int count, int type, const void* indices);
void C3D_ImmDrawBegin(GPU_Primitive_t primitive);
void C3D_ImmSendAttrib(float x, float y, float z, float w);
void C3D_ImmDrawEnd(void);

void C3D_DepthMap(bool bIsZBuffer, float zScale, float zOffset);
void C3D_CullFace(GPU_CULLMODE mode);
void C3D_StencilTest(bool enable, GPU_TESTFUNC function, int ref, int inputMask,
                     int writeMask);
void C3D_StencilOp(GPU_STENCILOP sfail, GPU_STENCILOP dfail, GPU_STENCILOP pass);
void C3D_BlendingColor(u32 color);
void C3D_EarlyDepthTest(bool enable, GPU_EARLYDEPTHFUNC function, u32 ref);
void C3D_DepthTest(bool enable, GPU_TESTFUNC function, GPU_WRITEMASK writemask);
void C3D_AlphaTest(bool enable, GPU_TESTFUNC function, int ref);
void C3D_AlphaBlend(GPU_BLENDEQUATION colorEq, GPU_BLENDEQUATION alphaEq,
                    GPU_BLENDFACTOR srcClr, GPU_BLENDFACTOR dstClr,
                    GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha);
void C3D_ColorLogicOp(GPU_LOGICOP op);
void C3D_FragOpMode(GPU_FRAGOPMODE mode);
void C3D_FragOpShadow(float scale, float bias);

// citro3d internals the port's 3DS code names. The R36S's GL layer keeps
// its own state, so these mark it dirty or do nothing.
void C3Di_UpdateContext(void);
void C3Di_DirtyUniforms(GPU_SHADER_TYPE type);
void C3Di_LoadShaderUniforms(shaderInstance_s* si);
bool C3Di_SplitFrame(u32** pBuf, u32* pSize);

#ifdef __cplusplus
}
#endif

#endif
