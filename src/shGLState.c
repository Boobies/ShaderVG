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

#include "shGLState.h"

void shSaveFramebufferState(SHGLFramebufferState *state)
{
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &state->renderbuffer);
  glGetIntegerv(GL_DRAW_BUFFER, &state->drawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &state->readBuffer);
}

void shRestoreFramebufferState(const SHGLFramebufferState *state)
{
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)state->framebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)state->renderbuffer);
  glDrawBuffer((GLenum)state->drawBuffer);
  glReadBuffer((GLenum)state->readBuffer);
}

void shSaveViewportState(SHGLViewportState *state)
{
  glGetIntegerv(GL_VIEWPORT, state->viewport);
}

void shRestoreViewportState(const SHGLViewportState *state)
{
  glViewport(state->viewport[0], state->viewport[1],
             state->viewport[2], state->viewport[3]);
}

void shSaveProgramState(SHGLProgramState *state)
{
  glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
}

void shRestoreProgramState(const SHGLProgramState *state)
{
  glUseProgram((GLuint)state->program);
}

void shSaveVertexBindingState(SHGLVertexBindingState *state)
{
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vertexArray);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->arrayBuffer);
}

void shRestoreVertexBindingState(const SHGLVertexBindingState *state)
{
  if (state->vertexArray == 0 ||
      glIsVertexArray((GLuint)state->vertexArray))
    glBindVertexArray((GLuint)state->vertexArray);
  else
    glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->arrayBuffer);
}

void shSaveActiveTextureState(SHGLActiveTextureState *state)
{
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state->activeTexture);
}

void shRestoreActiveTextureState(const SHGLActiveTextureState *state)
{
  glActiveTexture((GLenum)state->activeTexture);
}

void shSaveTextureBindingState(SHGLTextureBindingState *state, GLenum unit)
{
  state->unit = unit;
  glActiveTexture(unit);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->binding);
}

void shRestoreTextureBindingState(const SHGLTextureBindingState *state)
{
  glActiveTexture(state->unit);
  glBindTexture(GL_TEXTURE_2D, (GLuint)state->binding);
}

void shSaveCapabilityState(SHGLCapabilityState *state)
{
  state->blend = glIsEnabled(GL_BLEND);
  state->depth = glIsEnabled(GL_DEPTH_TEST);
  state->stencil = glIsEnabled(GL_STENCIL_TEST);
}

void shRestoreCapabilityState(const SHGLCapabilityState *state)
{
  if (state->blend) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);

  if (state->depth) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);

  if (state->stencil) glEnable(GL_STENCIL_TEST);
  else glDisable(GL_STENCIL_TEST);
}

void shSaveScissorState(SHGLScissorState *state)
{
  state->enabled = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_SCISSOR_BOX, state->box);
}

void shRestoreScissorState(const SHGLScissorState *state)
{
  if (state->enabled) glEnable(GL_SCISSOR_TEST);
  else glDisable(GL_SCISSOR_TEST);

  glScissor(state->box[0], state->box[1], state->box[2], state->box[3]);
}

void shSaveBlendState(SHGLBlendState *state)
{
  glGetIntegerv(GL_BLEND_SRC_RGB, &state->srcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &state->dstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &state->srcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &state->dstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &state->equationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state->equationAlpha);
}

void shRestoreBlendState(const SHGLBlendState *state)
{
  glBlendFuncSeparate((GLenum)state->srcRgb, (GLenum)state->dstRgb,
                      (GLenum)state->srcAlpha, (GLenum)state->dstAlpha);
  glBlendEquationSeparate((GLenum)state->equationRgb,
                          (GLenum)state->equationAlpha);
}

void shSaveStencilState(SHGLStencilState *state)
{
  glGetIntegerv(GL_STENCIL_FUNC, &state->func);
  glGetIntegerv(GL_STENCIL_REF, &state->ref);
  glGetIntegerv(GL_STENCIL_VALUE_MASK, &state->valueMask);
  glGetIntegerv(GL_STENCIL_FAIL, &state->fail);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state->passDepthFail);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state->passDepthPass);
  glGetIntegerv(GL_STENCIL_WRITEMASK, &state->writeMask);
  glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &state->clearValue);
}

void shRestoreStencilState(const SHGLStencilState *state)
{
  glStencilFunc((GLenum)state->func, state->ref,
                (GLuint)state->valueMask);
  glStencilOp((GLenum)state->fail, (GLenum)state->passDepthFail,
              (GLenum)state->passDepthPass);
  glStencilMask((GLuint)state->writeMask);
  glClearStencil(state->clearValue);
}

void shSaveColorState(SHGLColorState *state)
{
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clearColor);
  glGetBooleanv(GL_COLOR_WRITEMASK, state->colorMask);
}

void shRestoreColorState(const SHGLColorState *state)
{
  glColorMask(state->colorMask[0], state->colorMask[1],
              state->colorMask[2], state->colorMask[3]);
  glClearColor(state->clearColor[0], state->clearColor[1],
               state->clearColor[2], state->clearColor[3]);
}

void shSaveUnpackState(SHGLUnpackState *state)
{
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->alignment);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state->rowLength);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &state->skipPixels);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state->skipRows);
}

void shRestoreUnpackState(const SHGLUnpackState *state)
{
  glPixelStorei(GL_UNPACK_ALIGNMENT, state->alignment);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, state->rowLength);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, state->skipPixels);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, state->skipRows);
}

void shSavePackState(SHGLPackState *state)
{
  glGetIntegerv(GL_PACK_ALIGNMENT, &state->alignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &state->rowLength);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &state->skipPixels);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &state->skipRows);
}

void shRestorePackState(const SHGLPackState *state)
{
  glPixelStorei(GL_PACK_ALIGNMENT, state->alignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, state->rowLength);
  glPixelStorei(GL_PACK_SKIP_PIXELS, state->skipPixels);
  glPixelStorei(GL_PACK_SKIP_ROWS, state->skipRows);
}
