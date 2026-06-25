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

#ifndef __SHCONTEXT_H
#define __SHCONTEXT_H

#include "shDefs.h"
#include "shVectors.h"
#include "shArrays.h"
#include "shPath.h"
#include "shPaint.h"
#include "shImage.h"
#include "shFont.h"
#include "shGLState.h"
#include "shThread.h"

typedef struct
{
  VGHandle handle;
  GLuint texture;
  GLuint framebuffer;
  SHint width;
  SHint height;
} SHMaskLayer;

void SHMaskLayer_ctor(SHMaskLayer *m);
void SHMaskLayer_dtor(SHMaskLayer *m);

#define _ITEM_T SHMaskLayer*
#define _ARRAY_T SHMaskLayerArray
#define _FUNC_T shMaskLayerArray
#define _ARRAY_DECLARE
#include "shArrayBase.h"

/*------------------------------------------------
 * VGContext object
 *------------------------------------------------*/

typedef enum
{
  SH_RESOURCE_INVALID   = 0,
  SH_RESOURCE_PATH      = 1,
  SH_RESOURCE_PAINT     = 2,
  SH_RESOURCE_IMAGE     = 3,
  SH_RESOURCE_FONT      = 4,
  SH_RESOURCE_MASK_LAYER = 5
} SHResourceType;

typedef struct
{
  SHRecursiveMutex mutex;
  SHAtomicInt refCount;
  SHPathArray paths;
  SHPaintArray paints;
  SHImageArray images;
  SHFontArray fonts;
  SHMaskLayerArray maskLayers;
} SHResourceGroup;

typedef struct VGContext
{
  SHRecursiveMutex mutex;

  /* Surface info supplied by the EGL frontend */
  SHint surfaceWidth;
  SHint surfaceHeight;
  SHint surfaceSampleBuffers;
  SHint surfaceSamples;
  
  /* GetString info */
  char vendor[256];
  char renderer[256];
  char version[256];
  char extensions[256];
  
  /* Mode settings */
  VGMatrixMode        matrixMode;
	VGFillRule          fillRule;
	VGImageQuality      imageQuality;
	VGRenderingQuality  renderingQuality;
	VGBlendMode         blendMode;
	VGImageMode         imageMode;
  
	/* Scissor rectangles */
	SHRectArray        scissor;
  VGboolean          scissoring;
  VGboolean          masking;
  SHint              maskWidth;
  SHint              maskHeight;
  GLuint             maskTexture;
  GLuint             maskFramebuffer;
  SHint              renderToMaskWidth;
  SHint              renderToMaskHeight;
  GLuint             renderToMaskTexture;
  GLuint             renderToMaskFramebuffer;
  GLuint             renderToMaskStencil;
  SHint              coverageWidth;
  SHint              coverageHeight;
  GLuint             coverageTexture;
  GLuint             coverageFramebuffer;
  SHint              coverageSupersampleWidth;
  SHint              coverageSupersampleHeight;
  SHint              coverageSupersampleScale;
  GLuint             coverageSupersampleTexture;
  GLuint             coverageSupersampleFramebuffer;
  GLuint             coverageSupersampleStencil;
  
	/* Stroke parameters */
  SHfloat           strokeLineWidth;
  VGCapStyle        strokeCapStyle;
  VGJoinStyle       strokeJoinStyle;
  SHfloat           strokeMiterLimit;
  SHFloatArray      strokeDashPattern;
  SHfloat           strokeDashPhase;
  VGboolean         strokeDashPhaseReset;
  
  /* Edge fill color for vgConvolve and pattern paint */
  SHColor           tileFillColor;
  
  /* Color for vgClear */
  SHColor           clearColor;
  
  /* Color components layout inside pixel */
  VGPixelLayout     pixelLayout;
  
  /* Source format for image filters */
  VGboolean         filterFormatLinear;
  VGboolean         filterFormatPremultiplied;
  VGbitfield        filterChannelMask;

  /* Color transform */
  VGboolean         colorTransform;
  SHfloat           colorTransformValues[8];
  
  /* Matrices */
  SHMatrix3x3       pathTransform;
  SHMatrix3x3       imageTransform;
  SHMatrix3x3       fillTransform;
  SHMatrix3x3       strokeTransform;
  SHMatrix3x3       glyphTransform;
  SHVector2         glyphOrigin;
  
  /* Paints */
  SHPaint*          fillPaint;
  SHPaint*          strokePaint;
  SHPaint           defaultPaint;
  
  VGErrorCode       error;
  SHImage*          renderTargetImage;
  GLuint            blendTexture;
  SHint             blendTextureWidth;
  SHint             blendTextureHeight;
  
  /* Shared resources */
  SHResourceGroup  *resources;

  /* Pointers to extensions */
  
  /* GL locations */
  struct {
      GLint pos            ;
      GLint textureUV      ;
      GLint model          ;
      GLint projection     ;
      GLint paintInverted  ;
      GLint drawMode       ;
      GLint imageSampler   ;
      GLint imagePremultiplied;
      GLint imageMode      ;
      GLint paintType      ;
      GLint rampSampler    ;
      GLint patternSampler ;
      GLint patternTilingMode;
      GLint userSampler    ;
      GLint paintParams    ;
      GLint paintColor     ;
      GLint scaleFactorBias;
      GLint maskEnabled    ;
      GLint maskSampler    ;
      GLint maskSurfaceSize;
      GLint blendMode      ;
      GLint blendSampler   ;
      GLint blendSurfaceSize;
      GLint coverageEnabled;
      GLint coverageSampler;
      GLint coverageSurfaceSize;
      GLint coveragePass;
  } locationDraw;

  struct {
      GLint pos;
      GLint targetSize;
      GLint sourceSampler;
      GLint scale;
  } locationCoverage;

  struct {
      GLint pos;
      GLint texCoord;
      GLint targetSize;
      GLint sourceSampler;
      GLint sourceMode;
      GLint maskValue;
  } locationMask;

  struct {
      GLint pos;
      GLint startColor;
      GLint endColor;
      GLint startPixel;
      GLint pixelSpan;
  } locationColorRamp;

  struct {
      GLint pos;
      GLint mode;
      GLint sourceSampler;
      GLint auxSampler;
      GLint targetSize;
      GLint sourceSize;
      GLint sourceOrigin;
      GLint targetOrigin;
      GLint sourcePremultiplied;
      GLint kernelSize;
      GLint shift;
      GLint scale;
      GLint bias;
      GLint tilingMode;
      GLint colorMatrix;
      GLint colorBias;
      GLint tileFillColor;
      GLint sourceLinear;
      GLint filterLinear;
      GLint outputLinear;
      GLint dstLinear;
      GLint premultiplyInput;
      GLint unpremultiplyOutput;
      GLint dstStorageMode;
      GLint lookupSourceChannel;
      GLint blurSize;
      GLint blurOrigin;
      GLint blurTextureSize;
      GLint parametricOffset;
      GLint parametricStrength;
      GLint parametricFlags;
      GLint highlightPaintMode;
      GLint shadowPaintMode;
      GLint highlightColor;
      GLint shadowColor;
      GLint highlightSampler;
      GLint shadowSampler;
  } locationImageFilter;

  /* GL programs */
  GLuint progDraw;
  GLuint progColorRamp;
  GLuint progMask;
  GLuint progCoverage;
  GLuint progImageFilter;
  GLuint filterFramebuffer;
  GLuint filterScratchTexture;
  SHint filterScratchWidth;
  SHint filterScratchHeight;
  GLuint arrayObject;
  GLuint arrayBuffer;

  /* GL shaders */
  const void* userShaderVertex;
  const void* userShaderFragment;
  GLint vs;
  GLint fs;
  GLint maskVs;
  GLint maskFs;
  VGboolean glInitialized;

} VGContext;

typedef SHGLVertexBindingState SHVertexState;

typedef struct
{
  VGContext *context;
  SHResourceGroup *resources;
  VGboolean contextLocked;
  VGboolean resourcesLocked;
} SHContextLock;

void VGContext_ctor(VGContext *c);
void VGContext_dtor(VGContext *c);
void shSetError(VGContext *c, VGErrorCode e);
VGContext* shAcquireCurrentContext(SHContextLock *lock);
VGContext* shAcquireCurrentContextOnly(SHContextLock *lock);
void shContextLockCleanup(SHContextLock *lock);
void shLockContext(VGContext *c);
void shUnlockContext(VGContext *c);
void shLockResourceGroup(SHResourceGroup *resources);
void shUnlockResourceGroup(SHResourceGroup *resources);
SHint shIsValidPath(VGContext *c, VGHandle h);
SHint shIsValidPaint(VGContext *c, VGHandle h);
SHint shIsValidImage(VGContext *c, VGHandle h);
SHint shIsValidFont(VGContext *c, VGHandle h);
SHint shIsValidMaskLayer(VGContext *c, VGHandle h);
SHResourceType shGetResourceType(VGContext *c, VGHandle h);
VGHandle shRegisterResource(VGContext *c, SHResourceType type, void *object);
void shUnregisterResource(VGContext *c, VGHandle h,
                          SHResourceType type, void *object);
void* shGetResource(VGContext *c, VGHandle h, SHResourceType type);
SHPath* shGetPath(VGContext *c, VGPath path);
VGboolean shAcquirePath(VGContext *c, VGPath path, SHPathAccess *access);
VGboolean shAcquirePaths(VGContext *c,
                         const VGPath *paths,
                         SHPathAccess *accesses,
                         SHint count);
SHPaint* shGetPaint(VGContext *c, VGPaint paint);
VGboolean shAcquirePaint(VGContext *c,
                         VGPaint paint,
                         SHPaintAccess *access);
SHImage* shGetImage(VGContext *c, VGImage image);
VGboolean shAcquireImages(VGContext *c,
                          const VGImage *images,
                          SHImageAccess *accesses,
                          SHint count,
                          SHImageLockSet *locks);
VGboolean shAcquireImage(VGContext *c,
                         VGImage image,
                         SHImageAccess *access,
                         SHImageLockSet *locks);
SHFont* shGetFont(VGContext *c, VGFont font);
VGboolean shAcquireFont(VGContext *c,
                        VGFont font,
                        SHFontAccess *access);
SHMaskLayer* shGetMaskLayer(VGContext *c, VGMaskLayer maskLayer);
VGboolean shIsLiveImage(VGContext *c, const SHImage *image);
VGContext* shGetContext();
VGContext* shCreateContext(void);
VGContext* shCreateContextShared(VGContext *shareContext);
void shDestroyContext(VGContext *c);
VGboolean shSetCurrentContext(VGContext *c, VGint width, VGint height);
VGboolean shSetCurrentContextForImage(VGContext *c,
                                      VGint width,
                                      VGint height,
                                      SHImage *image);
void shClearCurrentContext(void);
void shResizeCurrentSurface(VGint width, VGint height);
void shMarkRenderTargetDirty(VGContext *c);
VGboolean shCanDeleteResourceGL(void);
void shBindContextVertexState(VGContext *c, SHVertexState *state);
void shRestoreVertexState(const SHVertexState *state);
void shApplyColorTransform(VGContext *context);
void shEnsureMaskTexture(VGContext *c);
VGboolean shApplyMaskTextureToSurface(VGContext *c,
                                      GLuint texture,
                                      VGMaskOperation operation);
VGboolean shApplyMaskValueToSurface(VGContext *c,
                                    VGfloat value,
                                    VGMaskOperation operation);

#define VG_NO_RETVAL

#define VG_GETCONTEXT(RETVAL) \
  SHContextLock shContextLock; \
  VGContext *context = shAcquireCurrentContext(&shContextLock); \
  if (!context) return RETVAL;

/* Use only for entry points that do not touch shared resources. */
#define VG_GETCONTEXT_CONTEXT_ONLY(RETVAL) \
  SHContextLock shContextLock; \
  VGContext *context = shAcquireCurrentContextOnly(&shContextLock); \
  if (!context) return RETVAL;
  
#define VG_RETURN(RETVAL) \
  { shContextLockCleanup(&shContextLock); return RETVAL; }

#define VG_RETURN_ERR(ERRORCODE, RETVAL) \
  { shSetError(context,ERRORCODE); shContextLockCleanup(&shContextLock); return RETVAL; }

#define VG_RETURN_ERR_IF(COND, ERRORCODE, RETVAL) \
  { if (COND) {shSetError(context,ERRORCODE); shContextLockCleanup(&shContextLock); return RETVAL;} }

/*-----------------------------------------------------------
 * Same macros but no mutex handling - used by sub-functions
 *-----------------------------------------------------------*/

#define SH_NO_RETVAL

#define SH_GETCONTEXT(RETVAL) \
  VGContext *context = shGetContext(); \
  if (!context) return RETVAL;
  
#define SH_RETURN(RETVAL) \
  { return RETVAL; }

#define SH_RETURN_ERR(ERRORCODE, RETVAL) \
  { shSetError(context,ERRORCODE); return RETVAL; }

#define SH_RETURN_ERR_IF(COND, ERRORCODE, RETVAL) \
  { if (COND) {shSetError(context,ERRORCODE); return RETVAL;} }


#endif /* __SHCONTEXT_H */
