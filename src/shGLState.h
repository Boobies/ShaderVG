/*
 * Copyright (c) 2007 Ivan Leben
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library in the file COPYING;
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef SH_GL_STATE_H
#define SH_GL_STATE_H

#include "shDefs.h"

typedef struct
{
  GLint framebuffer;
  GLint renderbuffer;
  GLint drawBuffer;
  GLint readBuffer;
} SHGLFramebufferState;

typedef struct
{
  GLint viewport[4];
} SHGLViewportState;

typedef struct
{
  GLint program;
} SHGLProgramState;

typedef struct
{
  GLint vertexArray;
  GLint arrayBuffer;
} SHGLVertexBindingState;

typedef struct
{
  GLint activeTexture;
} SHGLActiveTextureState;

typedef struct
{
  GLenum unit;
  GLint binding;
} SHGLTextureBindingState;

typedef struct
{
  GLboolean blend;
  GLboolean depth;
  GLboolean stencil;
} SHGLCapabilityState;

typedef struct
{
  GLboolean enabled;
  GLint box[4];
} SHGLScissorState;

typedef struct
{
  GLint srcRgb;
  GLint dstRgb;
  GLint srcAlpha;
  GLint dstAlpha;
  GLint equationRgb;
  GLint equationAlpha;
} SHGLBlendState;

typedef struct
{
  GLint func;
  GLint ref;
  GLint valueMask;
  GLint fail;
  GLint passDepthFail;
  GLint passDepthPass;
  GLint writeMask;
  GLint clearValue;
} SHGLStencilState;

typedef struct
{
  GLfloat clearColor[4];
  GLboolean colorMask[4];
} SHGLColorState;

typedef struct
{
  GLint alignment;
  GLint rowLength;
  GLint skipPixels;
  GLint skipRows;
} SHGLUnpackState;

typedef struct
{
  GLint alignment;
  GLint rowLength;
  GLint skipPixels;
  GLint skipRows;
} SHGLPackState;

void shSaveFramebufferState(SHGLFramebufferState *state);
void shRestoreFramebufferState(const SHGLFramebufferState *state);
void shSaveViewportState(SHGLViewportState *state);
void shRestoreViewportState(const SHGLViewportState *state);
void shSaveProgramState(SHGLProgramState *state);
void shRestoreProgramState(const SHGLProgramState *state);
void shSaveVertexBindingState(SHGLVertexBindingState *state);
void shRestoreVertexBindingState(const SHGLVertexBindingState *state);
void shSaveActiveTextureState(SHGLActiveTextureState *state);
void shRestoreActiveTextureState(const SHGLActiveTextureState *state);
void shSaveTextureBindingState(SHGLTextureBindingState *state, GLenum unit);
void shRestoreTextureBindingState(const SHGLTextureBindingState *state);
void shSaveCapabilityState(SHGLCapabilityState *state);
void shRestoreCapabilityState(const SHGLCapabilityState *state);
void shSaveScissorState(SHGLScissorState *state);
void shRestoreScissorState(const SHGLScissorState *state);
void shSaveBlendState(SHGLBlendState *state);
void shRestoreBlendState(const SHGLBlendState *state);
void shSaveStencilState(SHGLStencilState *state);
void shRestoreStencilState(const SHGLStencilState *state);
void shSaveColorState(SHGLColorState *state);
void shRestoreColorState(const SHGLColorState *state);
void shSaveUnpackState(SHGLUnpackState *state);
void shRestoreUnpackState(const SHGLUnpackState *state);
void shSavePackState(SHGLPackState *state);
void shRestorePackState(const SHGLPackState *state);

#endif /* SH_GL_STATE_H */
