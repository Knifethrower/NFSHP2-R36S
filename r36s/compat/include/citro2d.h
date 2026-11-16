// The subset of citro2d the port uses, for the R36S build: the screen
// targets, solid rectangles and images, drawn by the same GL layer as
// citro3d's draws (r36s/compat/src/c2d_gles.c).

#ifndef R36S_COMPAT_CITRO2D_H
#define R36S_COMPAT_CITRO2D_H

#include "citro3d.h"

#ifdef __cplusplus
extern "C" {
#endif

#define C2D_DEFAULT_MAX_OBJECTS 4096

typedef struct {
    u16   width;
    u16   height;
    float left;
    float top;
    float right;
    float bottom;
} Tex3DS_SubTexture;

typedef struct {
    C3D_Tex*                 tex;
    const Tex3DS_SubTexture* subtex;
} C2D_Image;

typedef struct {
    u32   color;
    float blend;
} C2D_Tint;

typedef enum {
    C2D_TopLeft, C2D_TopRight, C2D_BotLeft, C2D_BotRight,
} C2D_Corner;

typedef struct {
    C2D_Tint corners[4];
} C2D_ImageTint;

typedef enum {
    C2D_TintSolid, C2D_TintMult, C2D_TintLuma,
} C2D_TintMode;

typedef struct {
    struct { float x, y, w, h; } pos;
    struct { float x, y; } center;
    float depth;
    float angle;
} C2D_DrawParams;

static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a)
{
    return r | (g << (u32)8) | (b << (u32)16) | (a << (u32)24);
}
static inline u32 C2D_Color32f(float r, float g, float b, float a)
{
    return C2D_Color32((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), (u8)(a * 255));
}

bool C2D_Init(size_t maxObjects);
void C2D_Fini(void);
void C2D_Prepare(void);
void C2D_Flush(void);
void C2D_SceneSize(u32 width, u32 height, bool tilt);
static inline void C2D_SceneTarget(C3D_RenderTarget* target)
{
    C2D_SceneSize(target->frameBuf.width, target->frameBuf.height, target->linked);
}
static inline void C2D_SceneBegin(C3D_RenderTarget* target)
{
    C2D_Flush();
    C3D_FrameDrawOn(target);
    C2D_SceneTarget(target);
}
C3D_RenderTarget* C2D_CreateScreenTarget(gfxScreen_t screen, gfx3dSide_t side);
void C2D_TargetClear(C3D_RenderTarget* target, u32 color);
void C2D_SetTintMode(C2D_TintMode mode);

void C2D_PlainImageTint(C2D_ImageTint* tint, u32 color, float blend);
void C2D_SetImageTint(C2D_ImageTint* tint, C2D_Corner corner, u32 color, float blend);

bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams* params, const C2D_ImageTint* tint);
bool C2D_DrawRectangle(float x, float y, float z, float w, float h, u32 clr0, u32 clr1,
                       u32 clr2, u32 clr3);
static inline bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 clr)
{
    return C2D_DrawRectangle(x, y, z, w, h, clr, clr, clr, clr);
}
static inline bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth,
                                   const C2D_ImageTint* tint, float scaleX, float scaleY)
{
    C2D_DrawParams params = {
        { x, y, scaleX * img.subtex->width, scaleY * img.subtex->height },
        { 0.0f, 0.0f }, depth, 0.0f
    };
    return C2D_DrawImage(img, &params, tint);
}

#ifdef __cplusplus
}
#endif

#endif
