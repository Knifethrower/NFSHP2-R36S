// The GL thread (docs/R36S_OPTIMISATION_BACKLOG.md, item 4): the GL
// layer's calls, made on the game's thread, are recorded and played on a
// thread of their own, which holds the context. The Mali driver's time per
// draw, about two thirds of the game thread's, then runs on another core
// while the game makes its next frame. See glthread.c.
//
// Included by the GL layer's sources after the GLES headers: each GL
// function they call is one of glthread.c's (rgl_*), which records it, or
// makes it at once when the thread is off or the context is lent to the
// caller.

#ifndef R36S_GLTHREAD_H
#define R36S_GLTHREAD_H

#include <GLES2/gl2.h>
#include <stdbool.h>

#include "r36s_platform.h"

// Starts the thread (after the context is made and the GL layer set up),
// unless NFSHP2_GLTHREAD=0. The context leaves the calling thread.
void rgl_start(void);
// Whether calls are being recorded for the thread.
bool rgl_threaded(void);
void rgl_frame_is(unsigned frame);
// The frame's end: the swap and the frame's counts go to the thread,
// which swaps and logs the frame (r36s_perf_frame_with). Waits while the
// thread is still a frame behind. Without the thread, swaps here.
void rgl_frame_end(const R36sGlCounts* counts);
// Every recorded call made, the context lent to the calling thread for
// calls whose results it needs, and given back.
void rgl_direct_begin(void);
void rgl_direct_end(void);

// GLES 3's instanced draws, resolved at run time: false without them.
bool rgl_instancing(void);
void rgl_DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices,
                               GLsizei instances);
void rgl_DrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances);
void rgl_VertexAttribDivisor(GLuint index, GLuint divisor);

// GLES 3's program binaries and framebuffer invalidation, resolved at run
// time. rgl_direct_active: the context is on the calling thread
// (rgl_direct_begin), or there is no GL thread.
bool rgl_direct_active(void);
bool rgl_program_binaries(void);
bool rgl_invalidation(void);
void rgl_GetProgramBinary(GLuint program, GLsizei size, GLsizei* length, GLenum* format, void* data);
void rgl_ProgramBinary(GLuint program, GLenum format, const void* data, GLsizei length);
void rgl_ProgramParameteri(GLuint program, GLenum name, GLint value);
void rgl_InvalidateDepth(GLenum target, GLenum depth, GLenum stencil);
// glTexSubImage2D of a whole level.
void rgl_TexWhole2D(GLenum target, GLint level, GLsizei w, GLsizei h, GLenum format, GLenum type,
                    const void* pixels);

#define RGL_FUNCS(X) \
    X(void, ActiveTexture, (GLenum a), (a)) \
    X(void, AttachShader, (GLuint a, GLuint b), (a, b)) \
    X(void, BindAttribLocation, (GLuint a, GLuint b, const GLchar* c), (a, b, c)) \
    X(void, BindBuffer, (GLenum a, GLuint b), (a, b)) \
    X(void, BindFramebuffer, (GLenum a, GLuint b), (a, b)) \
    X(void, BindRenderbuffer, (GLenum a, GLuint b), (a, b)) \
    X(void, BindTexture, (GLenum a, GLuint b), (a, b)) \
    X(void, BlendColor, (GLfloat a, GLfloat b, GLfloat c, GLfloat d), (a, b, c, d)) \
    X(void, BlendEquationSeparate, (GLenum a, GLenum b), (a, b)) \
    X(void, BlendFuncSeparate, (GLenum a, GLenum b, GLenum c, GLenum d), (a, b, c, d)) \
    X(void, BufferData, (GLenum a, GLsizeiptr b, const void* c, GLenum d), (a, b, c, d)) \
    X(GLenum, CheckFramebufferStatus, (GLenum a), (a)) \
    X(void, Clear, (GLbitfield a), (a)) \
    X(void, ClearColor, (GLfloat a, GLfloat b, GLfloat c, GLfloat d), (a, b, c, d)) \
    X(void, ClearDepthf, (GLfloat a), (a)) \
    X(void, ClearStencil, (GLint a), (a)) \
    X(void, ColorMask, (GLboolean a, GLboolean b, GLboolean c, GLboolean d), (a, b, c, d)) \
    X(void, CompileShader, (GLuint a), (a)) \
    X(GLuint, CreateProgram, (void), ()) \
    X(GLuint, CreateShader, (GLenum a), (a)) \
    X(void, CullFace, (GLenum a), (a)) \
    X(void, DeleteBuffers, (GLsizei a, const GLuint* b), (a, b)) \
    X(void, DeleteFramebuffers, (GLsizei a, const GLuint* b), (a, b)) \
    X(void, DeleteProgram, (GLuint a), (a)) \
    X(void, DeleteRenderbuffers, (GLsizei a, const GLuint* b), (a, b)) \
    X(void, DeleteShader, (GLuint a), (a)) \
    X(void, DeleteTextures, (GLsizei a, const GLuint* b), (a, b)) \
    X(void, DepthFunc, (GLenum a), (a)) \
    X(void, DepthMask, (GLboolean a), (a)) \
    X(void, Disable, (GLenum a), (a)) \
    X(void, DisableVertexAttribArray, (GLuint a), (a)) \
    X(void, DrawArrays, (GLenum a, GLint b, GLsizei c), (a, b, c)) \
    X(void, DrawElements, (GLenum a, GLsizei b, GLenum c, const void* d), (a, b, c, d)) \
    X(void, Enable, (GLenum a), (a)) \
    X(void, EnableVertexAttribArray, (GLuint a), (a)) \
    X(void, FramebufferRenderbuffer, (GLenum a, GLenum b, GLenum c, GLuint d), (a, b, c, d)) \
    X(void, FramebufferTexture2D, (GLenum a, GLenum b, GLenum c, GLuint d, GLint e), (a, b, c, d, e)) \
    X(void, GenBuffers, (GLsizei a, GLuint* b), (a, b)) \
    X(void, GenFramebuffers, (GLsizei a, GLuint* b), (a, b)) \
    X(void, GenRenderbuffers, (GLsizei a, GLuint* b), (a, b)) \
    X(void, GenTextures, (GLsizei a, GLuint* b), (a, b)) \
    X(GLenum, GetError, (void), ()) \
    X(void, GetIntegerv, (GLenum a, GLint* b), (a, b)) \
    X(void, GetProgramInfoLog, (GLuint a, GLsizei b, GLsizei* c, GLchar* d), (a, b, c, d)) \
    X(void, GetProgramiv, (GLuint a, GLenum b, GLint* c), (a, b, c)) \
    X(void, GetShaderInfoLog, (GLuint a, GLsizei b, GLsizei* c, GLchar* d), (a, b, c, d)) \
    X(void, GetShaderiv, (GLuint a, GLenum b, GLint* c), (a, b, c)) \
    X(const GLubyte*, GetString, (GLenum a), (a)) \
    X(GLint, GetUniformLocation, (GLuint a, const GLchar* b), (a, b)) \
    X(GLboolean, IsEnabled, (GLenum a), (a)) \
    X(void, LinkProgram, (GLuint a), (a)) \
    X(void, PixelStorei, (GLenum a, GLint b), (a, b)) \
    X(void, ReadPixels, (GLint a, GLint b, GLsizei c, GLsizei d, GLenum e, GLenum f, void* g), (a, b, c, d, e, f, g)) \
    X(void, RenderbufferStorage, (GLenum a, GLenum b, GLsizei c, GLsizei d), (a, b, c, d)) \
    X(void, ShaderSource, (GLuint a, GLsizei b, const GLchar* const* c, const GLint* d), (a, b, c, d)) \
    X(void, TexImage2D, (GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void* i), (a, b, c, d, e, f, g, h, i)) \
    X(void, TexParameteri, (GLenum a, GLenum b, GLint c), (a, b, c)) \
    X(void, Uniform1f, (GLint a, GLfloat b), (a, b)) \
    X(void, Uniform1i, (GLint a, GLint b), (a, b)) \
    X(void, Uniform2f, (GLint a, GLfloat b, GLfloat c), (a, b, c)) \
    X(void, Uniform4f, (GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e), (a, b, c, d, e)) \
    X(void, Uniform4fv, (GLint a, GLsizei b, const GLfloat* c), (a, b, c)) \
    X(void, UseProgram, (GLuint a), (a)) \
    X(void, VertexAttribPointer, (GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void* f), (a, b, c, d, e, f)) \
    X(void, Viewport, (GLint a, GLint b, GLsizei c, GLsizei d), (a, b, c, d))

#define RGL_DECLARE(ret, name, params, args) ret rgl_##name params;
RGL_FUNCS(RGL_DECLARE)
#undef RGL_DECLARE

#ifndef RGL_NO_REDIRECT
#define glActiveTexture rgl_ActiveTexture
#define glAttachShader rgl_AttachShader
#define glBindAttribLocation rgl_BindAttribLocation
#define glBindBuffer rgl_BindBuffer
#define glBindFramebuffer rgl_BindFramebuffer
#define glBindRenderbuffer rgl_BindRenderbuffer
#define glBindTexture rgl_BindTexture
#define glBlendColor rgl_BlendColor
#define glBlendEquationSeparate rgl_BlendEquationSeparate
#define glBlendFuncSeparate rgl_BlendFuncSeparate
#define glBufferData rgl_BufferData
#define glCheckFramebufferStatus rgl_CheckFramebufferStatus
#define glClear rgl_Clear
#define glClearColor rgl_ClearColor
#define glClearDepthf rgl_ClearDepthf
#define glClearStencil rgl_ClearStencil
#define glColorMask rgl_ColorMask
#define glCompileShader rgl_CompileShader
#define glCreateProgram rgl_CreateProgram
#define glCreateShader rgl_CreateShader
#define glCullFace rgl_CullFace
#define glDeleteBuffers rgl_DeleteBuffers
#define glDeleteFramebuffers rgl_DeleteFramebuffers
#define glDeleteProgram rgl_DeleteProgram
#define glDeleteRenderbuffers rgl_DeleteRenderbuffers
#define glDeleteShader rgl_DeleteShader
#define glDeleteTextures rgl_DeleteTextures
#define glDepthFunc rgl_DepthFunc
#define glDepthMask rgl_DepthMask
#define glDisable rgl_Disable
#define glDisableVertexAttribArray rgl_DisableVertexAttribArray
#define glDrawArrays rgl_DrawArrays
#define glDrawElements rgl_DrawElements
#define glEnable rgl_Enable
#define glEnableVertexAttribArray rgl_EnableVertexAttribArray
#define glFramebufferRenderbuffer rgl_FramebufferRenderbuffer
#define glFramebufferTexture2D rgl_FramebufferTexture2D
#define glGenBuffers rgl_GenBuffers
#define glGenFramebuffers rgl_GenFramebuffers
#define glGenRenderbuffers rgl_GenRenderbuffers
#define glGenTextures rgl_GenTextures
#define glGetError rgl_GetError
#define glGetIntegerv rgl_GetIntegerv
#define glGetProgramInfoLog rgl_GetProgramInfoLog
#define glGetProgramiv rgl_GetProgramiv
#define glGetShaderInfoLog rgl_GetShaderInfoLog
#define glGetShaderiv rgl_GetShaderiv
#define glGetString rgl_GetString
#define glGetUniformLocation rgl_GetUniformLocation
#define glIsEnabled rgl_IsEnabled
#define glLinkProgram rgl_LinkProgram
#define glPixelStorei rgl_PixelStorei
#define glReadPixels rgl_ReadPixels
#define glRenderbufferStorage rgl_RenderbufferStorage
#define glShaderSource rgl_ShaderSource
#define glTexImage2D rgl_TexImage2D
#define glTexParameteri rgl_TexParameteri
#define glUniform1f rgl_Uniform1f
#define glUniform1i rgl_Uniform1i
#define glUniform2f rgl_Uniform2f
#define glUniform4f rgl_Uniform4f
#define glUniform4fv rgl_Uniform4fv
#define glUseProgram rgl_UseProgram
#define glVertexAttribPointer rgl_VertexAttribPointer
#define glViewport rgl_Viewport
#endif

#endif
