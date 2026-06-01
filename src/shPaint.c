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
#include "shContext.h"
#include "shPaint.h"
#include <stdio.h>

#define _ITEM_T SHStop
#define _ARRAY_T SHStopArray
#define _FUNC_T shStopArray
#define _COMPARE_T(s1,s2) 0
#define _ARRAY_DEFINE
#include "shArrayBase.h"

#define _ITEM_T SHPaint*
#define _ARRAY_T SHPaintArray
#define _FUNC_T shPaintArray
#define _ARRAY_DEFINE
#include "shArrayBase.h"

static void shEnsurePaintTexture(SHPaint *p)
{
  if (p->texture != 0)
    return;

  glGenTextures(1, &p->texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, p->texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SH_GRADIENT_TEX_WIDTH, SH_GRADIENT_TEX_HEIGHT, 0,
               GL_RGBA, GL_FLOAT, NULL);
  GL_CHECK_ERROR;
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
  GLint texture1Binding;
  GLint drawBuffer;
  GLint readBuffer;
  GLint scissorBox[4];
  GLint unpackAlignment;
  GLfloat clearColor[4];
  GLboolean blend;
  GLboolean scissor;
  GLboolean depth;
  GLboolean stencil;
  GLboolean colorMask[4];
} SHColorRampGLState;

static void shSaveColorRampGLState(SHColorRampGLState *state)
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
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->unpackAlignment);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clearColor);
  glGetBooleanv(GL_COLOR_WRITEMASK, state->colorMask);
  state->blend = glIsEnabled(GL_BLEND);
  state->scissor = glIsEnabled(GL_SCISSOR_TEST);
  state->depth = glIsEnabled(GL_DEPTH_TEST);
  state->stencil = glIsEnabled(GL_STENCIL_TEST);
  glActiveTexture(GL_TEXTURE1);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture1Binding);
  glActiveTexture(state->activeTexture);
}

static void shRestoreColorRampGLState(const SHColorRampGLState *state)
{
  if (state->blend) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);

  if (state->scissor) glEnable(GL_SCISSOR_TEST);
  else glDisable(GL_SCISSOR_TEST);

  if (state->depth) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);

  if (state->stencil) glEnable(GL_STENCIL_TEST);
  else glDisable(GL_STENCIL_TEST);

  glScissor(state->scissorBox[0], state->scissorBox[1],
            state->scissorBox[2], state->scissorBox[3]);
  glColorMask(state->colorMask[0], state->colorMask[1],
              state->colorMask[2], state->colorMask[3]);
  glClearColor(state->clearColor[0], state->clearColor[1],
               state->clearColor[2], state->clearColor[3]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, state->unpackAlignment);
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
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, state->texture1Binding);
  glActiveTexture(state->activeTexture);
}

static GLfloat shRampWindowXToClip(SHfloat x)
{
  return x / (SHfloat)SH_GRADIENT_TEX_WIDTH * 2.0f - 1.0f;
}

static void shDrawColorRampSegment(VGContext *context,
                                   SHint x1,
                                   SHint x2,
                                   SHStop *stop1,
                                   SHStop *stop2,
                                   VGboolean includeStartPixel)
{
  SHint leftPixel = includeStartPixel ? x1 : x1 + 1;
  GLfloat left;
  GLfloat right;
  GLfloat vertices[8];

  if (leftPixel > x2)
    return;

  left = shRampWindowXToClip((SHfloat)leftPixel);
  right = shRampWindowXToClip((SHfloat)(x2 + 1));

  vertices[0] = left;  vertices[1] = -1.0f;
  vertices[2] = right; vertices[3] = -1.0f;
  vertices[4] = left;  vertices[5] =  1.0f;
  vertices[6] = right; vertices[7] =  1.0f;

  glUniform4f(context->locationColorRamp.startColor,
              stop1->color.r, stop1->color.g,
              stop1->color.b, stop1->color.a);
  glUniform4f(context->locationColorRamp.endColor,
              stop2->color.r, stop2->color.g,
              stop2->color.b, stop2->color.a);
  glUniform1f(context->locationColorRamp.startPixel, (GLfloat)x1);
  glUniform1f(context->locationColorRamp.pixelSpan, (GLfloat)(x2 - x1));
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
  glVertexAttribPointer(context->locationColorRamp.pos, 2, GL_FLOAT, GL_FALSE,
                        0, (const GLvoid*)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}


void SHPaint_ctor(SHPaint *p)
{
  int i;
  
  p->type = VG_PAINT_TYPE_COLOR;
  CSET(p->color, 0,0,0,1);
  SH_INITOBJ(SHStopArray, p->instops);
  SH_INITOBJ(SHStopArray, p->stops);
  p->premultiplied = VG_FALSE;
  p->spreadMode = VG_COLOR_RAMP_SPREAD_PAD;
  p->tilingMode = VG_TILE_FILL;
  for (i=0; i<4; ++i) p->linearGradient[i] = 0.0f;
  for (i=0; i<5; ++i) p->radialGradient[i] = 0.0f;
  p->pattern = VG_INVALID_HANDLE;
  p->texture = 0;
}

void SHPaint_dtor(SHPaint *p)
{
  SH_DEINITOBJ(SHStopArray, p->instops);
  SH_DEINITOBJ(SHStopArray, p->stops);
  
  if (p->pattern != VG_INVALID_HANDLE) {
    shImageReleasePaintPatternRef((SHImage*)p->pattern);
    shImageRelease((SHImage*)p->pattern);
    p->pattern = VG_INVALID_HANDLE;
  }

  if (shCanDeleteResourceGL() &&
      p->texture != 0 &&
      glIsTexture(p->texture))
    glDeleteTextures(1, &p->texture);
}

VG_API_CALL VGPaint vgCreatePaint(void)
{
  SHPaint *p = NULL;
  VG_GETCONTEXT(VG_INVALID_HANDLE);
  
  /* Create new paint object */
  SH_NEWOBJ(SHPaint, p);
  VG_RETURN_ERR_IF(!p, VG_OUT_OF_MEMORY_ERROR,
                   VG_INVALID_HANDLE);
  
  /* Add to resource list */
  if (!shPaintArrayPushBack(&context->resources->paints, p)) {
    SH_DELETEOBJ(SHPaint, p);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  }
  
  VG_RETURN((VGPaint)p);
}

VG_API_CALL void vgDestroyPaint(VGPaint paint)
{
  SHint index;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* Check if handle valid */
  index = shPaintArrayFind(&context->resources->paints, (SHPaint*)paint);
  VG_RETURN_ERR_IF(index == -1, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  
  /* Delete object and remove resource */
  SH_DELETEOBJ(SHPaint, (SHPaint*)paint);
  shPaintArrayRemoveAt(&context->resources->paints, index);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgSetPaint(VGPaint paint, VGbitfield paintModes)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* Check if handle valid */
  VG_RETURN_ERR_IF(!shIsValidPaint(context, paint) &&
                   paint != VG_INVALID_HANDLE,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  
  /* Check for invalid mode */
  VG_RETURN_ERR_IF(paintModes & ~(VG_STROKE_PATH | VG_FILL_PATH),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  
  /* Set stroke / fill */
  if (paintModes & VG_STROKE_PATH)
    context->strokePaint = (SHPaint*)paint;
  if (paintModes & VG_FILL_PATH)
    context->fillPaint = (SHPaint*)paint;
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgPaintPattern(VGPaint paint, VGImage pattern)
{
  SHPaint *p;
  SHImage *image;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* Check if handle valid */
  VG_RETURN_ERR_IF(!shIsValidPaint(context, paint),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  
  /* Check if pattern image valid */
  VG_RETURN_ERR_IF(!shIsValidImage(context, pattern),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  
  image = (SHImage*)pattern;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(image),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  
  /* Set pattern image */
  p = (SHPaint*)paint;
  if (p->pattern != VG_INVALID_HANDLE) {
    shImageReleasePaintPatternRef((SHImage*)p->pattern);
    shImageRelease((SHImage*)p->pattern);
  }

  shImageAddRef(image);
  shImageAddPaintPatternRef(image);
  p->pattern = pattern;
  
  VG_RETURN(VG_NO_RETVAL);
}

void shUpdateColorRampTexture(SHPaint *p)
{
  SHint s=0;
  SHStop *stop1, *stop2;
  SHint x1=0, x2=0, dx;
  GLuint framebuffer = 0;
  GLenum status;
  SHColorRampGLState glState;
  SHVertexState vertexState;
  VGboolean vertexStateBound = VG_FALSE;
  SH_GETCONTEXT(SH_NO_RETVAL);

  if (!p || p->stops.size <= 0)
    return;

  shSaveColorRampGLState(&glState);

  glActiveTexture(GL_TEXTURE1);
  shEnsurePaintTexture(p);

  glGenFramebuffers(1, &framebuffer);
  if (framebuffer == 0) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    goto cleanup;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, p->texture, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    goto cleanup;
  }

  glViewport(0, 0, SH_GRADIENT_TEX_WIDTH, SH_GRADIENT_TEX_HEIGHT);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glUseProgram(context->progColorRamp);

  stop1 = &p->stops.items[0];
  glClearColor(stop1->color.r, stop1->color.g,
               stop1->color.b, stop1->color.a);
  glClear(GL_COLOR_BUFFER_BIT);

  shBindContextVertexState(context, &vertexState);
  vertexStateBound = VG_TRUE;
  glEnableVertexAttribArray(context->locationColorRamp.pos);
  
  /* Walk stops */
  for (s=1; s<p->stops.size; ++s, x1=x2, stop1=stop2) {
    
    /* Pick next stop */
    stop2 = &p->stops.items[s];
    x2 = (SHint)(stop2->offset * (SH_GRADIENT_TEX_WIDTH-1));
    
    SH_ASSERT(x1 >= 0 && x1 < SH_GRADIENT_TEX_WIDTH &&
              x2 >= 0 && x2 < SH_GRADIENT_TEX_WIDTH &&
              x1 <= x2);
    
    dx = x2 - x1;
    if (dx <= 0)
      continue;

    shDrawColorRampSegment(context, x1, x2, stop1, stop2,
                           s == 1 ? VG_TRUE : VG_FALSE);
  }

cleanup:
  if (vertexStateBound) {
    glDisableVertexAttribArray(context->locationColorRamp.pos);
    shRestoreVertexState(&vertexState);
  }
  shRestoreColorRampGLState(&glState);
  if (framebuffer != 0)
    glDeleteFramebuffers(1, &framebuffer);
  GL_CHECK_ERROR;
}

void shValidateInputStops(SHPaint *p)
{
  SHStop *instop = NULL;
  SHStop stop = {0};
  SHfloat lastOffset=0.0f;
  int i;
  
  shStopArrayClear(&p->stops);
  shStopArrayReserve(&p->stops, p->instops.size);
  
  /* Assure input stops are properly defined */
  for (i=0; i<p->instops.size; ++i) {
    
    /* Copy stop color */
    instop = &p->instops.items[i];
    stop.color = instop->color;
    
    /* Offset must be in [0,1] */
    if (instop->offset < 0.0f || instop->offset > 1.0f)
      continue;
    
    /* Discard whole sequence if not in ascending order */
    if (instop->offset < lastOffset)
      {shStopArrayClear(&p->stops); break;}
    
    /* Add stop at offset 0 with same color if first not at 0 */
    if (p->stops.size == 0 && instop->offset != 0.0f) {
      stop.offset = 0.0f;
      shStopArrayPushBackP(&p->stops, &stop);}
    
    /* Add current stop to array */
    stop.offset = instop->offset;
    shStopArrayPushBackP(&p->stops, &stop);
    
    /* Save last offset */
    lastOffset = instop->offset;
  }
  
  /* Add stop at offset 1 with same color if last not at 1 */
  if (p->stops.size > 0 && lastOffset != 1.0f) {
    stop.offset = 1.0f;
    shStopArrayPushBackP(&p->stops, &stop);
  }
  
  /* Add 2 default stops if no valid found */
  if (p->stops.size == 0) {
    /* First opaque black */
    stop.offset = 0.0f;
    CSET(stop.color, 0,0,0,1);
    shStopArrayPushBackP(&p->stops, &stop);
    /* Last opaque white */
    stop.offset = 1.0f;
    CSET(stop.color, 1,1,1,1);
    shStopArrayPushBackP(&p->stops, &stop);
  }
  
  /* Update texture */
  shUpdateColorRampTexture(p);
}

void shGenerateStops(SHPaint *p, SHfloat minOffset, SHfloat maxOffset,
                     SHStopArray *outStops)
{
  SHStop *s1,*s2;
  SHint i1,i2;
  SHfloat o=0.0f;
  SHfloat ostep=0.0f;
  SHint istep=1;
  SHint istart=0;
  SHint iend=p->stops.size-1;
  SHint minDone=0;
  SHint maxDone=0;  
  SHStop outStop;
  
  /* Start below zero? */
  if (minOffset < 0.0f) {
    if (p->spreadMode == VG_COLOR_RAMP_SPREAD_PAD) {
      /* Add min offset stop */
      outStop = p->stops.items[0];
      outStop.offset = minOffset;
      shStopArrayPushBackP(outStops, &outStop);
      /* Add max offset stop and exit */
      if (maxOffset < 0.0f) {
        outStop.offset = maxOffset;
        shStopArrayPushBackP(outStops, &outStop);
        return; }
    }else{
      /* Pad starting offset to nearest factor of 2 */
      SHint ioff = (SHint)SH_FLOOR(minOffset);
      o = (SHfloat)(ioff - (ioff & 1));
    }
  }
  
  /* Construct stops until max offset reached */
  for (i1=istart, i2=istart+istep; maxDone!=1;
       i1+=istep, i2+=istep, o+=ostep) {
    
    /* All stops consumed? */
    if (i1==iend) { switch(p->spreadMode) { 
        
      case VG_COLOR_RAMP_SPREAD_PAD:
        /* Pick last stop */
        outStop = p->stops.items[i1];
        if (!minDone) {
          /* Add min offset stop with last color */
          outStop.offset = minOffset;
          shStopArrayPushBackP(outStops, &outStop); }
        /* Add max offset stop with last color */
        outStop.offset = maxOffset;
        shStopArrayPushBackP(outStops, &outStop);
        return;
        
      case VG_COLOR_RAMP_SPREAD_REPEAT:
        /* Reset iteration */
        i1=istart; i2=istart+istep;
        /* Add stop1 if past min offset */
        if (minDone) {
          outStop = p->stops.items[0];
          outStop.offset = o;
          shStopArrayPushBackP(outStops, &outStop); }
        break;
        
      case VG_COLOR_RAMP_SPREAD_REFLECT:
        /* Reflect iteration direction */
        istep = -istep;
        i2 = i1 + istep;
        iend = (istep==1) ? p->stops.size-1 : 0;
        break;
      }
    }
    
    /* 2 stops and their offset distance */
    s1 = &p->stops.items[i1];
    s2 = &p->stops.items[i2];
    ostep = s2->offset - s1->offset;
    ostep = SH_ABS(ostep);
    
    /* Add stop1 if reached min offset */
    if (!minDone && o+ostep > minOffset) {
      minDone = 1;
      outStop = *s1;
      outStop.offset = o;
      shStopArrayPushBackP(outStops, &outStop);
    }
    
    /* Mark done if reached max offset */
    if (o+ostep > maxOffset)
      maxDone = 1;
    
    /* Add stop2 if past min offset */
    if (minDone) {
      outStop = *s2;
      outStop.offset = o+ostep;
      shStopArrayPushBackP(outStops, &outStop);
    }
  }
}

void shSetGradientTexGLState(SHPaint *p)
{
  shEnsurePaintTexture(p);
  glBindTexture(GL_TEXTURE_2D, p->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  
  switch (p->spreadMode) {
  case VG_COLOR_RAMP_SPREAD_PAD:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); break;
  case VG_COLOR_RAMP_SPREAD_REPEAT:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); break;
  case VG_COLOR_RAMP_SPREAD_REFLECT:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT); break;
  }
}

void shSetPatternTexGLState(SHPaint *p, VGContext *c)
{
  glBindTexture(GL_TEXTURE_2D, ((SHImage*)p->pattern)->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  
  switch(p->tilingMode) {
  case VG_TILE_FILL:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                     (GLfloat*)&c->tileFillColor);
    break;
  case VG_TILE_PAD:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    break;
  case VG_TILE_REPEAT:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    break;
  case VG_TILE_REFLECT:
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    break;
  }
}

int shLoadLinearGradientMesh(SHPaint *p, VGPaintMode mode, VGMatrixMode matrixMode)
{
  SHMatrix3x3 *m;
  SHMatrix3x3 mu2p;
  GLfloat u2p[9];

  /* Pick paint transform matrix */
  SH_GETCONTEXT(0);
  (void)matrixMode;
  m = &context->fillTransform;
  if (mode == VG_FILL_PATH)
    m = &context->fillTransform;
  else if (mode == VG_STROKE_PATH)
    m = &context->strokeTransform;

  /* Back to paint space */
  shInvertMatrix(m, &mu2p);
  shMatrixToVG(&mu2p, (SHfloat*)u2p);

  /* Setup shader */
  glUniform1i(context->locationDraw.paintType, VG_PAINT_TYPE_LINEAR_GRADIENT);
  glUniform2fv(context->locationDraw.paintParams, 2, p->linearGradient);
  glUniformMatrix3fv(context->locationDraw.paintInverted, 1, GL_FALSE, u2p);
  glActiveTexture(GL_TEXTURE1);
  shSetGradientTexGLState(p);
  glEnable(GL_TEXTURE_2D);
  glUniform1i(context->locationDraw.rampSampler, 1);
  GL_CHECK_ERROR;

  return 1; 
}

int shLoadRadialGradientMesh(SHPaint *p, VGPaintMode mode, VGMatrixMode matrixMode)
{
  SHMatrix3x3 *m;
  SHMatrix3x3 mu2p;
  GLfloat u2p[9];

  /* Pick paint transform matrix */
  SH_GETCONTEXT(0);
  (void)matrixMode;
  m = &context->fillTransform;
  if (mode == VG_FILL_PATH)
    m = &context->fillTransform;
  else if (mode == VG_STROKE_PATH)
    m = &context->strokeTransform;

  /* Back to paint space */
  shInvertMatrix(m, &mu2p);
  shMatrixToVG(&mu2p, (SHfloat*)u2p);

  /* Setup shader */
  glUniform1i(context->locationDraw.paintType, VG_PAINT_TYPE_RADIAL_GRADIENT);
  glUniform2fv(context->locationDraw.paintParams, 3, p->radialGradient);
  glUniformMatrix3fv(context->locationDraw.paintInverted, 1, GL_FALSE, u2p);
  glActiveTexture(GL_TEXTURE1);
  shSetGradientTexGLState(p);
  glEnable(GL_TEXTURE_2D);
  glUniform1i(context->locationDraw.rampSampler, 1);
  GL_CHECK_ERROR;

  return 1; 
}

int shLoadPatternMesh(SHPaint *p, VGPaintMode mode, VGMatrixMode matrixMode)
{
  SHImage *i = (SHImage*)p->pattern;
  SHMatrix3x3 *m;
  SHMatrix3x3 mu2p;
  GLfloat u2p[9];

  /* Pick paint transform matrix */
  SH_GETCONTEXT(0);
  (void)matrixMode;
  m = &context->fillTransform;
  if (mode == VG_FILL_PATH)
    m = &context->fillTransform;
  else if (mode == VG_STROKE_PATH)
    m = &context->strokeTransform;

  /* Back to paint space */
  shInvertMatrix(m, &mu2p);
  shMatrixToVG(&mu2p, (SHfloat*)u2p);

  /* Setup shader */
  glUniform1i(context->locationDraw.paintType, VG_PAINT_TYPE_PATTERN);
  glUniform2f(context->locationDraw.paintParams, (GLfloat)i->width, (GLfloat)i->height);
  glUniformMatrix3fv(context->locationDraw.paintInverted, 1, GL_FALSE, u2p);
  glActiveTexture(GL_TEXTURE1);
  shSetPatternTexGLState(p, context);
  glEnable(GL_TEXTURE_2D);
  glUniform1i(context->locationDraw.patternSampler, 1);
  GL_CHECK_ERROR;

  return 1; 
}

int shLoadOneColorMesh(SHPaint *p)
{
  static GLfloat id[9] = { 
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f };
  SH_GETCONTEXT(0);

  /* Setup shader */
  glUniform1i(context->locationDraw.paintType, VG_PAINT_TYPE_COLOR);
  glUniform4fv(context->locationDraw.paintColor, 1, (GLfloat*)&p->color);
  glUniformMatrix3fv(context->locationDraw.paintInverted, 1, GL_FALSE, id);

  return 1; 
}
