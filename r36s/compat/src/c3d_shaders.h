// The GLSL ES 1.00 twins of the port's PICA200 vertex shaders
// (3DS Files/source/*.v.pica), picked by the shader binary's name. Each
// is the .v.pica's instructions in order, with its uniform names, so the
// game's uniform registers (gen_embedded.py, as picasso assigns them) map
// onto them by name. Attributes are v0..v3 at locations 0..3; bytes arrive
// unnormalised, as the PICA's loader gives them.
//
// Every twin ends in r36s_position(): the PICA's clip z runs from -w (near)
// to 0 (far) and its depth is zScale * z / w + zOffset (C3D_DepthMap), so
// gl_Position.z is chosen to give GL's window depth that value.

#ifndef R36S_C3D_SHADERS_H
#define R36S_C3D_SHADERS_H

// Texture coordinates want highp in a fragment shader, which an ES 2 GPU
// needn't have (the Mali-400 hasn't, and refuses the word). Same text as
// highp wherever it exists.
#define R36S_FS_HIGHP \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n" \
    "#define HIGHP highp\n" \
    "#else\n" \
    "#define HIGHP mediump\n" \
    "#endif\n"

#define R36S_VS_COMMON \
    "uniform vec2 uDepth;\n" \
    "varying vec4 vColor;\n" \
    "varying highp vec4 vTex0;\n" \
    "varying highp vec4 vTex1;\n" \
    "vec4 r36s_position(vec4 p) {\n" \
    "    return vec4(p.x, p.y, 2.0 * uDepth.x * p.z + (2.0 * uDepth.y - 1.0) * p.w, p.w);\n" \
    "}\n"

// geosolid.v.pica: position, the fog's s, ST with the pad scale and the
// flip, and Lambert over the vertex colour.
static const char kVsGeoSolid[] =
    "attribute vec4 v0;\n"
    "attribute vec4 v1;\n"
    "attribute vec4 v2;\n"
    "attribute vec4 v3;\n"
    "uniform vec4 mvp[4];\n"
    "uniform vec4 fogModel;\n"
    "uniform vec4 lightModel;\n"
    "uniform vec4 lightClr;\n"
    "uniform vec4 ambientClr;\n"
    "uniform vec4 uvScale;\n"
    R36S_VS_COMMON
    "void main() {\n"
    "    vec4 r0 = vec4(v0.xyz, 1.0);\n"
    "    vec4 p = vec4(dot(mvp[0], r0), dot(mvp[1], r0), dot(mvp[2], r0), dot(mvp[3], r0));\n"
    "    vTex1 = vec4(dot(fogModel, r0), 0.5, 0.0, 0.0);\n"
    "    vec4 r7 = uvScale * v1;\n"
    "    vTex0 = vec4(r7.x, 1.0 - r7.y, 0.0, 0.0);\n"
    "    float r3 = max(0.0, dot(lightModel.xyz, v3.xyz));\n"
    "    vec4 r4 = ambientClr + lightClr * r3;\n"
    "    vColor = r4 * v2;\n"
    "    gl_Position = r36s_position(p);\n"
    "}\n";

// geosolid's instanced twin, for the draws c3d_gles.c groups (one draw for
// every copy of a mesh with the same state): the model's uniforms, mvp's
// four rows, fogModel and lightModel, come per instance at locations 4..9.
// The arithmetic is geosolid's, expression for expression.
static const char kVsGeoSolidInst[] =
    "attribute vec4 v0;\n"
    "attribute vec4 v1;\n"
    "attribute vec4 v2;\n"
    "attribute vec4 v3;\n"
    "attribute vec4 iMvp0;\n"
    "attribute vec4 iMvp1;\n"
    "attribute vec4 iMvp2;\n"
    "attribute vec4 iMvp3;\n"
    "attribute vec4 iFog;\n"
    "attribute vec4 iLight;\n"
    "uniform vec4 lightClr;\n"
    "uniform vec4 ambientClr;\n"
    "uniform vec4 uvScale;\n"
    R36S_VS_COMMON
    "void main() {\n"
    "    vec4 r0 = vec4(v0.xyz, 1.0);\n"
    "    vec4 p = vec4(dot(iMvp0, r0), dot(iMvp1, r0), dot(iMvp2, r0), dot(iMvp3, r0));\n"
    "    vTex1 = vec4(dot(iFog, r0), 0.5, 0.0, 0.0);\n"
    "    vec4 r7 = uvScale * v1;\n"
    "    vTex0 = vec4(r7.x, 1.0 - r7.y, 0.0, 0.0);\n"
    "    float r3 = max(0.0, dot(iLight.xyz, v3.xyz));\n"
    "    vec4 r4 = ambientClr + lightClr * r3;\n"
    "    vColor = r4 * v2;\n"
    "    gl_Position = r36s_position(p);\n"
    "}\n";

// geolit.v.pica: three lights, the fast-pow specular, the CBV mix, and
// the environment pass's sphere-map coordinates.
static const char kVsGeoLit[] =
    "attribute vec4 v0;\n"
    "attribute vec4 v1;\n"
    "attribute vec4 v2;\n"
    "attribute vec4 v3;\n"
    "uniform vec4 mvp[4];\n"
    "uniform vec4 fogModel;\n"
    "uniform vec4 uvScale;\n"
    "uniform vec4 lightDir[3];\n"
    "uniform vec4 difClr[3];\n"
    "uniform vec4 specClr[3];\n"
    "uniform vec4 ambient;\n"
    "uniform vec4 eye;\n"
    "uniform vec4 powParams;\n"
    "uniform vec4 envCol[3];\n"
    "uniform vec4 fresnel;\n"
    "uniform bool envPass;\n"
    "uniform bool glossEnv;\n"
    R36S_VS_COMMON
    "void main() {\n"
    "    const vec4 scales = vec4(0.00787402, 0.00392157, 0.0078125, 255.0);\n"
    "    vec4 r0 = vec4(v0.xyz, 1.0);\n"
    "    vec4 p = vec4(dot(mvp[0], r0), dot(mvp[1], r0), dot(mvp[2], r0), dot(mvp[3], r0));\n"
    "    vTex1 = vec4(dot(fogModel, r0), 0.5, 0.0, 0.0);\n"
    "    vec3 r2 = scales.x * v3.xyz;\n"
    "    vec3 r3 = r0.xyz - eye.xyz;\n"
    "    float r4 = dot(r3, r2);\n"
    "    r4 = r4 + r4;\n"
    "    r3 = r3 - r4 * r2;\n"
    "    r3 = inversesqrt(dot(r3, r3)) * r3;\n"
    "    vec3 r6 = max(vec3(0.0), vec3(dot(lightDir[0].xyz, r2), dot(lightDir[1].xyz, r2), dot(lightDir[2].xyz, r2)));\n"
    "    vec3 r7 = max(vec3(powParams.z), vec3(dot(lightDir[0].xyz, r3), dot(lightDir[1].xyz, r3), dot(lightDir[2].xyz, r3)));\n"
    "    vec3 r8 = floor(log2(r7));\n"
    "    vec3 r9 = exp2(-r8) * r7 + r8 - 1.0;\n"
    "    r9 = powParams.x * r9 + powParams.y;\n"
    "    vec3 r10 = floor(r9);\n"
    "    vec3 r11 = 1.0 + (r9 - r10);\n"
    "    r7 = exp2(r10) * r11;\n"
    "    vec4 r13 = ambient;\n"
    "    r13 += difClr[0] * r6.x;\n"
    "    r13 += difClr[1] * r6.y;\n"
    "    r13 += difClr[2] * r6.z;\n"
    "    r13 += specClr[0] * r7.x;\n"
    "    r13 += specClr[1] * r7.y;\n"
    "    r13 += specClr[2] * r7.z;\n"
    "    vec3 r14 = 1.0 + powParams.w * (v2.xyz - scales.w);\n"
    "    r13.xyz = r13.xyz * r14;\n"
    "    r13 = min(vec4(scales.w), max(vec4(0.0), r13));\n"
    "    vec4 r15 = vec4(scales.y * r13.xyz, scales.z * r13.w);\n"
    "    if (envPass) {\n"
    "        vec3 e = vec3(dot(envCol[0].xyz, r3), dot(envCol[1].xyz, r3), dot(envCol[2].xyz, r3));\n"
    "        vec3 s = vec3(e.xy, 1.0 + e.z);\n"
    "        float k = 0.5 * inversesqrt(dot(s, s));\n"
    "        vTex0 = vec4(0.5 + k * e.xy, 0.0, 0.0);\n"
    "        if (glossEnv) {\n"
    "            vColor = r15;\n"
    "        } else {\n"
    "            vColor = vec4(fresnel.zzz, fresnel.x + fresnel.y * e.z);\n"
    "        }\n"
    "    } else {\n"
    "        vec4 t = uvScale * v1;\n"
    "        vTex0 = vec4(t.x, 1.0 - t.y, 0.0, 0.0);\n"
    "        vColor = r15;\n"
    "    }\n"
    "    gl_Position = r36s_position(p);\n"
    "}\n";

// geofold.v.pica: the sphere map's grid, a fixed colour, the cube's
// direction as the texture coordinate.
static const char kVsGeoFold[] =
    "attribute vec4 v0;\n"
    "attribute vec4 v1;\n"
    R36S_VS_COMMON
    "void main() {\n"
    "    vTex0 = vec4(v1.xyz, 0.0);\n"
    "    vTex1 = vec4(0.0);\n"
    "    vColor = vec4(0.75, 0.75, 0.75, 0.5);\n"
    "    gl_Position = r36s_position(vec4(v0.xy, -0.5, 1.0));\n"
    "}\n";

// citro2d's shader, for c2d_gles.c: the scene's projection, the vertex's
// position, texture coordinate, colour and tint blend.
static const char kVsC2D[] =
    "attribute vec4 v0;\n"
    "attribute vec4 v1;\n"
    "attribute vec4 v2;\n"
    "attribute vec4 v3;\n"
    "uniform vec4 mvp[4];\n"
    R36S_VS_COMMON
    "varying float vBlend;\n"
    "void main() {\n"
    "    vec4 r0 = vec4(v0.xyz, 1.0);\n"
    "    vec4 p = vec4(dot(mvp[0], r0), dot(mvp[1], r0), dot(mvp[2], r0), dot(mvp[3], r0));\n"
    "    vTex0 = vec4(v1.xy, 0.0, 0.0);\n"
    "    vTex1 = vec4(0.0);\n"
    "    vBlend = v2.x;\n"
    "    vColor = v3 / 255.0;\n"
    "    gl_Position = r36s_position(p);\n"
    "}\n";

// The presenter: a target's colour texture over a rectangle of the window.
// uUv is the part of the picture the rectangle shows, as offset and size
// (0, 0, 1, 1 for all of it; a negative size mirrors it), and uShade its
// brightness.
static const char kVsPresent[] =
    "attribute vec2 v0;\n"
    "varying vec2 vUv;\n"
    "uniform vec4 uRect;\n"
    "uniform vec4 uUv;\n"
    "uniform vec2 uUvScale;\n"
    "void main() {\n"
    "    vUv = (uUv.xy + v0 * uUv.zw) * uUvScale;\n"
    "    gl_Position = vec4(uRect.xy + v0 * uRect.zw, 0.0, 1.0);\n"
    "}\n";

static const char kFsPresent[] =
    "precision mediump float;\n"
    "varying vec2 vUv;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uShade;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(texture2D(uTex, vUv).rgb * uShade, 1.0);\n"
    "}\n";

// The sun's probe (r36s_sun_probe): a rectangle at the far plane, which
// draws nothing, for an occlusion query to count against the depth buffer.
// The game's depth is the PICA's, 0 at the far plane and what the clear
// leaves, so the far plane's is the lowest window depth, NDC -1.
static const char kVsProbe[] =
    "attribute vec2 v0;\n"
    "uniform vec4 uRect;\n"
    "void main() {\n"
    "    gl_Position = vec4(uRect.xy + v0 * uRect.zw, -1.0, 1.0);\n"
    "}\n";

static const char kFsProbe[] =
    "precision mediump float;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(0.0);\n"
    "}\n";

#endif
