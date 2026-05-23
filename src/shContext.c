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
#include "shContext.h"
#include "shaders.h"
#include <string.h>
#include <stdio.h>

void shLoadExtensions(void *c);

/*-----------------------------------------------------
 * The current VG context is selected by the EGL frontend
 * when a ShaderVG-backed EGLContext is made current.
 *-----------------------------------------------------*/

#if defined(_MSC_VER)
#  define SH_TLS __declspec(thread)
#else
#  define SH_TLS __thread
#endif

static SH_TLS VGContext *g_current_context = NULL;

static VGboolean shResizeMaskSurface(VGContext *context, VGint width, VGint height)
{
  SHuint8 *maskData;
  size_t size;

  if (!context)
    return VG_FALSE;

  if (width <= 0 || height <= 0) {
    if (context->maskData) {
      free(context->maskData);
      context->maskData = NULL;
    }
    context->maskWidth = 0;
    context->maskHeight = 0;
    context->maskTextureDirty = VG_TRUE;
    return VG_TRUE;
  }

  if (context->maskData &&
      context->maskWidth == width &&
      context->maskHeight == height)
    return VG_TRUE;

  size = (size_t)width * (size_t)height;
  maskData = (SHuint8*)malloc(size);
  if (!maskData)
    return VG_FALSE;

  memset(maskData, 255, size);

  if (context->maskData)
    free(context->maskData);

  context->maskData = maskData;
  context->maskWidth = width;
  context->maskHeight = height;
  context->maskTextureDirty = VG_TRUE;

  if (context->glInitialized && context->maskTexture != 0) {
    glDeleteTextures(1, &context->maskTexture);
    context->maskTexture = 0;
  }

  return VG_TRUE;
}

void shEnsureMaskTexture(VGContext *context)
{
  if (!context || !context->maskData ||
      context->maskWidth <= 0 || context->maskHeight <= 0)
    return;

  if (context->maskTexture == 0)
    glGenTextures(1, &context->maskTexture);

  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, context->maskTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  if (context->maskTextureDirty) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 context->maskWidth, context->maskHeight, 0,
                 GL_RED, GL_UNSIGNED_BYTE, context->maskData);
    context->maskTextureDirty = VG_FALSE;
  }

  GL_CEHCK_ERROR;
}

static void shResizeSurface(VGContext *context, VGint width, VGint height)
{
  float mat[16];
  float volume;

  if (!context)
    return;

  context->surfaceWidth = width;
  context->surfaceHeight = height;
  if (!shResizeMaskSurface(context, width, height))
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);

  glViewport(0, 0, width, height);

  if (context->glInitialized) {
    volume = fmax(width, height) / 2;
    shCalcOrtho2D(mat, 0, width, 0, height, -volume, volume);
    glUseProgram(context->progDraw);
    glUniformMatrix4fv(context->locationDraw.projection, 1, GL_FALSE, mat);
    glUniform2f(context->locationDraw.maskSurfaceSize,
                (GLfloat)width, (GLfloat)height);
    GL_CEHCK_ERROR;
  }
}

static VGboolean shInitContextGL(VGContext *context, VGint width, VGint height)
{
  if (!context)
    return VG_FALSE;

  shResizeSurface(context, width, height);

  if (context->glInitialized)
    return VG_TRUE;

  shLoadExtensions(context);
  shInitPiplelineShaders();
  shInitRampShaders();
  context->glInitialized = VG_TRUE;
  shResizeSurface(context, width, height);

  return VG_TRUE;
}

VGContext* shGetContext()
{
  return g_current_context;
}

VGContext* shCreateContext(void)
{
  VGContext *context = NULL;
  SH_NEWOBJ(VGContext, context);
  return context;
}

void shDestroyContext(VGContext *context)
{
  VGContext *previous = g_current_context;

  if (!context)
    return;

  if (context->glInitialized) {
    g_current_context = context;
    shDeinitPiplelineShaders();
    shDeinitRampShaders();
    context->glInitialized = VG_FALSE;
  }

  SH_DELETEOBJ(VGContext, context);

  if (previous == context)
    g_current_context = NULL;
  else
    g_current_context = previous;
}

VGboolean shSetCurrentContext(VGContext *context, VGint width, VGint height)
{
  g_current_context = context;

  if (!context)
    return VG_TRUE;

  return shInitContextGL(context, width, height);
}

void shClearCurrentContext(void)
{
  g_current_context = NULL;
}

void shResizeCurrentSurface(VGint width, VGint height)
{
  shResizeSurface(g_current_context, width, height);
}

/*-----------------------------------------------------
 * VGContext constructor
 *-----------------------------------------------------*/

void VGContext_ctor(VGContext *c)
{
  /* Surface info */
  c->surfaceWidth = 0;
  c->surfaceHeight = 0;
  
  /* GetString info */
  strncpy(c->vendor, "Takuma Hayashi", sizeof(c->vendor));
  strncpy(c->renderer, "ShaderVG", sizeof(c->renderer));
  strncpy(c->version, "1.0.0", sizeof(c->version));
  strncpy(c->extensions, "", sizeof(c->extensions));
  
  /* Mode settings */
  c->matrixMode = VG_MATRIX_PATH_USER_TO_SURFACE;
  c->fillRule = VG_EVEN_ODD;
  c->imageQuality = VG_IMAGE_QUALITY_FASTER;
  c->renderingQuality = VG_RENDERING_QUALITY_BETTER;
  c->blendMode = VG_BLEND_SRC_OVER;
  c->imageMode = VG_DRAW_IMAGE_NORMAL;
  
  /* Scissor rectangles */
  SH_INITOBJ(SHRectArray, c->scissor);
  c->scissoring = VG_FALSE;
  c->masking = VG_FALSE;
  c->maskData = NULL;
  c->maskWidth = 0;
  c->maskHeight = 0;
  c->maskTexture = 0;
  c->maskTextureDirty = VG_TRUE;
  
  /* Stroke parameters */
  c->strokeLineWidth = 1.0f;
  c->strokeCapStyle = VG_CAP_BUTT;
  c->strokeJoinStyle = VG_JOIN_MITER;
  c->strokeMiterLimit = 4.0f;
  c->strokeDashPhase = 0.0f;
  c->strokeDashPhaseReset = VG_FALSE;
  SH_INITOBJ(SHFloatArray, c->strokeDashPattern);
  
  /* Edge fill color for vgConvolve and pattern paint */
  CSET(c->tileFillColor, 0,0,0,0);
  
  /* Color for vgClear */
  CSET(c->clearColor, 0,0,0,0);
  
  /* Color components layout inside pixel */
  c->pixelLayout = VG_PIXEL_LAYOUT_UNKNOWN;
  
  /* Source format for image filters */
  c->filterFormatLinear = VG_FALSE;
  c->filterFormatPremultiplied = VG_FALSE;
  c->filterChannelMask = VG_RED|VG_GREEN|VG_BLUE|VG_ALPHA;
  
  /* Matrices */
  SH_INITOBJ(SHMatrix3x3, c->pathTransform);
  SH_INITOBJ(SHMatrix3x3, c->imageTransform);
  SH_INITOBJ(SHMatrix3x3, c->fillTransform);
  SH_INITOBJ(SHMatrix3x3, c->strokeTransform);
  SH_INITOBJ(SHMatrix3x3, c->glyphTransform);
  SET2(c->glyphOrigin, 0.0f, 0.0f);
  
  /* Paints */
  c->fillPaint = NULL;
  c->strokePaint = NULL;
  SH_INITOBJ(SHPaint, c->defaultPaint);
  
  /* Error */
  c->error = VG_NO_ERROR;
  
  /* Resources */
  SH_INITOBJ(SHPathArray, c->paths);
  SH_INITOBJ(SHPaintArray, c->paints);
  SH_INITOBJ(SHImageArray, c->images);
  SH_INITOBJ(SHFontArray, c->fonts);
  
  /* GL state is initialized lazily after EGL makes the context current */
  c->progDraw = 0;
  c->progColorRamp = 0;
  c->userShaderVertex = NULL;
  c->userShaderFragment = NULL;
  c->vs = 0;
  c->fs = 0;
  c->glInitialized = VG_FALSE;
}

/*-----------------------------------------------------
 * VGContext constructor
 *-----------------------------------------------------*/

void VGContext_dtor(VGContext *c)
{
  int i;
  
  SH_DEINITOBJ(SHRectArray, c->scissor);
  SH_DEINITOBJ(SHFloatArray, c->strokeDashPattern);

  if (c->maskData) {
    free(c->maskData);
    c->maskData = NULL;
  }

  if (c->maskTexture != 0 && glIsTexture(c->maskTexture))
    glDeleteTextures(1, &c->maskTexture);
  
  /* Destroy resources */
  for (i=0; i<c->fonts.size; ++i)
    SH_DELETEOBJ(SHFont, c->fonts.items[i]);

  for (i=0; i<c->paths.size; ++i)
    shPathRelease(c->paths.items[i]);
  
  for (i=0; i<c->paints.size; ++i)
    SH_DELETEOBJ(SHPaint, c->paints.items[i]);
  
  for (i=0; i<c->images.size; ++i)
    shImageRelease(c->images.items[i]);

  SH_DEINITOBJ(SHPaint, c->defaultPaint);
  SH_DEINITOBJ(SHFontArray, c->fonts);
  SH_DEINITOBJ(SHPathArray, c->paths);
  SH_DEINITOBJ(SHPaintArray, c->paints);
  SH_DEINITOBJ(SHImageArray, c->images);
}

/*--------------------------------------------------
 * Tries to find resources in this context
 *--------------------------------------------------*/

SHint shIsValidPath(VGContext *c, VGHandle h)
{
  int index = shPathArrayFind(&c->paths, (SHPath*)h);
  return (index == -1) ? 0 : 1;
}

SHint shIsValidPaint(VGContext *c, VGHandle h)
{
  int index = shPaintArrayFind(&c->paints, (SHPaint*)h);
  return (index == -1) ? 0 : 1;
}

SHint shIsValidImage(VGContext *c, VGHandle h)
{
  int index = shImageArrayFind(&c->images, (SHImage*)h);
  return (index == -1) ? 0 : 1;
}

SHint shIsValidFont(VGContext *c, VGHandle h)
{
  int index = shFontArrayFind(&c->fonts, (SHFont*)h);
  return (index == -1) ? 0 : 1;
}

/*--------------------------------------------------
 * Tries to find a resources in this context and
 * return its type or invalid flag.
 *--------------------------------------------------*/

SHResourceType shGetResourceType(VGContext *c, VGHandle h)
{
  if (shIsValidPath(c, h))
    return SH_RESOURCE_PATH;
  
  else if (shIsValidPaint(c, h))
    return SH_RESOURCE_PAINT;
  
  else if (shIsValidImage(c, h))
    return SH_RESOURCE_IMAGE;

  else if (shIsValidFont(c, h))
    return SH_RESOURCE_FONT;
  
  else
    return SH_RESOURCE_INVALID;
}

/*-----------------------------------------------------
 * Sets the specified error on the given context if
 * there is no pending error yet
 *-----------------------------------------------------*/

void shSetError(VGContext *c, VGErrorCode e)
{
  if (c->error == VG_NO_ERROR)
    c->error = e;
}

/*--------------------------------------------------
 * Returns the oldest error pending on the current
 * context and clears its error code
 *--------------------------------------------------*/

VG_API_CALL VGErrorCode vgGetError(void)
{
  VGErrorCode error;
  VG_GETCONTEXT(VG_NO_CONTEXT_ERROR);
  error = context->error;
  context->error = VG_NO_ERROR;
  VG_RETURN(error);
}

VG_API_CALL void vgFlush(void)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  glFlush();
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgFinish(void)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  glFinish();
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgMask(VGImage mask, VGMaskOperation operation,
                        VGint x, VGint y, VGint width, VGint height)
{
  SHImage *image = NULL;
  long long rectX0, rectY0, rectX1, rectY1;
  long long surfX0, surfY0, surfX1, surfY1;
  long long sx, sy;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(operation != VG_CLEAR_MASK &&
                   operation != VG_FILL_MASK &&
                   operation != VG_SET_MASK &&
                   operation != VG_UNION_MASK &&
                   operation != VG_INTERSECT_MASK &&
                   operation != VG_SUBTRACT_MASK,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  if (operation != VG_CLEAR_MASK && operation != VG_FILL_MASK) {
    VG_RETURN_ERR_IF(!shIsValidImage(context, mask),
                     VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
    image = (SHImage*)mask;
  }

  if (!shResizeMaskSurface(context, context->surfaceWidth, context->surfaceHeight))
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (!context->maskData ||
      context->maskWidth <= 0 ||
      context->maskHeight <= 0)
    VG_RETURN(VG_NO_RETVAL);

  rectX0 = x;
  rectY0 = y;
  rectX1 = (long long)x + (long long)width;
  rectY1 = (long long)y + (long long)height;

  if (image) {
    long long imageX1 = (long long)x + (long long)image->width;
    long long imageY1 = (long long)y + (long long)image->height;
    if (rectX1 > imageX1) rectX1 = imageX1;
    if (rectY1 > imageY1) rectY1 = imageY1;
  }

  surfX0 = rectX0 < 0 ? 0 : rectX0;
  surfY0 = rectY0 < 0 ? 0 : rectY0;
  surfX1 = rectX1 > context->maskWidth ? context->maskWidth : rectX1;
  surfY1 = rectY1 > context->maskHeight ? context->maskHeight : rectY1;

  if (surfX1 <= surfX0 || surfY1 <= surfY0)
    VG_RETURN(VG_NO_RETVAL);

  for (sy=surfY0; sy<surfY1; ++sy) {
    SHuint8 *dst = context->maskData + sy * context->maskWidth + surfX0;
    const SHuint8 *src = NULL;

    if (image) {
      long long imageY = sy - y;
      long long imageX = surfX0 - x;
      src = image->data + imageY * image->texwidth * image->fd.bytes +
            imageX * image->fd.bytes;
    }

    for (sx=surfX0; sx<surfX1; ++sx, ++dst) {
      SHuint8 oldMask = *dst;
      SHuint8 newMask = 255;

      if (image) {
        SHColor color;
        shLoadColor(&color, src, &image->fd);
        SH_CLAMP(color.a, 0.0f, 1.0f);
        newMask = (SHuint8)(color.a * 255.0f + 0.5f);
        src += image->fd.bytes;
      }

      switch (operation) {
      case VG_CLEAR_MASK:
        *dst = 0;
        break;
      case VG_FILL_MASK:
        *dst = 255;
        break;
      case VG_SET_MASK:
        *dst = newMask;
        break;
      case VG_UNION_MASK:
        *dst = (SHuint8)(255 - (((255 - oldMask) * (255 - newMask) + 127) / 255));
        break;
      case VG_INTERSECT_MASK:
        *dst = (SHuint8)((oldMask * newMask + 127) / 255);
        break;
      case VG_SUBTRACT_MASK:
        *dst = (SHuint8)((oldMask * (255 - newMask) + 127) / 255);
        break;
      default:
        break;
      }
    }
  }

  context->maskTextureDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgClear(VGint x, VGint y, VGint width, VGint height)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* Clip to window */
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (width > context->surfaceWidth) width = context->surfaceWidth;
  if (height > context->surfaceHeight) height = context->surfaceHeight;
  
  /* Check if scissoring needed */
  if (x > 0 || y > 0 ||
      width < context->surfaceWidth ||
      height < context->surfaceHeight) {
    
    glScissor(x, y, width, height);
    glEnable(GL_SCISSOR_TEST);
  }
  
  /* Clear GL color buffer */
  /* TODO: what about stencil and depth? when do we clear that?
     we would need some kind of special "begin" function at
     beginning of each drawing or clear the planes prior to each
     drawing where it takes places */
  glClearColor(context->clearColor.r,
               context->clearColor.g,
               context->clearColor.b,
               context->clearColor.a);
  
  glClear(GL_COLOR_BUFFER_BIT |
          GL_STENCIL_BUFFER_BIT |
          GL_DEPTH_BUFFER_BIT);
  
  glDisable(GL_SCISSOR_TEST);
  
  VG_RETURN(VG_NO_RETVAL);
}

/*-----------------------------------------------------------
 * Returns the matrix currently selected via VG_MATRIX_MODE
 *-----------------------------------------------------------*/

SHMatrix3x3* shCurrentMatrix(VGContext *c)
{
  switch(c->matrixMode) {
  case VG_MATRIX_PATH_USER_TO_SURFACE:
    return &c->pathTransform;
  case VG_MATRIX_IMAGE_USER_TO_SURFACE:
    return &c->imageTransform;
  case VG_MATRIX_FILL_PAINT_TO_USER:
    return &c->fillTransform;
  case VG_MATRIX_STROKE_PAINT_TO_USER:
    return &c->strokeTransform;
  default:
    return &c->glyphTransform;
  }
}

/*--------------------------------------
 * Sets the current matrix to identity
 *--------------------------------------*/

VG_API_CALL void vgLoadIdentity(void)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  IDMAT((*m));
  
  VG_RETURN(VG_NO_RETVAL);
}

/*-------------------------------------------------------------
 * Loads values into the current matrix from the given array.
 * Matrix affinity is preserved if an affine matrix is loaded.
 *-------------------------------------------------------------*/

VG_API_CALL void vgLoadMatrix(const VGfloat * mm)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!mm, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  /* TODO: check matrix array alignment */
  
  m = shCurrentMatrix(context);

  if (context->matrixMode == VG_MATRIX_IMAGE_USER_TO_SURFACE) {
    
    SETMAT((*m),
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           mm[2], mm[5], mm[8]);
  }else{
    
    SETMAT((*m),
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           0.0f,  0.0f,  1.0f);
  }
  
  VG_RETURN(VG_NO_RETVAL);
}

/*---------------------------------------------------------------
 * Outputs the values of the current matrix into the given array
 *---------------------------------------------------------------*/

VG_API_CALL void vgGetMatrix(VGfloat * mm)
{
  SHMatrix3x3 *m; int i,j,k=0;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!mm, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  /* TODO: check matrix array alignment */
  
  m = shCurrentMatrix(context);
  
  for (i=0; i<3; ++i)
    for (j=0; j<3; ++j)
      mm[k++] = m->m[j][i];
  
  VG_RETURN(VG_NO_RETVAL);
}

/*-------------------------------------------------------------
 * Right-multiplies the current matrix with the one specified
 * in the given array. Matrix affinity is preserved if an
 * affine matrix is begin multiplied.
 *-------------------------------------------------------------*/

VG_API_CALL void vgMultMatrix(const VGfloat * mm)
{
  SHMatrix3x3 *m, mul, temp;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!mm, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  /* TODO: check matrix array alignment */
  
  m = shCurrentMatrix(context);
  
  if (context->matrixMode == VG_MATRIX_IMAGE_USER_TO_SURFACE) {
    
    SETMAT(mul,
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           mm[2], mm[5], mm[8]);
  }else{
    
    SETMAT(mul,
           mm[0], mm[3], mm[6],
           mm[1], mm[4], mm[7],
           0.0f,  0.0f,  1.0f);
  }
  
  MULMATMAT((*m), mul, temp);
  SETMATMAT((*m), temp);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgTranslate(VGfloat tx, VGfloat ty)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  TRANSLATEMATR((*m), tx, ty);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgScale(VGfloat sx, VGfloat sy)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  SCALEMATR((*m), sx, sy);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgShear(VGfloat shx, VGfloat shy)
{
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  m = shCurrentMatrix(context);
  SHEARMATR((*m), shx, shy);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgRotate(VGfloat angle)
{
  SHfloat a;
  SHMatrix3x3 *m;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  a = SH_DEG2RAD(angle);
  m = shCurrentMatrix(context);
  ROTATEMATR((*m), a);
  
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL VGHardwareQueryResult vgHardwareQuery(VGHardwareQueryType key,
                                                  VGint setting)
{
  return VG_HARDWARE_UNACCELERATED;
}
