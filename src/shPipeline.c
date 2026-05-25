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

#define VG_API_EXPORT
#include "openvg.h"
#include "shDefs.h"
#include "shExtensions.h"
#include "shContext.h"
#include "shPath.h"
#include "shImage.h"
#include "shPipeline.h"
#include "shGeometry.h"
#include "shPaint.h"

void shPremultiplyFramebuffer()
{
  /* Multiply target color with its own alpha */
  glBlendFunc(GL_ZERO, GL_DST_ALPHA);
}

void shUnpremultiplyFramebuffer()
{
  /* TODO: hmmmm..... any idea? */
}

void updateBlendingStateGL(VGContext *c, int alphaIsOne)
{
  /* Most common drawing mode (SRC_OVER with alpha=1)
     as well as SRC is optimized by turning OpenGL
     blending off. In other cases its turned on. */
  
  switch (c->blendMode)
  {
  case VG_BLEND_SRC:
    if (c->masking == VG_TRUE) {
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_BLEND);
    } else {
      glBlendFunc(GL_ONE, GL_ZERO);
      glDisable(GL_BLEND);
    }
    break;

  case VG_BLEND_SRC_IN:
    glBlendFunc(GL_DST_ALPHA, GL_ZERO);
    glEnable(GL_BLEND); break;

  case VG_BLEND_DST_IN:
    glBlendFunc(GL_ZERO, GL_SRC_ALPHA);
    glEnable(GL_BLEND); break;
    
  case VG_BLEND_SRC_OUT_SH:
    glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ZERO);
    glEnable(GL_BLEND); break;

  case VG_BLEND_DST_OUT_SH:
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND); break;

  case VG_BLEND_SRC_ATOP_SH:
    glBlendFunc(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND); break;

  case VG_BLEND_DST_ATOP_SH:
    glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA);
    glEnable(GL_BLEND); break;

  case VG_BLEND_DST_OVER:
    glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA);
    glEnable(GL_BLEND); break;

  case VG_BLEND_SRC_OVER: default:
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE_MINUS_DST_ALPHA, GL_ONE);
    if (alphaIsOne && c->masking == VG_FALSE) glDisable(GL_BLEND);
    else glEnable(GL_BLEND); break;
  };
}

static void shApplyMaskState(VGContext *context)
{
  if (context->masking == VG_TRUE &&
      context->maskTexture != 0 &&
      context->maskWidth > 0 &&
      context->maskHeight > 0) {
    shEnsureMaskTexture(context);
    glUniform1i(context->locationDraw.maskEnabled, 1);
    glUniform1i(context->locationDraw.maskSampler, SH_TEXTURE_MASK_INDEX);
    glUniform2f(context->locationDraw.maskSurfaceSize,
                (GLfloat)context->maskWidth,
                (GLfloat)context->maskHeight);
    glActiveTexture(SH_TEXTURE_MASK);
    glBindTexture(GL_TEXTURE_2D, context->maskTexture);
  } else {
    glUniform1i(context->locationDraw.maskEnabled, 0);
  }
}

/*-----------------------------------------------------------
 * Draws the triangles representing the stroke of a path.
 *-----------------------------------------------------------*/

static void shDrawStroke(SHPath *p)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  SHVertexState vertexState;

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(SHVector2) * p->stroke.size,
               p->stroke.items,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationDraw.pos);
  glVertexAttribPointer(context->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        0, (const GLvoid*)0);
  glDrawArrays(GL_TRIANGLES, 0, p->stroke.size);
  glDisableVertexAttribArray(context->locationDraw.pos);
  shRestoreVertexState(&vertexState);
  GL_CHECK_ERROR;
}

/*-----------------------------------------------------------
 * Draws the subdivided vertices in the OpenGL mode given
 * (this could be VG_TRIANGLE_FAN or VG_LINE_STRIP).
 *-----------------------------------------------------------*/

static void shDrawVertices(SHPath *p, GLenum mode)
{
  int start = 0;
  int size = 0;
  
  /* We separate vertex arrays by contours to properly
     handle the fill modes */
  VG_GETCONTEXT(VG_NO_RETVAL);
  SHVertexState vertexState;

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(SHVertex) * p->vertices.size,
               p->vertices.items,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationDraw.pos);
  glVertexAttribPointer(context->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHVertex), (const GLvoid*)0);
  
  while (start < p->vertices.size) {
    size = p->vertices.items[start].flags;
    glDrawArrays(mode, start, size);
    start += size;
  }
  
  glDisableVertexAttribArray(context->locationDraw.pos);
  shRestoreVertexState(&vertexState);
  GL_CHECK_ERROR;
}

/*--------------------------------------------------------------
 * Constructs & draws colored OpenGL primitives that cover the
 * given bounding box to represent the currently selected
 * stroke or fill paint
 *--------------------------------------------------------------*/

static void shDrawPaintMesh(VGContext *c, SHVector2 *min, SHVector2 *max,
                            VGPaintMode mode, GLenum texUnit)
{
  SHPaint *p = NULL;
  SHVector2 pmin, pmax;
  SHfloat K = 1.0f;
  SHVertexState vertexState;
  
  /* Pick the right paint */
  (void)texUnit;
  if (mode == VG_FILL_PATH) {
    p = (c->fillPaint ? c->fillPaint : &c->defaultPaint);
  }else if (mode == VG_STROKE_PATH) {
    p = (c->strokePaint ? c->strokePaint : &c->defaultPaint);
    K = SH_CEIL(c->strokeMiterLimit * c->strokeLineWidth) + 1.0f;
  }
  
  /* We want to be sure to cover every pixel of this path so better
     take a pixel more than leave some out (multisampling is tricky). */
  SET2V(pmin, (*min)); SUB2(pmin, K,K);
  SET2V(pmax, (*max)); ADD2(pmax, K,K);

  /* Construct appropriate OpenGL primitives so as
     to fill the stencil mask with select paint */

  switch (p->type) {
  case VG_PAINT_TYPE_LINEAR_GRADIENT:
    shLoadLinearGradientMesh(p, mode, VG_MATRIX_PATH_USER_TO_SURFACE);
    break; 

  case VG_PAINT_TYPE_RADIAL_GRADIENT:
    shLoadRadialGradientMesh(p, mode, VG_MATRIX_PATH_USER_TO_SURFACE);
    break; 
    
  case VG_PAINT_TYPE_PATTERN:
    if (p->pattern != VG_INVALID_HANDLE) {
      if (shImageIsRenderTarget((SHImage*)p->pattern)) {
        shSetError(c, VG_IMAGE_IN_USE_ERROR);
        return;
      }
      shLoadPatternMesh(p, mode, VG_MATRIX_PATH_USER_TO_SURFACE);
      break;
    }/* else behave as a color paint */
  
  case VG_PAINT_TYPE_COLOR:
    shLoadOneColorMesh(p);
    break;  
  }

  GLfloat v[] = { pmin.x, pmin.y,
                  pmax.x, pmin.y,
                  pmin.x, pmax.y,
                  pmax.x, pmax.y };
  shBindContextVertexState(c, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(c->locationDraw.pos);
  glVertexAttribPointer(c->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        0, (const GLvoid*)0);
  shApplyMaskState(c);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(c->locationDraw.pos);
  shRestoreVertexState(&vertexState);
  GL_CHECK_ERROR;
}

static void shDrawCoverageMesh(VGContext *c, SHVector2 *min, SHVector2 *max,
                               VGPaintMode mode)
{
  SHPaint coveragePaint;
  SHVector2 pmin, pmax;
  SHfloat K = 1.0f;
  GLfloat v[8];
  SHVertexState vertexState;

  if (mode == VG_STROKE_PATH)
    K = SH_CEIL(c->strokeMiterLimit * c->strokeLineWidth) + 1.0f;

  SET2V(pmin, (*min)); SUB2(pmin, K,K);
  SET2V(pmax, (*max)); ADD2(pmax, K,K);

  SHPaint_ctor(&coveragePaint);
  CSET(coveragePaint.color, 1.0f, 1.0f, 1.0f, 1.0f);
  shLoadOneColorMesh(&coveragePaint);
  glUniform1i(c->locationDraw.maskEnabled, 0);

  v[0] = pmin.x; v[1] = pmin.y;
  v[2] = pmax.x; v[3] = pmin.y;
  v[4] = pmin.x; v[5] = pmax.y;
  v[6] = pmax.x; v[7] = pmax.y;

  shBindContextVertexState(c, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(c->locationDraw.pos);
  glVertexAttribPointer(c->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        0, (const GLvoid*)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(c->locationDraw.pos);
  shRestoreVertexState(&vertexState);
  SHPaint_dtor(&coveragePaint);
  GL_CHECK_ERROR;
}

VGboolean shIsTessCacheValid (VGContext *c, SHPath *p)
{
  SHfloat nX, nY;
  SHVector2 X, Y;
  SHMatrix3x3 mi, mchange;
  VGboolean valid = VG_TRUE;

  if (p->cacheDataValid == VG_FALSE) {
    valid = VG_FALSE;
  }
  else if (p->cacheTransformInit == VG_FALSE) {
    valid = VG_FALSE;
  }
  else if (shInvertMatrix( &p->cacheTransform, &mi ) == VG_FALSE) {
    valid = VG_FALSE;
  }
  else
  {
    /* TODO: Compare change matrix for any scale or shear  */
    MULMATMAT( c->pathTransform, mi, mchange );
    SET2( X, mi.m[0][0], mi.m[1][0] );
    SET2( Y, mi.m[0][1], mi.m[1][1] );
    nX = NORM2( X ); nY = NORM2( Y );
    if (nX > 1.01f || nX < 0.99 ||
        nY > 1.01f || nY < 0.99)
      valid = VG_FALSE;
  }

  if (valid == VG_FALSE)
  {
    /* Update cache */
    p->cacheDataValid = VG_TRUE;
    p->cacheTransformInit = VG_TRUE;
    p->cacheTransform = c->pathTransform;
    p->cacheStrokeTessValid = VG_FALSE;
  }
  
  return valid;
}

typedef struct
{
  GLint framebuffer;
  GLint renderbuffer;
  GLint viewport[4];
  GLint program;
  GLint vertexArray;
  GLint arrayBuffer;
  GLint activeTexture;
  GLint maskTextureBinding;
  GLint drawBuffer;
  GLint readBuffer;
  GLint scissorBox[4];
  GLint blendSrcRgb;
  GLint blendDstRgb;
  GLint blendSrcAlpha;
  GLint blendDstAlpha;
  GLint blendEquationRgb;
  GLint blendEquationAlpha;
  GLint stencilFunc;
  GLint stencilRef;
  GLint stencilValueMask;
  GLint stencilFail;
  GLint stencilPassDepthFail;
  GLint stencilPassDepthPass;
  GLint stencilWriteMask;
  GLint clearStencil;
  GLfloat clearColor[4];
  GLboolean blend;
  GLboolean scissor;
  GLboolean depth;
  GLboolean stencil;
  GLboolean colorMask[4];
} SHRenderToMaskGLState;

static void shSaveRenderToMaskGLState(SHRenderToMaskGLState *state)
{
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &state->renderbuffer);
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vertexArray);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->arrayBuffer);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state->activeTexture);
  glGetIntegerv(GL_DRAW_BUFFER, &state->drawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &state->readBuffer);
  glGetIntegerv(GL_SCISSOR_BOX, state->scissorBox);
  glGetIntegerv(GL_BLEND_SRC_RGB, &state->blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &state->blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &state->blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &state->blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &state->blendEquationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state->blendEquationAlpha);
  glGetIntegerv(GL_STENCIL_FUNC, &state->stencilFunc);
  glGetIntegerv(GL_STENCIL_REF, &state->stencilRef);
  glGetIntegerv(GL_STENCIL_VALUE_MASK, &state->stencilValueMask);
  glGetIntegerv(GL_STENCIL_FAIL, &state->stencilFail);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &state->stencilPassDepthFail);
  glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &state->stencilPassDepthPass);
  glGetIntegerv(GL_STENCIL_WRITEMASK, &state->stencilWriteMask);
  glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &state->clearStencil);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clearColor);
  glGetBooleanv(GL_COLOR_WRITEMASK, state->colorMask);
  state->blend = glIsEnabled(GL_BLEND);
  state->scissor = glIsEnabled(GL_SCISSOR_TEST);
  state->depth = glIsEnabled(GL_DEPTH_TEST);
  state->stencil = glIsEnabled(GL_STENCIL_TEST);
  glActiveTexture(SH_TEXTURE_MASK);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->maskTextureBinding);
}

static void shRestoreRenderToMaskGLState(const SHRenderToMaskGLState *state)
{
  if (state->blend) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);

  if (state->scissor) glEnable(GL_SCISSOR_TEST);
  else glDisable(GL_SCISSOR_TEST);

  if (state->depth) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);

  if (state->stencil) glEnable(GL_STENCIL_TEST);
  else glDisable(GL_STENCIL_TEST);

  glBlendFuncSeparate(state->blendSrcRgb, state->blendDstRgb,
                      state->blendSrcAlpha, state->blendDstAlpha);
  glBlendEquationSeparate(state->blendEquationRgb,
                          state->blendEquationAlpha);
  glStencilFunc(state->stencilFunc, state->stencilRef,
                state->stencilValueMask);
  glStencilOp(state->stencilFail, state->stencilPassDepthFail,
              state->stencilPassDepthPass);
  glStencilMask(state->stencilWriteMask);
  glClearStencil(state->clearStencil);
  glScissor(state->scissorBox[0], state->scissorBox[1],
            state->scissorBox[2], state->scissorBox[3]);
  glColorMask(state->colorMask[0], state->colorMask[1],
              state->colorMask[2], state->colorMask[3]);
  glClearColor(state->clearColor[0], state->clearColor[1],
               state->clearColor[2], state->clearColor[3]);
  glBindFramebuffer(GL_FRAMEBUFFER, state->framebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, state->renderbuffer);
  glDrawBuffer(state->drawBuffer);
  glReadBuffer(state->readBuffer);
  glUseProgram(state->program);
  if (state->vertexArray == 0 ||
      glIsVertexArray((GLuint)state->vertexArray))
    glBindVertexArray((GLuint)state->vertexArray);
  else
    glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->arrayBuffer);
  glViewport(state->viewport[0], state->viewport[1],
             state->viewport[2], state->viewport[3]);
  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, state->maskTextureBinding);
  glActiveTexture(state->activeTexture);
}

static VGboolean shEnsureRenderToMaskTarget(VGContext *context)
{
  GLenum status;

  if (context->surfaceWidth <= 0 || context->surfaceHeight <= 0)
    return VG_FALSE;

  if (context->renderToMaskTexture != 0 &&
      context->renderToMaskFramebuffer != 0 &&
      context->renderToMaskStencil != 0 &&
      context->renderToMaskWidth == context->surfaceWidth &&
      context->renderToMaskHeight == context->surfaceHeight)
    return VG_TRUE;

  if (context->renderToMaskTexture == 0)
    glGenTextures(1, &context->renderToMaskTexture);
  if (context->renderToMaskFramebuffer == 0)
    glGenFramebuffers(1, &context->renderToMaskFramebuffer);
  if (context->renderToMaskStencil == 0)
    glGenRenderbuffers(1, &context->renderToMaskStencil);

  if (context->renderToMaskTexture == 0 ||
      context->renderToMaskFramebuffer == 0 ||
      context->renderToMaskStencil == 0)
    return VG_FALSE;

  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, context->renderToMaskTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
               context->surfaceWidth, context->surfaceHeight, 0,
               GL_RED, GL_UNSIGNED_BYTE, NULL);

  glBindRenderbuffer(GL_RENDERBUFFER, context->renderToMaskStencil);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                        context->surfaceWidth, context->surfaceHeight);

  glBindFramebuffer(GL_FRAMEBUFFER, context->renderToMaskFramebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, context->renderToMaskTexture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, context->renderToMaskStencil);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
    return VG_FALSE;

  context->renderToMaskWidth = context->surfaceWidth;
  context->renderToMaskHeight = context->surfaceHeight;
  GL_CHECK_ERROR;

  return VG_TRUE;
}

static void shClearRenderToMaskTarget(VGContext *context)
{
  glBindFramebuffer(GL_FRAMEBUFFER, context->renderToMaskFramebuffer);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, context->surfaceWidth, context->surfaceHeight);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glStencilMask(0xff);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

static VGboolean shSetRenderToMaskScissor(VGContext *context)
{
  SHRectangle *rect;

  if (context->scissoring != VG_TRUE) {
    glDisable(GL_SCISSOR_TEST);
    return VG_TRUE;
  }

  if (context->scissor.size == 0)
    return VG_FALSE;

  rect = &context->scissor.items[0];
  if (rect->w <= 0.0f || rect->h <= 0.0f)
    return VG_FALSE;

  glScissor((GLint)rect->x, (GLint)rect->y,
            (GLint)rect->w, (GLint)rect->h);
  glEnable(GL_SCISSOR_TEST);
  return VG_TRUE;
}

static void shEnsurePathGeometry(VGContext *context, SHPath *p)
{
  SHMatrix3x3 mi;

  if (shIsTessCacheValid(context, p) == VG_FALSE) {
    if (shInvertMatrix(&context->pathTransform, &mi)) {
      shFlattenPath(p, 1);
      shTransformVertices(&mi, p);
    } else {
      shFlattenPath(p, 0);
    }
    shFindBoundbox(p);
  }
}

static void shRenderFillToMaskTarget(VGContext *context, SHPath *p)
{
  if (p->vertices.size <= 0)
    return;

  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xff);
  glStencilFunc(GL_ALWAYS, 0, 0);
  glStencilOp(GL_INVERT, GL_INVERT, GL_INVERT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  shDrawVertices(p, GL_TRIANGLE_FAN);

  glDisable(GL_BLEND);
  glStencilFunc(GL_EQUAL, 1, 1);
  glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  shDrawCoverageMesh(context, &p->min, &p->max, VG_FILL_PATH);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_STENCIL_TEST);
}

VGboolean shIsStrokeCacheValid (VGContext *c, SHPath *p);

static void shRenderStrokeToMaskTarget(VGContext *context, SHPath *p)
{
  if (context->strokeLineWidth <= 0.0f || p->vertices.size <= 0)
    return;

  if (shIsStrokeCacheValid(context, p) == VG_FALSE) {
    shVector2ArrayClear(&p->stroke);
    shStrokePath(context, p);
  }

  if (p->stroke.size <= 0)
    return;

  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xff);
  glStencilFunc(GL_NOTEQUAL, 1, 1);
  glStencilOp(GL_KEEP, GL_INCR, GL_INCR);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  shDrawStroke(p);

  glDisable(GL_BLEND);
  glStencilFunc(GL_EQUAL, 1, 1);
  glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  shDrawCoverageMesh(context, &p->min, &p->max, VG_STROKE_PATH);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_STENCIL_TEST);
}

static VGboolean shRenderPathPassToMask(VGContext *context,
                                        SHPath *p,
                                        VGPaintMode mode,
                                        VGMaskOperation operation)
{
  SHfloat mgl[16];
  VGboolean renderCoverage;

  if (context->surfaceWidth <= 0 || context->surfaceHeight <= 0)
    return VG_TRUE;

  if (!shEnsureRenderToMaskTarget(context))
    return VG_FALSE;

  shClearRenderToMaskTarget(context);
  renderCoverage = shSetRenderToMaskScissor(context);

  glUseProgram(context->progDraw);
  shMatrixToGL(&context->pathTransform, mgl);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniform1i(context->locationDraw.drawMode, 0);
  glUniform1i(context->locationDraw.maskEnabled, 0);
  GL_CHECK_ERROR;

  if (renderCoverage) {
    if (mode == VG_FILL_PATH)
      shRenderFillToMaskTarget(context, p);
    else
      shRenderStrokeToMaskTarget(context, p);
  }

  glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  return shApplyMaskTextureToSurface(context,
                                     context->renderToMaskTexture,
                                     operation);
}

VGboolean shIsStrokeCacheValid (VGContext *c, SHPath *p)
{
  VGboolean valid = VG_TRUE;

  if (p->cacheStrokeInit == VG_FALSE) {
    valid = VG_FALSE;
  }
  else if (p->cacheStrokeTessValid == VG_FALSE) {
    valid = VG_FALSE;
  }
  else if (c->strokeDashPattern.size > 0) {
    valid = VG_FALSE;
  }
  else if (p->cacheStrokeLineWidth  != c->strokeLineWidth  ||
           p->cacheStrokeCapStyle   != c->strokeCapStyle   ||
           p->cacheStrokeJoinStyle  != c->strokeJoinStyle  ||
           p->cacheStrokeMiterLimit != c->strokeMiterLimit) {
    valid = VG_FALSE;
  }

  if (valid == VG_FALSE)
  {
    /* Update cache */
    p->cacheStrokeInit = VG_TRUE;
    p->cacheStrokeTessValid = VG_TRUE;
    p->cacheStrokeLineWidth  = c->strokeLineWidth;
    p->cacheStrokeCapStyle   = c->strokeCapStyle;
    p->cacheStrokeJoinStyle  = c->strokeJoinStyle;
    p->cacheStrokeMiterLimit = c->strokeMiterLimit;
  }

  return valid;
}

/*-----------------------------------------------------------
 * Tessellates / strokes the path and draws it according to
 * VGContext state.
 *-----------------------------------------------------------*/

void shDrawPath(VGContext *context, SHPath *p, VGbitfield paintModes)
{
  SHMatrix3x3 mi;
  SHfloat mgl[16];
  SHPaint *fill, *stroke;
  SHRectangle *rect;

  /* Check whether scissoring is enabled and scissor
     rectangle is valid */
  if (context->scissoring == VG_TRUE) {
    rect = &context->scissor.items[0];
    if (context->scissor.size == 0) VG_RETURN( VG_NO_RETVAL );
    if (rect->w <= 0.0f || rect->h <= 0.0f) VG_RETURN( VG_NO_RETVAL );
    glScissor( (GLint)rect->x, (GLint)rect->y, (GLint)rect->w, (GLint)rect->h );
    glEnable( GL_SCISSOR_TEST );
  }
  
  /* If user-to-surface matrix invertible tessellate in
     surface space for better path resolution */
  if (shIsTessCacheValid( context, p ) == VG_FALSE)
  {
    if (shInvertMatrix(&context->pathTransform, &mi)) {
      shFlattenPath(p, 1);
      shTransformVertices(&mi, p);
    }else shFlattenPath(p, 0);
    shFindBoundbox(p);
  }
  
  /* Pick paint if available or default*/
  fill = (context->fillPaint ? context->fillPaint : &context->defaultPaint);
  stroke = (context->strokePaint ? context->strokePaint : &context->defaultPaint);
  
  /* Apply transformation */
  shMatrixToGL(&context->pathTransform, mgl);
  glUseProgram(context->progDraw);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniform1i(context->locationDraw.drawMode, 0); /* drawMode: path */
  GL_CHECK_ERROR;
  
  if (paintModes & VG_FILL_PATH) {
    
    /* Tesselate into stencil */
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0);
    glStencilOp(GL_INVERT, GL_INVERT, GL_INVERT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    shDrawVertices(p, GL_TRIANGLE_FAN);
    
    /* Setup blending */
    updateBlendingStateGL(context,
                          fill->type == VG_PAINT_TYPE_COLOR &&
                          fill->color.a == 1.0f);
    
    /* Draw paint where stencil odd */
    glStencilFunc(GL_EQUAL, 1, 1);
    glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    shDrawPaintMesh(context, &p->min, &p->max, VG_FILL_PATH, GL_TEXTURE0);

    /* Reset state */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
  }
  
  if ((paintModes & VG_STROKE_PATH) &&
      context->strokeLineWidth > 0.0f) {
    
    if (1) {/*context->strokeLineWidth > 1.0f) {*/

      if (shIsStrokeCacheValid( context, p ) == VG_FALSE)
      {
        /* Generate stroke triangles in user space */
        shVector2ArrayClear(&p->stroke);
        shStrokePath(context, p);
      }

      /* Stroke into stencil */
      glEnable(GL_STENCIL_TEST);
      glStencilFunc(GL_NOTEQUAL, 1, 1);
      glStencilOp(GL_KEEP, GL_INCR, GL_INCR);
      glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      shDrawStroke(p);

      /* Setup blending */
      updateBlendingStateGL(context,
                            stroke->type == VG_PAINT_TYPE_COLOR &&
                            stroke->color.a == 1.0f);

      /* Draw paint where stencil odd */
      glStencilFunc(GL_EQUAL, 1, 1);
      glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      shDrawPaintMesh(context, &p->min, &p->max, VG_STROKE_PATH, GL_TEXTURE0);
      
      /* Reset state */
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_BLEND);
      
    }else{
      
      /* Simulate thin stroke by alpha */
      SHColor c = stroke->color;
      if (context->strokeLineWidth < 1.0f)
        c.a *= context->strokeLineWidth;
      
      /* Draw contour as a line */
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      shDrawVertices(p, GL_LINE_STRIP);
      glDisable(GL_BLEND);
    }
  }
  
  if (context->scissoring == VG_TRUE)
    glDisable( GL_SCISSOR_TEST );

  if (paintModes & (VG_FILL_PATH | VG_STROKE_PATH))
    shMarkRenderTargetDirty(context);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgDrawPath(VGPath path, VGbitfield paintModes)
{
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidPath(context, path),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  shDrawPath(context, (SHPath*)path, paintModes);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgRenderToMask(VGPath path,
                                VGbitfield paintModes,
                                VGMaskOperation operation)
{
  SHPath *p;
  SHRenderToMaskGLState state;
  VGboolean success = VG_TRUE;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidPath(context, path),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(operation != VG_CLEAR_MASK &&
                   operation != VG_FILL_MASK &&
                   operation != VG_SET_MASK &&
                   operation != VG_UNION_MASK &&
                   operation != VG_INTERSECT_MASK &&
                   operation != VG_SUBTRACT_MASK,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  if (operation == VG_CLEAR_MASK) {
    VG_RETURN_ERR_IF(!shApplyMaskValueToSurface(context,
                                                0.0f,
                                                operation),
                     VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
    VG_RETURN(VG_NO_RETVAL);
  }

  if (operation == VG_FILL_MASK) {
    VG_RETURN_ERR_IF(!shApplyMaskValueToSurface(context,
                                                1.0f,
                                                operation),
                     VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
    VG_RETURN(VG_NO_RETVAL);
  }

  if (paintModes == 0 ||
      context->surfaceWidth <= 0 ||
      context->surfaceHeight <= 0)
    VG_RETURN(VG_NO_RETVAL);

  p = (SHPath*)path;
  shEnsurePathGeometry(context, p);

  shSaveRenderToMaskGLState(&state);
  if (paintModes & VG_FILL_PATH)
    success = shRenderPathPassToMask(context, p, VG_FILL_PATH, operation);
  if (success && (paintModes & VG_STROKE_PATH))
    success = shRenderPathPassToMask(context, p, VG_STROKE_PATH, operation);
  shRestoreRenderToMaskGLState(&state);

  VG_RETURN_ERR_IF(!success, VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

void shDrawImage(VGContext *context, SHImage *i)
{
  typedef struct
  {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
  } SHImageVertex;
  SHfloat mgl[16];
  SHPaint *fill;
  SHRectangle *rect;
  SHImageVertex vertices[4];
  SHVertexState vertexState;

  if (shImageIsRenderTarget(i)) {
    shSetError(context, VG_IMAGE_IN_USE_ERROR);
    return;
  }
  
  /* Check whether scissoring is enabled and scissor
     rectangle is valid */
  if (context->scissoring == VG_TRUE) {
    rect = &context->scissor.items[0];
    if (context->scissor.size == 0) VG_RETURN( VG_NO_RETVAL );
    if (rect->w <= 0.0f || rect->h <= 0.0f) VG_RETURN( VG_NO_RETVAL );
    glScissor( (GLint)rect->x, (GLint)rect->y, (GLint)rect->w, (GLint)rect->h );
    glEnable( GL_SCISSOR_TEST );
  }
  
  /* Apply image-user-to-surface transformation */
  shMatrixToGL(&context->imageTransform, mgl);
  glUseProgram(context->progDraw);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniform1i(context->locationDraw.drawMode, 1); /* drawMode: image */
  GL_CHECK_ERROR;
  
  /* Clamp to edge for proper filtering, modulate for multiply mode */
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, i->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  
  /* Adjust antialiasing to settings */
  if (context->imageQuality == VG_IMAGE_QUALITY_NONANTIALIASED) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }else{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  }

  vertices[0].x = 0.0f;
  vertices[0].y = 0.0f;
  vertices[0].u = 0.0f;
  vertices[0].v = 0.0f;
  vertices[1].x = i->width;
  vertices[1].y = 0.0f;
  vertices[1].u = 1.0f;
  vertices[1].v = 0.0f;
  vertices[2].x = 0.0f;
  vertices[2].y = i->height;
  vertices[2].u = 0.0f;
  vertices[2].v = 1.0f;
  vertices[3].x = i->width;
  vertices[3].y = i->height;
  vertices[3].u = 1.0f;
  vertices[3].v = 1.0f;

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationDraw.textureUV);
  glVertexAttribPointer(context->locationDraw.textureUV, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHImageVertex),
                        (const GLvoid*)(2 * sizeof(GLfloat)));
  glUniform1i(context->locationDraw.imageSampler, 0);
  GL_CHECK_ERROR;
  
  /* Pick fill paint */
  fill = (context->fillPaint ? context->fillPaint : &context->defaultPaint);
  
  /* Setup blending */
  updateBlendingStateGL(context, 0);

  /* Draw textured quad */
  glEnable(GL_TEXTURE_2D);
    
  if (context->imageMode == VG_DRAW_IMAGE_MULTIPLY){
      /* Multiply each colors */
      glUniform1i(context->locationDraw.imageMode, VG_DRAW_IMAGE_MULTIPLY );
      switch(fill->type){
          case VG_PAINT_TYPE_RADIAL_GRADIENT:
              shLoadRadialGradientMesh(fill, VG_FILL_PATH, VG_MATRIX_IMAGE_USER_TO_SURFACE);
              break;
          case VG_PAINT_TYPE_LINEAR_GRADIENT:
              shLoadLinearGradientMesh(fill, VG_FILL_PATH, VG_MATRIX_IMAGE_USER_TO_SURFACE);
              break;
          case VG_PAINT_TYPE_PATTERN:
              shLoadPatternMesh(fill, VG_FILL_PATH, VG_MATRIX_IMAGE_USER_TO_SURFACE);
              break;
          default:
          case VG_PAINT_TYPE_COLOR:
              shLoadOneColorMesh(fill);
              break;
      }
  } else {
      glUniform1i(context->locationDraw.imageMode, VG_DRAW_IMAGE_NORMAL );
  }

  glVertexAttribPointer(context->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHImageVertex), (const GLvoid*)0);
  glEnableVertexAttribArray(context->locationDraw.pos);
  shApplyMaskState(context);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(context->locationDraw.pos);
  glDisableVertexAttribArray(context->locationDraw.textureUV);
  shRestoreVertexState(&vertexState);
    
  glDisable(GL_TEXTURE_2D);
  GL_CHECK_ERROR;

  if (context->scissoring == VG_TRUE)
    glDisable( GL_SCISSOR_TEST );

  shMarkRenderTargetDirty(context);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgDrawImage(VGImage image)
{
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, image),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  shDrawImage(context, (SHImage*)image);

  VG_RETURN(VG_NO_RETVAL);
}
