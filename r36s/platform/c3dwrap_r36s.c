// c3dwrap.h on the R36S. The 3DS's c3dwrap.c writes the PICA's command
// buffer itself (step 13b) and wraps citro3d's internals at link time;
// here citro3d is the GL layer, which tracks its own dirty state, so the
// uniform setters are citro3d's and the draws are left to it.

#include <citro3d.h>

#include "c3dwrap.h"

void c3d_unif_set(int id, float x, float y, float z, float w)
{
    if (id < 0 || id >= C3D_FVUNIF_COUNT) return;
    C3D_FVUnifSet(GPU_VERTEX_SHADER, id, x, y, z, w);
}

void c3d_unif_vec(int id, const float v[4])
{
    if (id < 0 || id >= C3D_FVUNIF_COUNT) return;
    C3D_FVUnifSet(GPU_VERTEX_SHADER, id, v[0], v[1], v[2], v[3]);
}

void c3d_unif_mtx(int id, const C3D_Mtx* m)
{
    if (id < 0 || id + 4 > C3D_FVUNIF_COUNT) return;
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, id, m);
}

void c3d_unif_bool(int id, bool on)
{
    if (id - 0x68 < 0 || id - 0x68 > 15) return;
    C3D_BoolUnifSet(GPU_VERTEX_SHADER, id, on);
}

void c3d_lean_log(void (*fn)(const char* fmt, ...))
{
    if (fn != NULL) fn("c3dwrap: R36S, draws through the GL layer");
}
