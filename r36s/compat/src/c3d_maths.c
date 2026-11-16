// citro3d's matrix helpers the port uses. Same conventions as citro3d:
// row-major rows r[0..3], each C3D_FVec stored w, z, y, x; the projection
// maps view-space depth onto [-1, 0] clip z/w (near -1, far 0), which the
// GL layer turns into GL's range in its vertex shaders.

#include <math.h>
#include <string.h>

#include <citro3d.h>

void Mtx_Identity(C3D_Mtx* out)
{
    Mtx_Zeros(out);
    out->r[0].x = out->r[1].y = out->r[2].z = out->r[3].w = 1.0f;
}

void Mtx_Multiply(C3D_Mtx* out, const C3D_Mtx* a, const C3D_Mtx* b)
{
    C3D_Mtx t;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += a->r[i].c[3 - k] * b->r[k].c[3 - j];
            t.r[i].c[3 - j] = s;
        }
    }
    *out = t;
}

void Mtx_Translate(C3D_Mtx* mtx, float x, float y, float z, bool bRightSide)
{
    C3D_Mtx t;
    Mtx_Identity(&t);
    t.r[0].w = x;
    t.r[1].w = y;
    t.r[2].w = z;
    if (bRightSide)
        Mtx_Multiply(mtx, mtx, &t);
    else
        Mtx_Multiply(mtx, &t, mtx);
}

void Mtx_Scale(C3D_Mtx* mtx, float x, float y, float z)
{
    for (int i = 0; i < 4; i++) {
        mtx->r[i].x *= x;
        mtx->r[i].y *= y;
        mtx->r[i].z *= z;
    }
}

static void rotate(C3D_Mtx* mtx, int axis, float angle, bool bRightSide)
{
    C3D_Mtx r;
    float s = sinf(angle), c = cosf(angle);
    Mtx_Identity(&r);
    if (axis == 0) {
        r.r[1].y = c; r.r[1].z = -s;
        r.r[2].y = s; r.r[2].z = c;
    } else if (axis == 1) {
        r.r[0].x = c; r.r[0].z = s;
        r.r[2].x = -s; r.r[2].z = c;
    } else {
        r.r[0].x = c; r.r[0].y = -s;
        r.r[1].x = s; r.r[1].y = c;
    }
    if (bRightSide)
        Mtx_Multiply(mtx, mtx, &r);
    else
        Mtx_Multiply(mtx, &r, mtx);
}

void Mtx_RotateX(C3D_Mtx* m, float a, bool r) { rotate(m, 0, a, r); }
void Mtx_RotateY(C3D_Mtx* m, float a, bool r) { rotate(m, 1, a, r); }
void Mtx_RotateZ(C3D_Mtx* m, float a, bool r) { rotate(m, 2, a, r); }

void Mtx_Ortho(C3D_Mtx* mtx, float left, float right, float bottom, float top,
               float near, float far, bool isLeftHanded)
{
    Mtx_Zeros(mtx);
    mtx->r[0].x = 2.0f / (right - left);
    mtx->r[0].w = (left + right) / (left - right);
    mtx->r[1].y = 2.0f / (top - bottom);
    mtx->r[1].w = (bottom + top) / (bottom - top);
    mtx->r[2].z = isLeftHanded ? 1.0f / (far - near) : 1.0f / (near - far);
    mtx->r[2].w = 0.5f * (near + far) / (near - far) - 0.5f;
    mtx->r[3].w = 1.0f;
}

void Mtx_Persp(C3D_Mtx* mtx, float fovy, float aspect, float near, float far,
               bool isLeftHanded)
{
    float fovy_tan = tanf(fovy / 2.0f);
    Mtx_Zeros(mtx);
    mtx->r[0].x = 1.0f / (aspect * fovy_tan);
    mtx->r[1].y = 1.0f / fovy_tan;
    mtx->r[2].w = far * near / (near - far);
    mtx->r[3].z = isLeftHanded ? 1.0f : -1.0f;
    mtx->r[2].z = -mtx->r[3].z * near / (near - far);
}

// The tilted forms draw into the 3DS's portrait framebuffers. The GL
// layer's targets are landscape, so the projection is the untilted one.
void Mtx_OrthoTilt(C3D_Mtx* mtx, float left, float right, float bottom,
                   float top, float near, float far, bool isLeftHanded)
{
    Mtx_Ortho(mtx, left, right, bottom, top, near, far, isLeftHanded);
}

void Mtx_PerspTilt(C3D_Mtx* mtx, float fovy, float aspect, float near,
                   float far, bool isLeftHanded)
{
    Mtx_Persp(mtx, fovy, aspect, near, far, isLeftHanded);
}
