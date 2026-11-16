// The GL layer's own interface between citro3d's (c3d_gles.c) and
// citro2d's (c2d_gles.c) halves: one GL state, one set of targets and
// textures.

#ifndef R36S_GL_H
#define R36S_GL_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include <stdint.h>

#include <citro3d.h>

// Whether the GL layer draws at all (a context exists).
bool r36sgl_active(void);

// citro2d's batch, flushed before any citro3d draw, clear, target change
// or frame end, so the two keep the order the game drew in.
void r36sgl_set_flush2d(void (*flush)(void));
void r36sgl_flush2d(void);

// Binds the current target's framebuffer and viewport and applies the
// fixed-function state citro3d holds (depth, blend, cull, masks). False
// when there is nothing to draw on.
bool r36sgl_begin_draw(void);

// Makes `prog` current (cached).
void r36sgl_use_program(GLuint prog);

// Binds `tex` on `unit`, uploading it first if its memory changed. The
// texture's GL target (GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP), or 0 for
// none.
GLenum r36sgl_bind_texture(int unit, C3D_Tex* tex);

// glVertexAttribPointer, skipped when GL already has that pointer with the
// same array buffer; and forgetting them all (after buffers are deleted,
// or GL's state is unknown).
void r36sgl_attrib_pointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride,
                           const void* ptr);
void r36sgl_attrib_forget(void);
// Enables exactly the vertex attribute arrays in `mask` (bit n is
// location n).
void r36sgl_attrib_mask(unsigned mask);

// The current depth map (C3D_DepthMap): zScale, zOffset.
void r36sgl_depth_map(float out[2]);

// Compiles and links a vertex/fragment pair, attributes v0..v3 at 0..3.
// 0 on failure, with the log printed.
GLuint r36sgl_link(const char* vs, const char* fs, const char* what);

// The baked meshes' GPU buffers (c3d_buffers.c). For a draw's vertex
// buffer at `base`: binds its GL buffer and returns the pointer to give
// glVertexAttribPointer (0, the buffer's start), or binds none and returns
// `base` for a client array. `only`: it's the draw's one buffer, so its
// indices may come from the block too.
const void* r36sgl_vertex_source(const void* base, bool only);
// The draw's indices: an offset in the block's index buffer, bound, or the
// client pointer with none bound.
const void* r36sgl_index_source(const void* indices);
// For a draw that waits to be grouped: the GL buffers of the mesh block at
// `base`, and with `indices` in the block, their offset in its index
// buffer (NULL indices: an array draw). Makes or refreshes the block's
// buffers first. False when `base` isn't a mesh block or `indices` aren't
// in it.
bool r36sgl_mesh_ref(const void* base, const void* indices, GLuint* vbo, GLuint* ibo,
                     uintptr_t* ioff);
// Whether the block at `base` would be uploaded at its next use (so held
// draws must go first).
bool r36sgl_mesh_dirty(const void* base);
// The buffer bindings, cached with the draws' own; the array buffer bound
// (-1 when unknown).
void r36sgl_bind_array(GLuint name);
GLint r36sgl_bound_array(void);
void r36sgl_bind_element(GLuint name);
// Draws the draws held for grouping (c3d_gles.c), before anything that
// must follow them: citro2d's batch, for one.
void r36sgl_flush_held(void);
// No buffers bound, for a draw from client arrays (citro2d, the presenter,
// the frame counter).
void r36sgl_client_arrays(void);
// GL's bindings are unknown (the state cache's reset).
void r36sgl_buffers_unknown(void);
// Once a frame on the GL thread: deletes forgotten blocks' buffers, and
// with NFSHP2_VBO=2 checks every block.
void r36sgl_buffers_frame(void);

// NFSHP2_GLDEBUG's count of citro2d's quads.
void r36sgl_debug_2d(int quads);
// Whether this frame is one NFSHP2_GLDEBUG reports in detail.
bool r36sgl_debug_frame(void);

#endif
