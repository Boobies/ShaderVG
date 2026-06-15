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
#include <VG/openvg.h>
#include <VG/vgext.h>
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

static VGboolean shUsesShaderBlendMode(VGBlendMode mode,
                                       VGboolean sourceCoverageEnabled)
{
  if ((VGint)mode >= VG_BLEND_OVERLAY_KHR &&
      (VGint)mode <= VG_BLEND_XOR_KHR)
    return VG_TRUE;

  if (mode == VG_BLEND_MULTIPLY ||
      mode == VG_BLEND_SCREEN ||
      mode == VG_BLEND_DARKEN ||
      mode == VG_BLEND_LIGHTEN ||
      mode == VG_BLEND_ADDITIVE)
    return VG_TRUE;

  if (sourceCoverageEnabled == VG_TRUE &&
      mode != VG_BLEND_SRC_OVER &&
      mode != VG_BLEND_DST_OVER)
    return VG_TRUE;

  return VG_FALSE;
}

static VGboolean shEnsureBlendTexture(VGContext *c)
{
  GLint previousActiveTexture;
  GLint previousTexture;

  if (!c || c->surfaceWidth <= 0 || c->surfaceHeight <= 0)
    return VG_FALSE;

  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  glActiveTexture(SH_TEXTURE_BLEND);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

  if (c->blendTexture == 0)
    glGenTextures(1, &c->blendTexture);
  if (c->blendTexture == 0) {
    glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
    glActiveTexture(previousActiveTexture);
    return VG_FALSE;
  }

  glBindTexture(GL_TEXTURE_2D, c->blendTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  if (c->blendTextureWidth != c->surfaceWidth ||
      c->blendTextureHeight != c->surfaceHeight) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 c->surfaceWidth, c->surfaceHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    if (glGetError() != GL_NO_ERROR) {
      glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
      glActiveTexture(previousActiveTexture);
      return VG_FALSE;
    }

    c->blendTextureWidth = c->surfaceWidth;
    c->blendTextureHeight = c->surfaceHeight;
  }

  glBindTexture(GL_TEXTURE_2D, (GLuint)previousTexture);
  glActiveTexture(previousActiveTexture);
  return VG_TRUE;
}

static VGboolean shPrepareShaderBlend(VGContext *c)
{
  GLint previousActiveTexture;

  if (!shEnsureBlendTexture(c))
    return VG_FALSE;

  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  glActiveTexture(SH_TEXTURE_BLEND);
  glBindTexture(GL_TEXTURE_2D, c->blendTexture);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0,
                      c->surfaceWidth, c->surfaceHeight);
  glActiveTexture(previousActiveTexture);

  if (glGetError() != GL_NO_ERROR)
    return VG_FALSE;

  glUniform1i(c->locationDraw.blendMode, (GLint)c->blendMode);
  glUniform1i(c->locationDraw.blendSampler, SH_TEXTURE_BLEND_INDEX);
  glUniform2f(c->locationDraw.blendSurfaceSize,
              (GLfloat)c->surfaceWidth,
              (GLfloat)c->surfaceHeight);
  glDisable(GL_BLEND);
  return VG_TRUE;
}

static void shDisableShaderBlend(VGContext *c)
{
  glUniform1i(c->locationDraw.blendMode, 0);
}

static VGboolean updateBlendingStateGL(VGContext *c, int alphaIsOne,
                                       int coverageEnabled)
{
  VGboolean sourceCoverageEnabled =
    (coverageEnabled || c->masking == VG_TRUE) ? VG_TRUE : VG_FALSE;

  /* Most common drawing mode (SRC_OVER with alpha=1)
     as well as SRC is optimized by turning OpenGL
     blending off. In other cases its turned on. */

  if (shUsesShaderBlendMode(c->blendMode, sourceCoverageEnabled)) {
    if (!shPrepareShaderBlend(c))
      return VG_FALSE;
    return VG_TRUE;
  }

  shDisableShaderBlend(c);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

  switch (c->blendMode)
  {
  case VG_BLEND_SRC:
    if (c->masking == VG_TRUE || coverageEnabled) {
      glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                          GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
    glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE);
    glEnable(GL_BLEND); break;

  case VG_BLEND_SRC_OVER: default:
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (alphaIsOne && c->masking == VG_FALSE && !coverageEnabled)
      glDisable(GL_BLEND);
    else glEnable(GL_BLEND); break;
  };

  return VG_TRUE;
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

static void shApplyCoverageState(VGContext *context, VGboolean enabled)
{
  if (enabled == VG_TRUE &&
      context->coverageTexture != 0 &&
      context->coverageWidth == context->surfaceWidth &&
      context->coverageHeight == context->surfaceHeight) {
    glUniform1i(context->locationDraw.coverageEnabled, 1);
    glUniform1i(context->locationDraw.coverageSampler,
                SH_TEXTURE_COVERAGE_INDEX);
    glUniform2f(context->locationDraw.coverageSurfaceSize,
                (GLfloat)context->surfaceWidth,
                (GLfloat)context->surfaceHeight);
    glActiveTexture(SH_TEXTURE_COVERAGE);
    glBindTexture(GL_TEXTURE_2D, context->coverageTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  } else {
    glUniform1i(context->locationDraw.coverageEnabled, 0);
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
  VG_RETURN(VG_NO_RETVAL);
}

/*-----------------------------------------------------------
 * Draws the subdivided vertices in the OpenGL mode given
 * (this could be VG_TRIANGLE_FAN or VG_LINE_STRIP).
 *-----------------------------------------------------------*/

static void shDrawVertexData(const SHVertex *vertices,
                             SHint vertexCount,
                             GLenum mode)
{
  int start = 0;
  int size = 0;
  
  /* We separate vertex arrays by contours to properly
     handle the fill modes */
  VG_GETCONTEXT(VG_NO_RETVAL);
  SHVertexState vertexState;

  if (!vertices || vertexCount <= 0)
    VG_RETURN(VG_NO_RETVAL);

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER,
               sizeof(SHVertex) * vertexCount,
               vertices,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationDraw.pos);
  glVertexAttribPointer(context->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHVertex), (const GLvoid*)0);
  
  while (start < vertexCount) {
    size = vertices[start].flags;
    if (size <= 0 || start + size > vertexCount)
      size = vertexCount - start;
    glDrawArrays(mode, start, size);
    start += size;
  }
  
  glDisableVertexAttribArray(context->locationDraw.pos);
  shRestoreVertexState(&vertexState);
  GL_CHECK_ERROR;
  VG_RETURN(VG_NO_RETVAL);
}

static void shDrawVertices(SHPath *p, GLenum mode)
{
  shDrawVertexData(p->vertices.items, p->vertices.size, mode);
}

/*--------------------------------------------------------------
 * Constructs & draws colored OpenGL primitives that cover the
 * given bounding box to represent the currently selected
 * stroke or fill paint
 *--------------------------------------------------------------*/

static void shDrawPaintMesh(VGContext *c, SHVector2 *min, SHVector2 *max,
                            VGPaintMode mode, GLenum texUnit,
                            VGboolean coverageEnabled)
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
    if (p->pattern) {
      if (shImageIsRenderTarget(p->pattern)) {
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
  shApplyCoverageState(c, coverageEnabled);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  shApplyCoverageState(c, VG_FALSE);
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
  glUniform1i(c->locationDraw.coverageEnabled, 0);
  shDisableShaderBlend(c);

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
  SHGLFramebufferState framebuffer;
  SHGLViewportState viewport;
  SHGLProgramState program;
  SHVertexState vertex;
  SHGLActiveTextureState activeTexture;
  SHGLTextureBindingState texture0;
  SHGLTextureBindingState maskTexture;
  SHGLTextureBindingState coverageTexture;
  SHGLScissorState scissor;
  SHGLCapabilityState capabilities;
  SHGLBlendState blend;
  SHGLStencilState stencil;
  SHGLColorState color;
} SHRenderToMaskGLState;

static void shSaveRenderToMaskGLState(SHRenderToMaskGLState *state)
{
  shSaveFramebufferState(&state->framebuffer);
  shSaveViewportState(&state->viewport);
  shSaveProgramState(&state->program);
  shSaveVertexBindingState(&state->vertex);
  shSaveActiveTextureState(&state->activeTexture);
  shSaveScissorState(&state->scissor);
  shSaveBlendState(&state->blend);
  shSaveStencilState(&state->stencil);
  shSaveColorState(&state->color);
  shSaveCapabilityState(&state->capabilities);
  shSaveTextureBindingState(&state->texture0, GL_TEXTURE0);
  shSaveTextureBindingState(&state->maskTexture, SH_TEXTURE_MASK);
  shSaveTextureBindingState(&state->coverageTexture, SH_TEXTURE_COVERAGE);
}

static void shRestoreRenderToMaskGLState(const SHRenderToMaskGLState *state)
{
  shRestoreCapabilityState(&state->capabilities);
  shRestoreBlendState(&state->blend);
  shRestoreStencilState(&state->stencil);
  shRestoreScissorState(&state->scissor);
  shRestoreColorState(&state->color);
  shRestoreFramebufferState(&state->framebuffer);
  shRestoreProgramState(&state->program);
  shRestoreVertexBindingState(&state->vertex);
  shRestoreViewportState(&state->viewport);
  shRestoreTextureBindingState(&state->texture0);
  shRestoreTextureBindingState(&state->maskTexture);
  shRestoreTextureBindingState(&state->coverageTexture);
  shRestoreActiveTextureState(&state->activeTexture);
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

static VGboolean shSurfaceHasNativeMultisampling(VGContext *context)
{
  return (context->surfaceSampleBuffers > 0 &&
          context->surfaceSamples > 1) ? VG_TRUE : VG_FALSE;
}

static SHint shRequestedCoverageScale(VGContext *context)
{
  if (context->renderingQuality == VG_RENDERING_QUALITY_NONANTIALIASED ||
      shSurfaceHasNativeMultisampling(context))
    return 1;

  return context->renderingQuality == VG_RENDERING_QUALITY_BETTER ? 4 : 2;
}

static SHint shCoverageScaleForSurface(VGContext *context)
{
  SHint scale = shRequestedCoverageScale(context);
  GLint maxTextureSize = 0;
  GLint maxRenderbufferSize = 0;

  if (scale <= 1 ||
      context->surfaceWidth <= 0 ||
      context->surfaceHeight <= 0)
    return 1;

  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
  glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbufferSize);
  if (maxTextureSize <= 0 || maxRenderbufferSize <= 0)
    return 1;

  while (scale > 1) {
    if ((long long)context->surfaceWidth * scale <= maxTextureSize &&
        (long long)context->surfaceHeight * scale <= maxTextureSize &&
        (long long)context->surfaceWidth * scale <= maxRenderbufferSize &&
        (long long)context->surfaceHeight * scale <= maxRenderbufferSize)
      return scale;
    scale /= 2;
  }

  return 1;
}

static VGboolean shEnsureCoverageTarget(VGContext *context, SHint scale)
{
  SHint highWidth;
  SHint highHeight;
  GLenum status;

  if (scale <= 1 ||
      context->surfaceWidth <= 0 ||
      context->surfaceHeight <= 0)
    return VG_FALSE;

  highWidth = context->surfaceWidth * scale;
  highHeight = context->surfaceHeight * scale;

  if (context->coverageTexture != 0 &&
      context->coverageFramebuffer != 0 &&
      context->coverageSupersampleTexture != 0 &&
      context->coverageSupersampleFramebuffer != 0 &&
      context->coverageSupersampleStencil != 0 &&
      context->coverageWidth == context->surfaceWidth &&
      context->coverageHeight == context->surfaceHeight &&
      context->coverageSupersampleWidth == highWidth &&
      context->coverageSupersampleHeight == highHeight &&
      context->coverageSupersampleScale == scale)
    return VG_TRUE;

  if (context->coverageTexture == 0)
    glGenTextures(1, &context->coverageTexture);
  if (context->coverageFramebuffer == 0)
    glGenFramebuffers(1, &context->coverageFramebuffer);
  if (context->coverageSupersampleTexture == 0)
    glGenTextures(1, &context->coverageSupersampleTexture);
  if (context->coverageSupersampleFramebuffer == 0)
    glGenFramebuffers(1, &context->coverageSupersampleFramebuffer);
  if (context->coverageSupersampleStencil == 0)
    glGenRenderbuffers(1, &context->coverageSupersampleStencil);

  if (context->coverageTexture == 0 ||
      context->coverageFramebuffer == 0 ||
      context->coverageSupersampleTexture == 0 ||
      context->coverageSupersampleFramebuffer == 0 ||
      context->coverageSupersampleStencil == 0)
    return VG_FALSE;

  glActiveTexture(SH_TEXTURE_COVERAGE);
  glBindTexture(GL_TEXTURE_2D, context->coverageSupersampleTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, highWidth, highHeight, 0,
               GL_RED, GL_UNSIGNED_BYTE, NULL);
  if (glGetError() != GL_NO_ERROR)
    return VG_FALSE;

  glBindRenderbuffer(GL_RENDERBUFFER, context->coverageSupersampleStencil);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                        highWidth, highHeight);
  if (glGetError() != GL_NO_ERROR)
    return VG_FALSE;

  glBindFramebuffer(GL_FRAMEBUFFER, context->coverageSupersampleFramebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D,
                         context->coverageSupersampleTexture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER,
                            context->coverageSupersampleStencil);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
    return VG_FALSE;

  glBindTexture(GL_TEXTURE_2D, context->coverageTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
               context->surfaceWidth, context->surfaceHeight, 0,
               GL_RED, GL_UNSIGNED_BYTE, NULL);
  if (glGetError() != GL_NO_ERROR)
    return VG_FALSE;

  glBindFramebuffer(GL_FRAMEBUFFER, context->coverageFramebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, context->coverageTexture, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
    return VG_FALSE;

  context->coverageWidth = context->surfaceWidth;
  context->coverageHeight = context->surfaceHeight;
  context->coverageSupersampleWidth = highWidth;
  context->coverageSupersampleHeight = highHeight;
  context->coverageSupersampleScale = scale;
  GL_CHECK_ERROR;

  return VG_TRUE;
}

static void shResolveCoverageTarget(VGContext *context)
{
  GLfloat v[8];
  SHVertexState vertexState;

  glBindFramebuffer(GL_FRAMEBUFFER, context->coverageFramebuffer);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0, context->surfaceWidth, context->surfaceHeight);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

  glUseProgram(context->progCoverage);
  glUniform2f(context->locationCoverage.targetSize,
              (GLfloat)context->surfaceWidth,
              (GLfloat)context->surfaceHeight);
  glUniform1i(context->locationCoverage.sourceSampler, 0);
  glUniform1i(context->locationCoverage.scale,
              context->coverageSupersampleScale);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, context->coverageSupersampleTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  v[0] = 0.0f; v[1] = 0.0f;
  v[2] = (GLfloat)context->surfaceWidth; v[3] = 0.0f;
  v[4] = 0.0f; v[5] = (GLfloat)context->surfaceHeight;
  v[6] = (GLfloat)context->surfaceWidth;
  v[7] = (GLfloat)context->surfaceHeight;

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationCoverage.pos);
  glVertexAttribPointer(context->locationCoverage.pos, 2, GL_FLOAT, GL_FALSE,
                        0, (const GLvoid*)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(context->locationCoverage.pos);
  shRestoreVertexState(&vertexState);
  GL_CHECK_ERROR;
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

static void shDrawFillStencilVertices(VGContext *context,
                                      const SHVertex *vertices,
                                      SHint vertexCount)
{
  GLboolean cullEnabled;
  GLint frontFace;

  if (!vertices || vertexCount <= 0)
    return;

  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xff);
  glStencilFunc(GL_ALWAYS, 0, 0);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

  if (context->fillRule == VG_NON_ZERO) {
    cullEnabled = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
    glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
    shDrawVertexData(vertices, vertexCount, GL_TRIANGLE_FAN);
    glFrontFace((GLenum)frontFace);
    if (cullEnabled == GL_TRUE)
      glEnable(GL_CULL_FACE);
    else
      glDisable(GL_CULL_FACE);
  } else {
    glStencilOp(GL_INVERT, GL_INVERT, GL_INVERT);
    shDrawVertexData(vertices, vertexCount, GL_TRIANGLE_FAN);
  }
}

static void shDrawFillStencil(VGContext *context, SHPath *p)
{
  shDrawFillStencilVertices(context, p->vertices.items, p->vertices.size);
}

static void shSetFillStencilPaintTest(VGContext *context)
{
  if (context->fillRule == VG_NON_ZERO)
    glStencilFunc(GL_NOTEQUAL, 0, 0xff);
  else
    glStencilFunc(GL_EQUAL, 1, 1);
  glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
}

static void shRenderFillToMaskTarget(VGContext *context, SHPath *p)
{
  if (p->vertices.size <= 0)
    return;

  shDrawFillStencil(context, p);

  glDisable(GL_BLEND);
  shSetFillStencilPaintTest(context);
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

static VGboolean shRenderPathCoverage(VGContext *context,
                                      SHPath *p,
                                      VGPaintMode mode,
                                      SHint scale)
{
  SHRenderToMaskGLState state;
  SHRectangle *rect;
  SHfloat mgl[16];
  SHfloat projection[16];
  SHfloat volume;
  VGboolean drawCoverage = VG_TRUE;

  if (scale <= 1)
    return VG_FALSE;

  shSaveRenderToMaskGLState(&state);

  if (!shEnsureCoverageTarget(context, scale)) {
    shRestoreRenderToMaskGLState(&state);
    return VG_FALSE;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, context->coverageSupersampleFramebuffer);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glViewport(0, 0,
             context->coverageSupersampleWidth,
             context->coverageSupersampleHeight);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glStencilMask(0xff);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClearStencil(0);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  if (context->scissoring == VG_TRUE) {
    if (context->scissor.size == 0) {
      drawCoverage = VG_FALSE;
    } else {
      rect = &context->scissor.items[0];
      if (rect->w <= 0.0f || rect->h <= 0.0f) {
        drawCoverage = VG_FALSE;
      } else {
        glScissor((GLint)(rect->x * scale),
                  (GLint)(rect->y * scale),
                  (GLint)(rect->w * scale),
                  (GLint)(rect->h * scale));
        glEnable(GL_SCISSOR_TEST);
      }
    }
  }

  glUseProgram(context->progDraw);
  shApplyColorTransform(context);
  shMatrixToGL(&context->pathTransform, mgl);
  volume = fmax(context->surfaceWidth, context->surfaceHeight) / 2.0f;
  shCalcOrtho2D(projection,
                0.0f, (SHfloat)context->surfaceWidth,
                0.0f, (SHfloat)context->surfaceHeight,
                -volume, volume);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniformMatrix4fv(context->locationDraw.projection, 1, GL_FALSE,
                     projection);
  glUniform1i(context->locationDraw.drawMode, 0);
  glUniform1i(context->locationDraw.maskEnabled, 0);
  glUniform1i(context->locationDraw.coverageEnabled, 0);
  glUniform1i(context->locationDraw.coveragePass, 1);
  shDisableShaderBlend(context);
  GL_CHECK_ERROR;

  if (drawCoverage == VG_TRUE) {
    if (mode == VG_FILL_PATH)
      shRenderFillToMaskTarget(context, p);
    else
      shRenderStrokeToMaskTarget(context, p);
  }

  glUniform1i(context->locationDraw.coveragePass, 0);
  GL_CHECK_ERROR;
  shResolveCoverageTarget(context);
  shRestoreRenderToMaskGLState(&state);

  return VG_TRUE;
}

static VGboolean shRenderBestPathCoverage(VGContext *context,
                                          SHPath *p,
                                          VGPaintMode mode,
                                          VGboolean *coverageEnabled)
{
  SHint scale = shCoverageScaleForSurface(context);

  *coverageEnabled = VG_FALSE;

  while (scale > 1) {
    if (shRenderPathCoverage(context, p, mode, scale)) {
      *coverageEnabled = VG_TRUE;
      return VG_TRUE;
    }
    scale /= 2;
  }

  return shRequestedCoverageScale(context) > 1 ? VG_FALSE : VG_TRUE;
}

static VGboolean shRenderPathPassToMask(VGContext *context,
                                        SHPath *p,
                                        VGPaintMode mode,
                                        VGMaskOperation operation)
{
  SHfloat mgl[16];
  VGboolean coverageEnabled;
  VGboolean renderCoverage;

  if (context->surfaceWidth <= 0 || context->surfaceHeight <= 0)
    return VG_TRUE;

  if (shRequestedCoverageScale(context) > 1) {
    if (!shRenderBestPathCoverage(context, p, mode, &coverageEnabled))
      return VG_FALSE;
    if (coverageEnabled == VG_TRUE)
      return shApplyMaskTextureToSurface(context,
                                         context->coverageTexture,
                                         operation);
  }

  if (!shEnsureRenderToMaskTarget(context))
    return VG_FALSE;

  shClearRenderToMaskTarget(context);
  renderCoverage = shSetRenderToMaskScissor(context);

  glUseProgram(context->progDraw);
  shApplyColorTransform(context);
  shMatrixToGL(&context->pathTransform, mgl);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniform1i(context->locationDraw.drawMode, 0);
  glUniform1i(context->locationDraw.maskEnabled, 0);
  glUniform1i(context->locationDraw.coverageEnabled, 0);
  glUniform1i(context->locationDraw.coveragePass, 1);
  GL_CHECK_ERROR;

  if (renderCoverage) {
    if (mode == VG_FILL_PATH)
      shRenderFillToMaskTarget(context, p);
    else
      shRenderStrokeToMaskTarget(context, p);
  }

  glUniform1i(context->locationDraw.coveragePass, 0);
  GL_CHECK_ERROR;
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

static void shTransformPathBounds(SHPath *p,
                                  const SHMatrix3x3 *transform,
                                  SHRectangle *bounds)
{
  SHVector2 corners[4];
  SHVector2 min;
  SHVector2 max;
  int i;

  SET2(corners[0], p->min.x, p->min.y);
  SET2(corners[1], p->max.x, p->min.y);
  SET2(corners[2], p->min.x, p->max.y);
  SET2(corners[3], p->max.x, p->max.y);

  TRANSFORM2(corners[0], (*transform));
  SET2V(min, corners[0]);
  SET2V(max, corners[0]);

  for (i=1; i<4; ++i) {
    TRANSFORM2(corners[i], (*transform));
    if (corners[i].x < min.x) min.x = corners[i].x;
    if (corners[i].y < min.y) min.y = corners[i].y;
    if (corners[i].x > max.x) max.x = corners[i].x;
    if (corners[i].y > max.y) max.y = corners[i].y;
  }

  shRectangleSet(bounds, min.x, min.y, max.x - min.x, max.y - min.y);
}

static VGboolean shBoundsOverlap(const SHRectangle *a, const SHRectangle *b)
{
  const SHfloat margin = 0.001f;

  if (a->x + a->w + margin < b->x ||
      b->x + b->w + margin < a->x ||
      a->y + a->h + margin < b->y ||
      b->y + b->h + margin < a->y)
    return VG_FALSE;

  return VG_TRUE;
}

static void shUpdateBatchBounds(SHVector2 *min,
                                SHVector2 *max,
                                SHVector2 point,
                                VGboolean *initialized)
{
  if (*initialized == VG_FALSE) {
    SET2V((*min), point);
    SET2V((*max), point);
    *initialized = VG_TRUE;
    return;
  }

  if (point.x < min->x) min->x = point.x;
  if (point.y < min->y) min->y = point.y;
  if (point.x > max->x) max->x = point.x;
  if (point.y > max->y) max->y = point.y;
}

SHPathGlyphBatchResult shDrawPathGlyphBatch(VGContext *context,
                                            const SHPathGlyph *glyphs,
                                            SHint glyphCount)
{
  static const GLfloat identity4[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };

  SHPaint *fill;
  SHMatrix3x3 savedTransform;
  SHRectangle *bounds = NULL;
  SHVertex *vertices = NULL;
  SHRectangle *candidateBounds;
  SHVector2 min;
  SHVector2 max;
  SHVector2 point;
  SHPath *path;
  SHRectangle *rect;
  size_t maxVertices = 0;
  SHint boundsCount = 0;
  SHint vertexCount;
  VGboolean boundsInitialized = VG_FALSE;
  SHint i;
  SHint j;
  SHint v;

  if (!context || !glyphs || glyphCount <= 0)
    return SH_PATH_GLYPH_BATCH_UNSUPPORTED;
  if (context->surfaceWidth <= 0 || context->surfaceHeight <= 0)
    return SH_PATH_GLYPH_BATCH_DRAWN;

  fill = (context->fillPaint ? context->fillPaint : &context->defaultPaint);
  if (fill->type != VG_PAINT_TYPE_COLOR ||
      shRequestedCoverageScale(context) > 1)
    return SH_PATH_GLYPH_BATCH_UNSUPPORTED;

  /* Independent non-zero glyph paths need separate winding evaluation. */
  if (context->fillRule == VG_NON_ZERO)
    return SH_PATH_GLYPH_BATCH_UNSUPPORTED;

  if (context->scissoring == VG_TRUE) {
    if (context->scissor.size == 0)
      return SH_PATH_GLYPH_BATCH_DRAWN;
    rect = &context->scissor.items[0];
    if (rect->w <= 0.0f || rect->h <= 0.0f)
      return SH_PATH_GLYPH_BATCH_DRAWN;
  }

  if ((size_t)glyphCount > ((size_t)-1) / sizeof(SHRectangle)) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  bounds = (SHRectangle*)malloc((size_t)glyphCount * sizeof(SHRectangle));
  if (!bounds) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  SETMATMAT(savedTransform, context->pathTransform);

  for (i=0; i<glyphCount; ++i) {
    path = glyphs[i].path;
    if (!path)
      continue;

    SETMATMAT(context->pathTransform, glyphs[i].transform);
    shEnsurePathGeometry(context, path);
    if (path->vertices.outofmemory) {
      SETMATMAT(context->pathTransform, savedTransform);
      free(bounds);
      shSetError(context, VG_OUT_OF_MEMORY_ERROR);
      return SH_PATH_GLYPH_BATCH_ERROR;
    }

    if (path->vertices.size <= 0)
      continue;

    if ((size_t)path->vertices.size >
        ((size_t)-1) / sizeof(SHVertex)) {
      SETMATMAT(context->pathTransform, savedTransform);
      free(bounds);
      shSetError(context, VG_OUT_OF_MEMORY_ERROR);
      return SH_PATH_GLYPH_BATCH_ERROR;
    }

    candidateBounds = &bounds[boundsCount];
    shTransformPathBounds(path, &glyphs[i].transform, candidateBounds);
    for (j=0; j<boundsCount; ++j) {
      if (shBoundsOverlap(candidateBounds, &bounds[j]) == VG_TRUE) {
        SETMATMAT(context->pathTransform, savedTransform);
        free(bounds);
        return SH_PATH_GLYPH_BATCH_UNSUPPORTED;
      }
    }

    ++boundsCount;
    if ((size_t)path->vertices.size > maxVertices)
      maxVertices = (size_t)path->vertices.size;
  }

  SETMATMAT(context->pathTransform, savedTransform);

  if (maxVertices == 0) {
    free(bounds);
    return SH_PATH_GLYPH_BATCH_DRAWN;
  }

  vertices = (SHVertex*)malloc(maxVertices * sizeof(SHVertex));
  if (!vertices) {
    free(bounds);
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  free(bounds);

  if (context->scissoring == VG_TRUE) {
    rect = &context->scissor.items[0];
    glScissor((GLint)rect->x, (GLint)rect->y,
              (GLint)rect->w, (GLint)rect->h);
    glEnable(GL_SCISSOR_TEST);
  }

  glUseProgram(context->progDraw);
  shApplyColorTransform(context);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, identity4);
  glUniform1i(context->locationDraw.drawMode, 0);
  glUniform1i(context->locationDraw.coveragePass, 0);
  GL_CHECK_ERROR;

  for (i=0; i<glyphCount; ++i) {
    path = glyphs[i].path;
    if (!path || path->vertices.size <= 0)
      continue;

    vertexCount = path->vertices.size;
    for (v=0; v<vertexCount; ++v) {
      vertices[v] = path->vertices.items[v];
      TRANSFORM2(vertices[v].point, glyphs[i].transform);
      SET2V(point, vertices[v].point);
      shUpdateBatchBounds(&min, &max, point, &boundsInitialized);
    }

    shDrawFillStencilVertices(context, vertices, vertexCount);
  }

  if (!updateBlendingStateGL(context, fill->color.a == 1.0f, 0)) {
    free(vertices);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    if (context->scissoring == VG_TRUE)
      glDisable(GL_SCISSOR_TEST);
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  shSetFillStencilPaintTest(context);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  shDrawPaintMesh(context, &min, &max, VG_FILL_PATH, GL_TEXTURE0, VG_FALSE);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  if (context->scissoring == VG_TRUE)
    glDisable(GL_SCISSOR_TEST);

  free(vertices);
  shMarkRenderTargetDirty(context);
  return SH_PATH_GLYPH_BATCH_DRAWN;
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
  VGboolean coverageEnabled;

  if (context->surfaceWidth <= 0 || context->surfaceHeight <= 0)
    return;

  /* Check whether scissoring is enabled and scissor
     rectangle is valid */
  if (context->scissoring == VG_TRUE) {
    if (context->scissor.size == 0) return;
    rect = &context->scissor.items[0];
    if (rect->w <= 0.0f || rect->h <= 0.0f) return;
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
  shApplyColorTransform(context);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniform1i(context->locationDraw.drawMode, 0); /* drawMode: path */
  glUniform1i(context->locationDraw.coveragePass, 0);
  GL_CHECK_ERROR;
  
  if (paintModes & VG_FILL_PATH) {
    if (!shRenderBestPathCoverage(context, p, VG_FILL_PATH,
                                  &coverageEnabled)) {
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_BLEND);
      if (context->scissoring == VG_TRUE)
        glDisable(GL_SCISSOR_TEST);
      shSetError(context, VG_OUT_OF_MEMORY_ERROR);
      return;
    }

    if (coverageEnabled == VG_TRUE) {
      if (!updateBlendingStateGL(context,
                                 fill->type == VG_PAINT_TYPE_COLOR &&
                                 fill->color.a == 1.0f,
                                 1)) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        if (context->scissoring == VG_TRUE)
          glDisable(GL_SCISSOR_TEST);
        shSetError(context, VG_OUT_OF_MEMORY_ERROR);
        return;
      }

      glDisable(GL_STENCIL_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      shDrawPaintMesh(context, &p->min, &p->max, VG_FILL_PATH, GL_TEXTURE0,
                      VG_TRUE);
      glDisable(GL_BLEND);
    } else {

      /* Tesselate into stencil */
      shDrawFillStencil(context, p);

      /* Setup blending */
      if (!updateBlendingStateGL(context,
                                 fill->type == VG_PAINT_TYPE_COLOR &&
                                 fill->color.a == 1.0f,
                                 0)) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        if (context->scissoring == VG_TRUE)
          glDisable(GL_SCISSOR_TEST);
        shSetError(context, VG_OUT_OF_MEMORY_ERROR);
        return;
      }

      /* Draw paint where the selected fill rule left stencil coverage. */
      shSetFillStencilPaintTest(context);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      shDrawPaintMesh(context, &p->min, &p->max, VG_FILL_PATH, GL_TEXTURE0,
                      VG_FALSE);
    }

    /* Reset state */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
  }

  if ((paintModes & VG_STROKE_PATH) &&
      context->strokeLineWidth > 0.0f) {

    if (1) {/*context->strokeLineWidth > 1.0f) {*/
      if (!shRenderBestPathCoverage(context, p, VG_STROKE_PATH,
                                    &coverageEnabled)) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_BLEND);
        if (context->scissoring == VG_TRUE)
          glDisable(GL_SCISSOR_TEST);
        shSetError(context, VG_OUT_OF_MEMORY_ERROR);
        return;
      }

      if (coverageEnabled == VG_TRUE) {
        if (!updateBlendingStateGL(context,
                                   stroke->type == VG_PAINT_TYPE_COLOR &&
                                   stroke->color.a == 1.0f,
                                   1)) {
          glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
          glDisable(GL_STENCIL_TEST);
          glDisable(GL_BLEND);
          if (context->scissoring == VG_TRUE)
            glDisable(GL_SCISSOR_TEST);
          shSetError(context, VG_OUT_OF_MEMORY_ERROR);
          return;
        }

        glDisable(GL_STENCIL_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        shDrawPaintMesh(context, &p->min, &p->max, VG_STROKE_PATH,
                        GL_TEXTURE0, VG_TRUE);
      } else {
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
        if (!updateBlendingStateGL(context,
                                   stroke->type == VG_PAINT_TYPE_COLOR &&
                                   stroke->color.a == 1.0f,
                                   0)) {
          glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
          glDisable(GL_STENCIL_TEST);
          glDisable(GL_BLEND);
          if (context->scissoring == VG_TRUE)
            glDisable(GL_SCISSOR_TEST);
          shSetError(context, VG_OUT_OF_MEMORY_ERROR);
          return;
        }

        /* Draw paint where stencil odd */
        glStencilFunc(GL_EQUAL, 1, 1);
        glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        shDrawPaintMesh(context, &p->min, &p->max, VG_STROKE_PATH,
                        GL_TEXTURE0, VG_FALSE);
      }
      
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

  return;
}

VG_API_CALL void vgDrawPath(VGPath path, VGbitfield paintModes)
{
  SHPath *p;
  VG_GETCONTEXT(VG_NO_RETVAL);

  p = shGetPath(context, path);
  VG_RETURN_ERR_IF(!p,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  shDrawPath(context, p, paintModes);

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

  p = shGetPath(context, path);
  VG_RETURN_ERR_IF(!p,
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

void shDrawImageQuadBatch(VGContext *context,
                          GLuint texture,
                          const SHImageQuad *quads,
                          SHint quadCount)
{
  static const GLfloat identity4[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };

  SHPaint *fill;
  SHRectangle *rect;
  SHVertexState vertexState;
  GLsizei vertexCount;

  if (!context || texture == 0 || !quads || quadCount <= 0)
    return;
  if (quadCount > SH_MAX_INT / 6) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return;
  }

  if (context->scissoring == VG_TRUE) {
    rect = &context->scissor.items[0];
    if (context->scissor.size == 0) return;
    if (rect->w <= 0.0f || rect->h <= 0.0f) return;
    glScissor( (GLint)rect->x, (GLint)rect->y, (GLint)rect->w, (GLint)rect->h );
    glEnable( GL_SCISSOR_TEST );
  }

  glUseProgram(context->progDraw);
  shApplyColorTransform(context);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, identity4);
  glUniform1i(context->locationDraw.drawMode, 1);
  glUniform1i(context->locationDraw.imagePremultiplied, 0);
  glUniform1i(context->locationDraw.coveragePass, 0);
  GL_CHECK_ERROR;

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (context->imageQuality == VG_IMAGE_QUALITY_NONANTIALIASED) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  }

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER,
               (GLsizeiptr)((size_t)quadCount * sizeof(SHImageQuad)),
               quads,
               GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationDraw.textureUV);
  glVertexAttribPointer(context->locationDraw.textureUV, 2, GL_FLOAT,
                        GL_FALSE, sizeof(SHImageQuadVertex),
                        (const GLvoid*)(2 * sizeof(GLfloat)));
  glUniform1i(context->locationDraw.imageSampler, 0);
  GL_CHECK_ERROR;

  fill = (context->fillPaint ? context->fillPaint : &context->defaultPaint);

  if (!updateBlendingStateGL(context, 0, 0)) {
    glDisableVertexAttribArray(context->locationDraw.textureUV);
    glDisable(GL_BLEND);
    if (context->scissoring == VG_TRUE)
      glDisable(GL_SCISSOR_TEST);
    shRestoreVertexState(&vertexState);
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return;
  }

  glEnable(GL_TEXTURE_2D);

  if (context->imageMode == VG_DRAW_IMAGE_MULTIPLY) {
    glUniform1i(context->locationDraw.imageMode, VG_DRAW_IMAGE_MULTIPLY);
    switch (fill->type) {
    case VG_PAINT_TYPE_RADIAL_GRADIENT:
      shLoadRadialGradientMesh(fill, VG_FILL_PATH,
                               VG_MATRIX_IMAGE_USER_TO_SURFACE);
      break;
    case VG_PAINT_TYPE_LINEAR_GRADIENT:
      shLoadLinearGradientMesh(fill, VG_FILL_PATH,
                               VG_MATRIX_IMAGE_USER_TO_SURFACE);
      break;
    case VG_PAINT_TYPE_PATTERN:
      shLoadPatternMesh(fill, VG_FILL_PATH,
                        VG_MATRIX_IMAGE_USER_TO_SURFACE);
      break;
    default:
    case VG_PAINT_TYPE_COLOR:
      shLoadOneColorMesh(fill);
      break;
    }
  } else {
    glUniform1i(context->locationDraw.imageMode, VG_DRAW_IMAGE_NORMAL);
  }

  glVertexAttribPointer(context->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHImageQuadVertex), (const GLvoid*)0);
  glEnableVertexAttribArray(context->locationDraw.pos);
  shApplyMaskState(context);
  shApplyCoverageState(context, VG_FALSE);
  vertexCount = (GLsizei)(quadCount * 6);
  glDrawArrays(GL_TRIANGLES, 0, vertexCount);
  glDisableVertexAttribArray(context->locationDraw.pos);
  glDisableVertexAttribArray(context->locationDraw.textureUV);
  shRestoreVertexState(&vertexState);

  glDisable(GL_TEXTURE_2D);
  GL_CHECK_ERROR;

  if (context->scissoring == VG_TRUE)
    glDisable( GL_SCISSOR_TEST );

  shMarkRenderTargetDirty(context);
  return;
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
  SHImage *root = shImageRoot(i);
  GLfloat u0;
  GLfloat u1;
  GLfloat v0;
  GLfloat v1;

  if (shImageIsRenderTarget(i)) {
    shSetError(context, VG_IMAGE_IN_USE_ERROR);
    return;
  }

  if (!root)
    return;
  
  /* Check whether scissoring is enabled and scissor
     rectangle is valid */
  if (context->scissoring == VG_TRUE) {
    rect = &context->scissor.items[0];
    if (context->scissor.size == 0) return;
    if (rect->w <= 0.0f || rect->h <= 0.0f) return;
    glScissor( (GLint)rect->x, (GLint)rect->y, (GLint)rect->w, (GLint)rect->h );
    glEnable( GL_SCISSOR_TEST );
  }
  
  /* Apply image-user-to-surface transformation */
  shMatrixToGL(&context->imageTransform, mgl);
  glUseProgram(context->progDraw);
  shApplyColorTransform(context);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, mgl);
  glUniform1i(context->locationDraw.drawMode, 1); /* drawMode: image */
  glUniform1i(context->locationDraw.imagePremultiplied, 0);
  glUniform1i(context->locationDraw.coveragePass, 0);
  GL_CHECK_ERROR;
  
  /* Clamp to edge for proper filtering, modulate for multiply mode */
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, root->texture);
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

  u0 = (GLfloat)i->storageX / (GLfloat)root->texwidth;
  u1 = (GLfloat)(i->storageX + i->width) / (GLfloat)root->texwidth;
  v0 = (GLfloat)i->storageY / (GLfloat)root->texheight;
  v1 = (GLfloat)(i->storageY + i->height) / (GLfloat)root->texheight;

  vertices[0].x = 0.0f;
  vertices[0].y = 0.0f;
  vertices[0].u = u0;
  vertices[0].v = v0;
  vertices[1].x = i->width;
  vertices[1].y = 0.0f;
  vertices[1].u = u1;
  vertices[1].v = v0;
  vertices[2].x = 0.0f;
  vertices[2].y = i->height;
  vertices[2].u = u0;
  vertices[2].v = v1;
  vertices[3].x = i->width;
  vertices[3].y = i->height;
  vertices[3].u = u1;
  vertices[3].v = v1;

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
  if (!updateBlendingStateGL(context, 0, 0)) {
    glDisableVertexAttribArray(context->locationDraw.textureUV);
    glDisable(GL_BLEND);
    if (context->scissoring == VG_TRUE)
      glDisable(GL_SCISSOR_TEST);
    shRestoreVertexState(&vertexState);
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return;
  }

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
  shApplyCoverageState(context, VG_FALSE);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(context->locationDraw.pos);
  glDisableVertexAttribArray(context->locationDraw.textureUV);
  shRestoreVertexState(&vertexState);
    
  glDisable(GL_TEXTURE_2D);
  GL_CHECK_ERROR;

  if (context->scissoring == VG_TRUE)
    glDisable( GL_SCISSOR_TEST );

  shMarkRenderTargetDirty(context);
  
  return;
}

VG_API_CALL void vgDrawImage(VGImage image)
{
  SHImage *i;
  VG_GETCONTEXT(VG_NO_RETVAL);

  i = shGetImage(context, image);
  VG_RETURN_ERR_IF(!i,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  shDrawImage(context, i);

  VG_RETURN(VG_NO_RETVAL);
}
