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

#define _ITEM_T SHMaskLayer*
#define _ARRAY_T SHMaskLayerArray
#define _FUNC_T shMaskLayerArray
#define _ARRAY_DEFINE
#include "shArrayBase.h"

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

#define SH_MASK_SOURCE_CONSTANT 0
#define SH_MASK_SOURCE_RED      1
#define SH_MASK_SOURCE_ALPHA    2

static const char* shMaskVertexShaderSource =
"#version 330\n"
"\n"
"in vec2 pos;\n"
"in vec2 texCoord;\n"
"uniform vec2 targetSize;\n"
"out vec2 sourceCoord;\n"
"\n"
"void main()\n"
"{\n"
"    vec2 clip = vec2(pos.x / targetSize.x * 2.0 - 1.0,\n"
"                     pos.y / targetSize.y * 2.0 - 1.0);\n"
"    gl_Position = vec4(clip, 0.0, 1.0);\n"
"    sourceCoord = texCoord;\n"
"}\n";

static const char* shMaskFragmentShaderSource =
"#version 330\n"
"\n"
"in vec2 sourceCoord;\n"
"uniform sampler2D sourceSampler;\n"
"uniform int sourceMode;\n"
"uniform float maskValue;\n"
"out vec4 fragColor;\n"
"\n"
"void main()\n"
"{\n"
"    float coverage = maskValue;\n"
"    if (sourceMode == 1)\n"
"        coverage = texture(sourceSampler, sourceCoord).r;\n"
"    else if (sourceMode == 2)\n"
"        coverage = texture(sourceSampler, sourceCoord).a;\n"
"    fragColor = vec4(coverage, coverage, coverage, coverage);\n"
"}\n";

void SHMaskLayer_ctor(SHMaskLayer *m)
{
  m->texture = 0;
  m->framebuffer = 0;
  m->width = 0;
  m->height = 0;
}

void SHMaskLayer_dtor(SHMaskLayer *m)
{
  if (m->texture != 0)
    glDeleteTextures(1, &m->texture);

  if (m->framebuffer != 0)
    glDeleteFramebuffers(1, &m->framebuffer);

  m->texture = 0;
  m->framebuffer = 0;
  m->width = 0;
  m->height = 0;
}

static VGboolean shConfigureMaskTarget(GLuint *texture,
                                       GLuint *framebuffer,
                                       VGint width, VGint height,
                                       VGfloat value)
{
  GLint previousFramebuffer;
  GLint previousViewport[4];
  GLint previousActiveTexture;
  GLint previousMaskTextureBinding;
  GLint previousDrawBuffer;
  GLint previousReadBuffer;
  GLfloat previousClearColor[4];
  GLboolean previousScissor;
  GLboolean previousBlend;
  GLboolean previousColorMask[4];
  GLenum status;

  if (!texture || !framebuffer || width <= 0 || height <= 0)
    return VG_FALSE;

  if (*texture == 0)
    glGenTextures(1, texture);
  if (*framebuffer == 0)
    glGenFramebuffers(1, framebuffer);

  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
  previousScissor = glIsEnabled(GL_SCISSOR_TEST);
  previousBlend = glIsEnabled(GL_BLEND);
  glGetBooleanv(GL_COLOR_WRITEMASK, previousColorMask);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

  glActiveTexture(SH_TEXTURE_MASK);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousMaskTextureBinding);
  glBindTexture(GL_TEXTURE_2D, *texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0,
               GL_RED, GL_UNSIGNED_BYTE, NULL);

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);

  glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, *texture, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
    glDrawBuffer(previousDrawBuffer);
    glReadBuffer(previousReadBuffer);
    glViewport(previousViewport[0], previousViewport[1],
               previousViewport[2], previousViewport[3]);
    glActiveTexture(SH_TEXTURE_MASK);
    glBindTexture(GL_TEXTURE_2D, previousMaskTextureBinding);
    glActiveTexture(previousActiveTexture);
    return VG_FALSE;
  }

  glViewport(0, 0, width, height);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(value, value, value, value);
  glClear(GL_COLOR_BUFFER_BIT);

  glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
  glDrawBuffer(previousDrawBuffer);
  glReadBuffer(previousReadBuffer);
  glViewport(previousViewport[0], previousViewport[1],
             previousViewport[2], previousViewport[3]);
  if (previousScissor) glEnable(GL_SCISSOR_TEST);
  else glDisable(GL_SCISSOR_TEST);
  if (previousBlend) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);
  glColorMask(previousColorMask[0], previousColorMask[1],
              previousColorMask[2], previousColorMask[3]);
  glClearColor(previousClearColor[0], previousClearColor[1],
               previousClearColor[2], previousClearColor[3]);
  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, previousMaskTextureBinding);
  glActiveTexture(previousActiveTexture);
  GL_CEHCK_ERROR;

  return VG_TRUE;
}

static VGboolean shResizeMaskSurface(VGContext *context, VGint width, VGint height)
{
  if (!context)
    return VG_FALSE;

  if (width <= 0 || height <= 0) {
    if (context->maskTexture != 0) {
      glDeleteTextures(1, &context->maskTexture);
      context->maskTexture = 0;
    }
    if (context->maskFramebuffer != 0) {
      glDeleteFramebuffers(1, &context->maskFramebuffer);
      context->maskFramebuffer = 0;
    }
    context->maskWidth = 0;
    context->maskHeight = 0;
    return VG_TRUE;
  }

  if (context->maskTexture != 0 &&
      context->maskFramebuffer != 0 &&
      context->maskWidth == width &&
      context->maskHeight == height)
    return VG_TRUE;

  if (!shConfigureMaskTarget(&context->maskTexture,
                             &context->maskFramebuffer,
                             width, height, 1.0f))
    return VG_FALSE;

  context->maskWidth = width;
  context->maskHeight = height;

  return VG_TRUE;
}

void shEnsureMaskTexture(VGContext *context)
{
  if (!context ||
      context->maskWidth <= 0 || context->maskHeight <= 0)
    return;

  if (context->maskTexture == 0)
    shResizeMaskSurface(context, context->surfaceWidth, context->surfaceHeight);

  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, context->maskTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  GL_CEHCK_ERROR;
}

static VGboolean shInitMaskProgram(VGContext *context)
{
  GLint compileStatus;

  context->maskVs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(context->maskVs, 1, &shMaskVertexShaderSource, NULL);
  glCompileShader(context->maskVs);
  glGetShaderiv(context->maskVs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  context->maskFs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(context->maskFs, 1, &shMaskFragmentShaderSource, NULL);
  glCompileShader(context->maskFs);
  glGetShaderiv(context->maskFs, GL_COMPILE_STATUS, &compileStatus);
  printf("Shader compile status :%d line:%d\n", compileStatus, __LINE__);
  GL_CEHCK_ERROR;

  context->progMask = glCreateProgram();
  glAttachShader(context->progMask, context->maskVs);
  glAttachShader(context->progMask, context->maskFs);
  glLinkProgram(context->progMask);
  GL_CEHCK_ERROR;

  context->locationMask.pos =
    glGetAttribLocation(context->progMask, "pos");
  context->locationMask.texCoord =
    glGetAttribLocation(context->progMask, "texCoord");
  context->locationMask.targetSize =
    glGetUniformLocation(context->progMask, "targetSize");
  context->locationMask.sourceSampler =
    glGetUniformLocation(context->progMask, "sourceSampler");
  context->locationMask.sourceMode =
    glGetUniformLocation(context->progMask, "sourceMode");
  context->locationMask.maskValue =
    glGetUniformLocation(context->progMask, "maskValue");
  GL_CEHCK_ERROR;

  glUseProgram(context->progMask);
  glUniform1i(context->locationMask.sourceSampler, SH_TEXTURE_MASK_INDEX);
  GL_CEHCK_ERROR;

  return VG_TRUE;
}

static void shDeinitMaskProgram(VGContext *context)
{
  if (context->maskVs != 0) {
    glDeleteShader(context->maskVs);
    context->maskVs = 0;
  }

  if (context->maskFs != 0) {
    glDeleteShader(context->maskFs);
    context->maskFs = 0;
  }

  if (context->progMask != 0) {
    glDeleteProgram(context->progMask);
    context->progMask = 0;
  }
}

static void shResizeSurface(VGContext *context, VGint width, VGint height)
{
  float mat[16];
  float volume;

  if (!context)
    return;

  context->surfaceWidth = width;
  context->surfaceHeight = height;
  if ((context->maskTexture != 0 ||
       context->maskFramebuffer != 0 ||
       context->maskWidth > 0 ||
       context->maskHeight > 0) &&
      !shResizeMaskSurface(context, width, height))
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
    shDeinitMaskProgram(context);
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
  c->maskWidth = 0;
  c->maskHeight = 0;
  c->maskTexture = 0;
  c->maskFramebuffer = 0;
  
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
  SH_INITOBJ(SHMaskLayerArray, c->maskLayers);
  
  /* GL state is initialized lazily after EGL makes the context current */
  c->progDraw = 0;
  c->progColorRamp = 0;
  c->progMask = 0;
  c->userShaderVertex = NULL;
  c->userShaderFragment = NULL;
  c->vs = 0;
  c->fs = 0;
  c->maskVs = 0;
  c->maskFs = 0;
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

  if (c->maskTexture != 0)
    glDeleteTextures(1, &c->maskTexture);
  if (c->maskFramebuffer != 0)
    glDeleteFramebuffers(1, &c->maskFramebuffer);
  
  /* Destroy resources */
  for (i=0; i<c->fonts.size; ++i)
    SH_DELETEOBJ(SHFont, c->fonts.items[i]);

  for (i=0; i<c->maskLayers.size; ++i)
    SH_DELETEOBJ(SHMaskLayer, c->maskLayers.items[i]);

  for (i=0; i<c->paths.size; ++i)
    shPathRelease(c->paths.items[i]);
  
  for (i=0; i<c->paints.size; ++i)
    SH_DELETEOBJ(SHPaint, c->paints.items[i]);
  
  for (i=0; i<c->images.size; ++i)
    shImageRelease(c->images.items[i]);

  SH_DEINITOBJ(SHPaint, c->defaultPaint);
  SH_DEINITOBJ(SHMaskLayerArray, c->maskLayers);
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

SHint shIsValidMaskLayer(VGContext *c, VGHandle h)
{
  int index = shMaskLayerArrayFind(&c->maskLayers, (SHMaskLayer*)h);
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

  else if (shIsValidMaskLayer(c, h))
    return SH_RESOURCE_MASK_LAYER;
  
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

typedef struct
{
  GLint framebuffer;
  GLint viewport[4];
  GLint program;
  GLint activeTexture;
  GLint maskTextureBinding;
  GLint scissorBox[4];
  GLint blendSrcRgb;
  GLint blendDstRgb;
  GLint blendSrcAlpha;
  GLint blendDstAlpha;
  GLint blendEquationRgb;
  GLint blendEquationAlpha;
  GLboolean blend;
  GLboolean scissor;
  GLboolean depth;
  GLboolean stencil;
  GLboolean colorMask[4];
} SHMaskGLState;

static void shSaveMaskGLState(SHMaskGLState *state)
{
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state->activeTexture);
  glGetIntegerv(GL_SCISSOR_BOX, state->scissorBox);
  glGetIntegerv(GL_BLEND_SRC_RGB, &state->blendSrcRgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &state->blendDstRgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &state->blendSrcAlpha);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &state->blendDstAlpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &state->blendEquationRgb);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state->blendEquationAlpha);
  glGetBooleanv(GL_COLOR_WRITEMASK, state->colorMask);
  state->blend = glIsEnabled(GL_BLEND);
  state->scissor = glIsEnabled(GL_SCISSOR_TEST);
  state->depth = glIsEnabled(GL_DEPTH_TEST);
  state->stencil = glIsEnabled(GL_STENCIL_TEST);
  glActiveTexture(SH_TEXTURE_MASK);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->maskTextureBinding);
}

static void shRestoreMaskGLState(const SHMaskGLState *state)
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
  glScissor(state->scissorBox[0], state->scissorBox[1],
            state->scissorBox[2], state->scissorBox[3]);
  glColorMask(state->colorMask[0], state->colorMask[1],
              state->colorMask[2], state->colorMask[3]);
  glUseProgram(state->program);
  glBindFramebuffer(GL_FRAMEBUFFER, state->framebuffer);
  glViewport(state->viewport[0], state->viewport[1],
             state->viewport[2], state->viewport[3]);
  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, state->maskTextureBinding);
  glActiveTexture(state->activeTexture);
}

static void shSetMaskBlendOperation(VGMaskOperation operation)
{
  glBlendEquation(GL_FUNC_ADD);

  switch (operation) {
  case VG_UNION_MASK:
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
    break;
  case VG_INTERSECT_MASK:
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    break;
  case VG_SUBTRACT_MASK:
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
    break;
  default:
    glDisable(GL_BLEND);
    break;
  }
}

static void shDrawMaskRect(VGContext *context,
                           GLuint framebuffer,
                           VGint targetWidth, VGint targetHeight,
                           VGint x, VGint y,
                           VGint width, VGint height,
                           GLuint sourceTexture,
                           VGint sourceMode,
                           VGfloat maskValue,
                           VGfloat s0, VGfloat t0,
                           VGfloat s1, VGfloat t1,
                           VGMaskOperation operation)
{
  GLfloat positions[8];
  GLfloat texCoords[8];

  if (context->progMask == 0)
    shInitMaskProgram(context);

  positions[0] = (GLfloat)x;
  positions[1] = (GLfloat)y;
  positions[2] = (GLfloat)(x + width);
  positions[3] = (GLfloat)y;
  positions[4] = (GLfloat)x;
  positions[5] = (GLfloat)(y + height);
  positions[6] = (GLfloat)(x + width);
  positions[7] = (GLfloat)(y + height);

  texCoords[0] = s0; texCoords[1] = t0;
  texCoords[2] = s1; texCoords[3] = t0;
  texCoords[4] = s0; texCoords[5] = t1;
  texCoords[6] = s1; texCoords[7] = t1;

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glViewport(0, 0, targetWidth, targetHeight);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glEnable(GL_SCISSOR_TEST);
  glScissor(x, y, width, height);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  shSetMaskBlendOperation(operation);

  glUseProgram(context->progMask);
  glUniform2f(context->locationMask.targetSize,
              (GLfloat)targetWidth, (GLfloat)targetHeight);
  glUniform1i(context->locationMask.sourceMode, sourceMode);
  glUniform1f(context->locationMask.maskValue, maskValue);
  glUniform1i(context->locationMask.sourceSampler, SH_TEXTURE_MASK_INDEX);

  glActiveTexture(SH_TEXTURE_MASK);
  glBindTexture(GL_TEXTURE_2D, sourceTexture);
  if (sourceMode != SH_MASK_SOURCE_CONSTANT && sourceTexture != 0) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  }

  glEnableVertexAttribArray(context->locationMask.pos);
  glVertexAttribPointer(context->locationMask.pos, 2,
                        GL_FLOAT, GL_FALSE, 0, positions);
  glEnableVertexAttribArray(context->locationMask.texCoord);
  glVertexAttribPointer(context->locationMask.texCoord, 2,
                        GL_FLOAT, GL_FALSE, 0, texCoords);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(context->locationMask.texCoord);
  glDisableVertexAttribArray(context->locationMask.pos);
  GL_CEHCK_ERROR;
}

static VGint shMaskSourceModeForImage(SHImage *image)
{
  return image->fd.amask ? SH_MASK_SOURCE_ALPHA : SH_MASK_SOURCE_RED;
}

VG_API_CALL void vgMask(VGHandle mask, VGMaskOperation operation,
                        VGint x, VGint y, VGint width, VGint height)
{
  SHImage *image = NULL;
  SHMaskLayer *layer = NULL;
  SHResourceType maskType = SH_RESOURCE_INVALID;
  SHMaskGLState state;
  GLuint sourceTexture = 0;
  VGint sourceMode = SH_MASK_SOURCE_CONSTANT;
  SHint sourceWidth = 0;
  SHint sourceHeight = 0;
  long long rectX0, rectY0, rectX1, rectY1;
  long long surfX0, surfY0, surfX1, surfY1;
  long long sourceX0 = 0, sourceY0 = 0;
  long long drawWidth, drawHeight;
  VGfloat maskValue = 1.0f;
  VGfloat s0 = 0.0f, t0 = 0.0f, s1 = 0.0f, t1 = 0.0f;
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
    maskType = shGetResourceType(context, mask);
    VG_RETURN_ERR_IF(maskType != SH_RESOURCE_IMAGE &&
                     maskType != SH_RESOURCE_MASK_LAYER,
                     VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

    if (maskType == SH_RESOURCE_IMAGE) {
      image = (SHImage*)mask;
      sourceWidth = image->width;
      sourceHeight = image->height;
      sourceTexture = image->texture;
      sourceMode = shMaskSourceModeForImage(image);
    } else {
      layer = (SHMaskLayer*)mask;
      sourceWidth = layer->width;
      sourceHeight = layer->height;
      sourceTexture = layer->texture;
      sourceMode = SH_MASK_SOURCE_RED;
    }
  } else if (operation == VG_CLEAR_MASK) {
    maskValue = 0.0f;
  }

  if (!shResizeMaskSurface(context, context->surfaceWidth, context->surfaceHeight))
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (context->maskTexture == 0 ||
      context->maskFramebuffer == 0 ||
      context->maskWidth <= 0 ||
      context->maskHeight <= 0)
    VG_RETURN(VG_NO_RETVAL);

  rectX0 = x;
  rectY0 = y;
  rectX1 = (long long)x + (long long)width;
  rectY1 = (long long)y + (long long)height;

  if (image || layer) {
    long long sourceX1 = (long long)x + (long long)sourceWidth;
    long long sourceY1 = (long long)y + (long long)sourceHeight;
    if (rectX1 > sourceX1) rectX1 = sourceX1;
    if (rectY1 > sourceY1) rectY1 = sourceY1;
  }

  surfX0 = rectX0 < 0 ? 0 : rectX0;
  surfY0 = rectY0 < 0 ? 0 : rectY0;
  surfX1 = rectX1 > context->maskWidth ? context->maskWidth : rectX1;
  surfY1 = rectY1 > context->maskHeight ? context->maskHeight : rectY1;

  if (surfX1 <= surfX0 || surfY1 <= surfY0)
    VG_RETURN(VG_NO_RETVAL);

  drawWidth = surfX1 - surfX0;
  drawHeight = surfY1 - surfY0;

  if (image || layer) {
    sourceX0 = surfX0 - x;
    sourceY0 = surfY0 - y;
    s0 = (VGfloat)sourceX0 / (VGfloat)sourceWidth;
    t0 = (VGfloat)sourceY0 / (VGfloat)sourceHeight;
    s1 = (VGfloat)(sourceX0 + drawWidth) / (VGfloat)sourceWidth;
    t1 = (VGfloat)(sourceY0 + drawHeight) / (VGfloat)sourceHeight;
  }

  shSaveMaskGLState(&state);
  shDrawMaskRect(context,
                 context->maskFramebuffer,
                 context->maskWidth, context->maskHeight,
                 (VGint)surfX0, (VGint)surfY0,
                 (VGint)drawWidth, (VGint)drawHeight,
                 sourceTexture, sourceMode, maskValue,
                 s0, t0, s1, t1,
                 operation);
  shRestoreMaskGLState(&state);
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL VGMaskLayer vgCreateMaskLayer(VGint width, VGint height)
{
  SHMaskLayer *layer = NULL;
  long long pixels;
  VG_GETCONTEXT(VG_INVALID_HANDLE);

  pixels = (long long)width * (long long)height;
  VG_RETURN_ERR_IF(width <= 0 || height <= 0 ||
                   width > SH_MAX_IMAGE_WIDTH ||
                   height > SH_MAX_IMAGE_HEIGHT ||
                   pixels > SH_MAX_IMAGE_PIXELS ||
                   pixels > SH_MAX_IMAGE_BYTES,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_INVALID_HANDLE);

  SH_NEWOBJ(SHMaskLayer, layer);
  VG_RETURN_ERR_IF(!layer, VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);

  if (!shConfigureMaskTarget(&layer->texture,
                             &layer->framebuffer,
                             width, height, 1.0f)) {
    SH_DELETEOBJ(SHMaskLayer, layer);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  }

  layer->width = width;
  layer->height = height;

  if (!shMaskLayerArrayPushBack(&context->maskLayers, layer)) {
    SH_DELETEOBJ(SHMaskLayer, layer);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  }

  VG_RETURN((VGMaskLayer)layer);
}

VG_API_CALL void vgDestroyMaskLayer(VGMaskLayer maskLayer)
{
  SHint index;
  VG_GETCONTEXT(VG_NO_RETVAL);

  index = shMaskLayerArrayFind(&context->maskLayers, (SHMaskLayer*)maskLayer);
  VG_RETURN_ERR_IF(index == -1, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  shMaskLayerArrayRemoveAt(&context->maskLayers, index);
  SH_DELETEOBJ(SHMaskLayer, (SHMaskLayer*)maskLayer);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgFillMaskLayer(VGMaskLayer maskLayer,
                                 VGint x, VGint y,
                                 VGint width, VGint height,
                                 VGfloat value)
{
  SHMaskLayer *layer;
  SHMaskGLState state;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidMaskLayer(context, maskLayer),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(x < 0 || y < 0 ||
                   width <= 0 || height <= 0 ||
                   SH_ISNAN(value) || value < 0.0f || value > 1.0f,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  layer = (SHMaskLayer*)maskLayer;
  VG_RETURN_ERR_IF((long long)x + (long long)width > layer->width ||
                   (long long)y + (long long)height > layer->height,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  shSaveMaskGLState(&state);
  shDrawMaskRect(context,
                 layer->framebuffer,
                 layer->width, layer->height,
                 x, y, width, height,
                 0, SH_MASK_SOURCE_CONSTANT, value,
                 0.0f, 0.0f, 0.0f, 0.0f,
                 VG_SET_MASK);
  shRestoreMaskGLState(&state);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgCopyMask(VGMaskLayer maskLayer,
                            VGint dx, VGint dy,
                            VGint sx, VGint sy,
                            VGint width, VGint height)
{
  SHMaskLayer *layer;
  SHMaskGLState state;
  long long dstX0, dstY0, dstX1, dstY1;
  long long srcX0, srcY0, srcX1, srcY1;
  long long copyWidth, copyHeight;
  VGfloat s0, t0, s1, t1;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidMaskLayer(context, maskLayer),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  if (!shResizeMaskSurface(context, context->surfaceWidth, context->surfaceHeight))
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (context->maskTexture == 0 ||
      context->maskFramebuffer == 0 ||
      context->maskWidth <= 0 ||
      context->maskHeight <= 0)
    VG_RETURN(VG_NO_RETVAL);

  layer = (SHMaskLayer*)maskLayer;

  dstX0 = dx;
  dstY0 = dy;
  dstX1 = (long long)dx + (long long)width;
  dstY1 = (long long)dy + (long long)height;
  srcX0 = sx;
  srcY0 = sy;
  srcX1 = (long long)sx + (long long)width;
  srcY1 = (long long)sy + (long long)height;

  if (dstX0 < 0) { srcX0 -= dstX0; dstX0 = 0; }
  if (dstY0 < 0) { srcY0 -= dstY0; dstY0 = 0; }
  if (srcX0 < 0) { dstX0 -= srcX0; srcX0 = 0; }
  if (srcY0 < 0) { dstY0 -= srcY0; srcY0 = 0; }

  if (dstX1 > layer->width) {
    srcX1 -= dstX1 - layer->width;
    dstX1 = layer->width;
  }
  if (dstY1 > layer->height) {
    srcY1 -= dstY1 - layer->height;
    dstY1 = layer->height;
  }
  if (srcX1 > context->maskWidth) {
    dstX1 -= srcX1 - context->maskWidth;
    srcX1 = context->maskWidth;
  }
  if (srcY1 > context->maskHeight) {
    dstY1 -= srcY1 - context->maskHeight;
    srcY1 = context->maskHeight;
  }

  copyWidth = dstX1 - dstX0;
  copyHeight = dstY1 - dstY0;
  if (copyWidth <= 0 || copyHeight <= 0)
    VG_RETURN(VG_NO_RETVAL);

  s0 = (VGfloat)srcX0 / (VGfloat)context->maskWidth;
  t0 = (VGfloat)srcY0 / (VGfloat)context->maskHeight;
  s1 = (VGfloat)(srcX0 + copyWidth) / (VGfloat)context->maskWidth;
  t1 = (VGfloat)(srcY0 + copyHeight) / (VGfloat)context->maskHeight;

  shSaveMaskGLState(&state);
  shDrawMaskRect(context,
                 layer->framebuffer,
                 layer->width, layer->height,
                 (VGint)dstX0, (VGint)dstY0,
                 (VGint)copyWidth, (VGint)copyHeight,
                 context->maskTexture, SH_MASK_SOURCE_RED, 1.0f,
                 s0, t0, s1, t1,
                 VG_SET_MASK);
  shRestoreMaskGLState(&state);

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
  VG_GETCONTEXT(VG_HARDWARE_UNACCELERATED);

  switch (key) {
  case VG_IMAGE_FORMAT_QUERY:
    VG_RETURN_ERR_IF(!shIsValidImageFormat((VGImageFormat)setting),
                     VG_ILLEGAL_ARGUMENT_ERROR,
                     VG_HARDWARE_UNACCELERATED);
    break;

  case VG_PATH_DATATYPE_QUERY:
    VG_RETURN_ERR_IF(setting != VG_PATH_DATATYPE_S_8 &&
                     setting != VG_PATH_DATATYPE_S_16 &&
                     setting != VG_PATH_DATATYPE_S_32 &&
                     setting != VG_PATH_DATATYPE_F,
                     VG_ILLEGAL_ARGUMENT_ERROR,
                     VG_HARDWARE_UNACCELERATED);
    break;

  default:
    VG_RETURN_ERR(VG_ILLEGAL_ARGUMENT_ERROR, VG_HARDWARE_UNACCELERATED);
  }

  VG_RETURN(VG_HARDWARE_UNACCELERATED);
}
