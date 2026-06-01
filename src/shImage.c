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
#include "shImage.h"
#include "shContext.h"
#include <string.h>
#include <stdio.h>

#define _ITEM_T SHColor
#define _ARRAY_T SHColorArray
#define _FUNC_T shColorArray
#define _ARRAY_DEFINE
#define _COMPARE_T(c1,c2) 0
#include "shArrayBase.h"

#define _ITEM_T SHImage*
#define _ARRAY_T SHImageArray
#define _FUNC_T shImageArray
#define _ARRAY_DEFINE
#include "shArrayBase.h"


/*-----------------------------------------------------------
 * Prepares the proper pixel pack/unpack info for the given
 * OpenVG image format.
 *-----------------------------------------------------------*/

void shSetupImageFormat(VGImageFormat vg, SHImageFormatDesc *f)
{
  SHuint8 abits = 0;
  SHuint8 tshift = 0;
  SHuint8 tmask = 0;
  SHuint32 amsbBit = 0;
  SHuint32 bgrBit = 0;

  /* Store VG format name */
  f->vgformat = vg;

  /* Check if alpha on MSB or colors in BGR order */
  amsbBit = ((vg & (1 << 6)) >> 6);
  bgrBit = ((vg & (1 << 7)) >> 7);

  /* Find component ordering and size */
  switch(vg & 0x1F)
  {
  case 0: /* VG_sRGBX_8888 */
  case 7: /* VG_lRGBX_8888 */
    f->bytes = 4;
    f->rmask = 0xFF000000;
    f->rshift = 24;
    f->rmax = 255;
    f->gmask = 0x00FF0000;
    f->gshift = 16;
    f->gmax = 255;
    f->bmask = 0x0000FF00;
    f->bshift = 8;
    f->bmax = 255;
    f->amask = 0x0;
    f->ashift = 0;
    f->amax = 1;
    break;
  case 1: /* VG_sRGBA_8888 */
  case 2: /* VG_sRGBA_8888_PRE */
  case 8: /* VG_lRGBA_8888 */
  case 9: /* VG_lRGBA_8888_PRE */
    f->bytes = 4;
    f->rmask = 0xFF000000;
    f->rshift = 24;
    f->rmax = 255;
    f->gmask = 0x00FF0000;
    f->gshift = 16;
    f->gmax = 255;
    f->bmask = 0x0000FF00;
    f->bshift = 8;
    f->bmax = 255;
    f->amask = 0x000000FF;
    f->ashift = 0;
    f->amax = 255;
    break;
  case 3: /* VG_sRGB_565 */
    f->bytes = 2;
    f->rmask = 0xF800;
    f->rshift = 11;
    f->rmax = 31;
    f->gmask = 0x07E0;
    f->gshift = 5;
    f->gmax = 63;
    f->bmask = 0x001F;
    f->bshift = 0;
    f->bmax = 31;
    f->amask = 0x0;
    f->ashift = 0;
    f->amax = 1;
    break;
  case 4: /* VG_sRGBA_5551 */
    f->bytes = 2;
    f->rmask = 0xF800;
    f->rshift = 11;
    f->rmax = 31;
    f->gmask = 0x07C0;
    f->gshift = 6;
    f->gmax = 31;
    f->bmask = 0x003E;
    f->bshift = 1;
    f->bmax = 31;
    f->amask = 0x0001;
    f->ashift = 0;
    f->amax = 1;
    break;
  case 5: /* VG_sRGBA_4444 */
    f->bytes = 2;
    f->rmask = 0xF000;
    f->rshift = 12;
    f->rmax = 15;
    f->gmask = 0x0F00;
    f->gshift = 8;
    f->gmax = 15;
    f->bmask = 0x00F0;
    f->bshift = 4;
    f->bmax = 15;
    f->amask = 0x000F;
    f->ashift = 0;
    f->amax = 15;
    break;
  case 6: /* VG_sL_8 */
  case 10: /* VG_lL_8 */
    f->bytes = 1;
    f->rmask = 0xFF;
    f->rshift = 0;
    f->rmax = 255;
    f->gmask = 0xFF;
    f->gshift = 0;
    f->gmax = 255;
    f->bmask = 0xFF;
    f->bshift = 0;
    f->bmax = 255;
    f->amask = 0x0;
    f->ashift = 0;
    f->amax = 1;
    break;
  case 11: /* VG_A_8 */
    f->bytes = 1;
    f->rmask = 0x0;
    f->rshift = 0;
    f->rmax = 1;
    f->gmask = 0x0;
    f->gshift = 0;
    f->gmax = 1;
    f->bmask = 0x0;
    f->bshift = 0;
    f->bmax = 1;
    f->amask = 0xFF;
    f->ashift = 0;
    f->amax = 255;
    break;
  case 12: /* VG_BW_1 */
    f->bytes = 1;
    f->rmask = 0x0;
    f->rshift = 0;
    f->rmax = 1;
    f->gmask = 0x0;
    f->gshift = 0;
    f->gmax = 1;
    f->bmask = 0x0;
    f->bshift = 0;
    f->bmax = 1;
    f->amask = 0x0;
    f->ashift = 0;
    f->amax = 1;
    break;
  }

  /* Check for A,X at MSB */
  if (amsbBit) {

    abits = f->bshift;

    f->rshift -= abits;
    f->gshift -= abits;
    f->bshift -= abits;
    f->ashift = f->bytes * 8 - abits;

    f->rmask >>= abits;
    f->gmask >>= abits;
    f->bmask >>= abits;
    f->amask <<= f->bytes * 8 - abits;
  }

  /* Check for BGR ordering */
  if (bgrBit) {

    tshift = f->bshift;
    f->bshift = f->rshift;
    f->rshift = tshift;

    tmask = f->bmask;
    f->bmask = f->rmask;
    f->rmask = tmask;
  }

  /* Find proper mapping to OpenGL formats */
  switch(vg & 0x1F)
  {
  case VG_sRGBX_8888:
  case VG_lRGBX_8888:
  case VG_sRGBA_8888:
  case VG_sRGBA_8888_PRE:
  case VG_lRGBA_8888:
  case VG_lRGBA_8888_PRE:

    f->glintformat = GL_RGBA;

    if (amsbBit == 0 && bgrBit == 0) {
      f->glformat = GL_RGBA;
      f->gltype = GL_UNSIGNED_INT_8_8_8_8;

    }else if (amsbBit == 1 && bgrBit == 0) {
      f->glformat = GL_BGRA;
      f->gltype = GL_UNSIGNED_INT_8_8_8_8_REV;

    }else if (amsbBit == 0 && bgrBit == 1) {
      f->glformat = GL_BGRA;
      f->gltype = GL_UNSIGNED_INT_8_8_8_8;

    }else if (amsbBit == 1 && bgrBit == 1) {
      f->glformat = GL_RGBA;
      f->gltype = GL_UNSIGNED_INT_8_8_8_8_REV;
    }

    break;
  case VG_sRGBA_5551:

    f->glintformat = GL_RGBA;

    if (amsbBit == 0 && bgrBit == 0) {
      f->glformat = GL_RGBA;
      f->gltype = GL_UNSIGNED_SHORT_5_5_5_1;

    }else if (amsbBit == 1 && bgrBit == 0) {
      f->glformat = GL_BGRA;
      f->gltype = GL_UNSIGNED_SHORT_1_5_5_5_REV;

    }else if (amsbBit == 0 && bgrBit == 1) {
      f->glformat = GL_BGRA;
      f->gltype = GL_UNSIGNED_SHORT_5_5_5_1;

    }else if (amsbBit == 1 && bgrBit == 1) {
      f->glformat = GL_RGBA;
      f->gltype = GL_UNSIGNED_SHORT_1_5_5_5_REV;
    }

    break;
  case VG_sRGBA_4444:

    f->glintformat = GL_RGBA;

    if (amsbBit == 0 && bgrBit == 0) {
      f->glformat = GL_RGBA;
      f->gltype = GL_UNSIGNED_SHORT_4_4_4_4;

    }else if (amsbBit == 1 && bgrBit == 0) {
      f->glformat = GL_BGRA;
      f->gltype = GL_UNSIGNED_SHORT_4_4_4_4_REV;

    }else if (amsbBit == 0 && bgrBit == 1) {
      f->glformat = GL_BGRA;
      f->gltype = GL_UNSIGNED_SHORT_4_4_4_4;

    }else if (amsbBit == 1 && bgrBit == 1) {
      f->glformat = GL_RGBA;
      f->gltype = GL_UNSIGNED_SHORT_4_4_4_4_REV;
    }

    break;
  case VG_sRGB_565:

    f->glintformat = GL_RGB;

    if (bgrBit == 0) {
      f->glformat = GL_RGB;
      f->gltype = GL_UNSIGNED_SHORT_5_6_5;

    }else if (bgrBit == 1) {
      f->glformat = GL_RGB;
      f->gltype = GL_UNSIGNED_SHORT_5_6_5;
    }

    break;
  case VG_sL_8:
  case VG_lL_8:
    f->glintformat = GL_R8;
    f->glformat = GL_RED;
    f->gltype = GL_UNSIGNED_BYTE;

    break;
  case VG_A_8:

    f->glintformat = GL_R8;
    f->glformat = GL_RED;
    f->gltype = GL_UNSIGNED_BYTE;

    break;
  case VG_BW_1:

    f->glintformat = 0;
    f->glformat = 0;
    f->gltype = 0;
    break;
  }
}

/*-----------------------------------------------------
 * Returns 1 if the given format is valid according to
 * the OpenVG specification, else 0.
 *-----------------------------------------------------*/

int shIsValidImageFormat(VGImageFormat format)
{
  SHint aOrderBit = (1 << 6);
  SHint rgbOrderBit = (1 << 7);
  SHint baseFormat = format & 0x1F;
  SHint unorderedRgba = format & (~(aOrderBit | rgbOrderBit));
  SHint isRgba = (baseFormat == VG_sRGBX_8888     ||
                  baseFormat == VG_sRGBA_8888     ||
                  baseFormat == VG_sRGBA_8888_PRE ||
                  baseFormat == VG_sRGBA_5551     ||
                  baseFormat == VG_sRGBA_4444     ||
                  baseFormat == VG_lRGBX_8888     ||
                  baseFormat == VG_lRGBA_8888     ||
                  baseFormat == VG_lRGBA_8888_PRE);
  
  SHint check = isRgba ? unorderedRgba : format;
  return check >= VG_sRGBX_8888 && check <= VG_BW_1;
}

/*-----------------------------------------------------
 * Returns 1 if the given format is supported by this
 * implementation
 *-----------------------------------------------------*/

int shIsSupportedImageFormat(VGImageFormat format)
{
  SHuint32 baseFormat = (format & 0x1F);
  if (baseFormat == VG_sRGBA_8888_PRE ||
      baseFormat == VG_lRGBA_8888_PRE ||
      baseFormat == VG_BW_1)
      return 0;

  return 1;
}

static void shApplyImageTextureSwizzle(VGImageFormat format)
{
  SHuint32 baseFormat = format & 0x1F;

  if (baseFormat == VG_A_8) {
    GLint swizzle[4] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
  } else if (baseFormat == VG_sL_8 || baseFormat == VG_lL_8) {
    GLint swizzle[4] = {GL_RED, GL_RED, GL_RED, GL_ONE};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
  } else if (baseFormat == VG_sRGBX_8888 ||
             baseFormat == VG_lRGBX_8888) {
    GLint swizzle[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ONE};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
  } else {
    GLint swizzle[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
  }
}

/*--------------------------------------------------------
 * Packs the pixel color components into memory at given
 * address according to given format
 *--------------------------------------------------------*/

void shStoreColor(SHColor *c, void *data, SHImageFormatDesc *f)
{
  /*
  TODO: unsupported formats:
  - s and l both behave linearly
  - 1-bit black & white (BW_1)
  */

  SHfloat l = 0.0f;
  SHuint32 out = 0x0;

  if (f->vgformat == VG_lL_8 || f->vgformat == VG_sL_8) {

    /* Grayscale (luminosity) conversion as defined by the spec */
    l = 0.2126f * c->r + 0.7152f * c->g + 0.0722f * c->b;
    out = (SHuint32)(l * (SHfloat)f->rmax + 0.5f);

  }else{

    /* Pack color components */
    out += ( ((SHuint32)(c->r * (SHfloat)f->rmax + 0.5f)) << f->rshift ) & f->rmask;
    out += ( ((SHuint32)(c->g * (SHfloat)f->gmax + 0.5f)) << f->gshift ) & f->gmask;
    out += ( ((SHuint32)(c->b * (SHfloat)f->bmax + 0.5f)) << f->bshift ) & f->bmask;
    out += ( ((SHuint32)(c->a * (SHfloat)f->amax + 0.5f)) << f->ashift ) & f->amask;
  }
  
  /* Store to buffer */
  switch (f->bytes) {
  case 4: *((SHuint32*)data) = (SHuint32)(out & 0xFFFFFFFF); break;
  case 2: *((SHuint16*)data) = (SHuint16)(out & 0x0000FFFF); break;
  case 1: *((SHuint8*)data)  = (SHuint8) (out & 0x000000FF); break;
  }
}

/*---------------------------------------------------------
 * Unpacks the pixel color components from memory at given
 * address according to the given format
 *---------------------------------------------------------*/

void shLoadColor(SHColor *c, const void *data, SHImageFormatDesc *f)
{
  /*
  TODO: unsupported formats:
  - s and l both behave linearly
  - 1-bit black & white (BW_1)
  */

  SHuint32 in = 0x0;
  
  /* Load from buffer */
  switch (f->bytes) {
  case 4: in = (SHuint32) *((SHuint32*)data); break;
  case 2: in = (SHuint32) *((SHuint16*)data); break;
  case 1: in = (SHuint32) *((SHuint8*)data); break;
  }

  /* Unpack color components */
  c->r = (SHfloat)((in & f->rmask) >> f->rshift) / (SHfloat) f->rmax;
  c->g = (SHfloat)((in & f->gmask) >> f->gshift) / (SHfloat) f->gmax;
  c->b = (SHfloat)((in & f->bmask) >> f->bshift) / (SHfloat) f->bmax;
  c->a = (SHfloat)((in & f->amask) >> f->ashift) / (SHfloat) f->amax;
  
  /* Initialize unused components to 1 */
  if (f->amask == 0x0) { c->a = 1.0f; }
  if (f->rmask == 0x0) { c->r = 1.0f; c->g = 1.0f; c->b = 1.0f; }
}


/*----------------------------------------------
 * Color and Image constructors and destructors
 *----------------------------------------------*/

void SHColor_ctor(SHColor *c)
{
  c->r = 0.0f;
  c->g = 0.0f;
  c->b = 0.0f;
  c->a = 0.0f;
}

void SHColor_dtor(SHColor *c) {
}

void SHImage_ctor(SHImage *i)
{
  i->data = NULL;
  i->width = 0;
  i->height = 0;
  i->texture = 0;
  i->refCount = 1;
  i->eglPbufferRefs = 0;
  i->renderTargetRefs = 0;
  i->paintPatternRefs = 0;
  i->glyphRefs = 0;
  i->gpuDataDirty = VG_FALSE;
}

void SHImage_dtor(SHImage *i)
{
  if (i->data != NULL)
    free(i->data);
  
  if (shCanDeleteResourceGL() &&
      i->texture != 0 &&
      glIsTexture(i->texture))
    glDeleteTextures(1, &i->texture);
}

void shImageAddRef(SHImage *i)
{
  if (i)
    ++i->refCount;
}

void shImageRelease(SHImage *i)
{
  if (!i)
    return;

  --i->refCount;
  if (i->refCount <= 0)
    SH_DELETEOBJ(SHImage, i);
}

void shImageAddEGLPbufferRef(SHImage *i)
{
  if (!i)
    return;

  ++i->eglPbufferRefs;
  shImageAddRef(i);
}

void shImageReleaseEGLPbufferRef(SHImage *i)
{
  if (!i)
    return;

  if (i->eglPbufferRefs > 0)
    --i->eglPbufferRefs;
  shImageRelease(i);
}

void shImageAddPaintPatternRef(SHImage *i)
{
  if (i)
    ++i->paintPatternRefs;
}

void shImageReleasePaintPatternRef(SHImage *i)
{
  if (i && i->paintPatternRefs > 0)
    --i->paintPatternRefs;
}

void shImageAddGlyphRef(SHImage *i)
{
  if (i)
    ++i->glyphRefs;
}

void shImageReleaseGlyphRef(SHImage *i)
{
  if (i && i->glyphRefs > 0)
    --i->glyphRefs;
}

void shImageBeginRenderTarget(SHImage *i)
{
  if (i)
    ++i->renderTargetRefs;
}

void shImageEndRenderTarget(SHImage *i)
{
  if (i && i->renderTargetRefs > 0)
    --i->renderTargetRefs;
}

VGboolean shImageIsEGLPbufferBound(SHImage *i)
{
  return (i && i->eglPbufferRefs > 0) ? VG_TRUE : VG_FALSE;
}

VGboolean shImageIsRenderTarget(SHImage *i)
{
  return (i && i->renderTargetRefs > 0) ? VG_TRUE : VG_FALSE;
}

VGboolean shImageIsRenderTargetEligible(SHImage *i)
{
  return (i &&
          i->paintPatternRefs == 0 &&
          i->glyphRefs == 0) ? VG_TRUE : VG_FALSE;
}

void shImageMarkGpuDataDirty(SHImage *i)
{
  if (i)
    i->gpuDataDirty = VG_TRUE;
}

/*--------------------------------------------------------
 * Finds appropriate OpenGL texture size for the size of
 * the given image
 *--------------------------------------------------------*/

void shUpdateImageTextureSize(SHImage *i)
{
  i->texwidth = i->width;
  i->texheight = i->height;
  i->texwidthK = 1.0f;
  i->texheightK = 1.0f;
  
  /* Round size to nearest power of 2 */
  /* TODO: might be dropped out if it works without  */
  
  /*i->texwidth = 1;
  while (i->texwidth < i->width)
    i->texwidth *= 2;
  
  i->texheight = 1;
  while (i->texheight < i->height)
    i->texheight *= 2;
  
  i->texwidthK  = (SHfloat)i->width  / i->texwidth;
  i->texheightK = (SHfloat)i->height / i->texheight; */
}

/*--------------------------------------------------
 * Downloads the image data from OpenVG into 
 * an OpenGL texture
 *--------------------------------------------------*/

void shUpdateImageTexture(SHImage *i, VGContext *c)
{
  GLint activeTexture;
  GLint previousTexture;
  GLint unpackAlignment;
  GLint unpackRowLength;
  GLint unpackSkipPixels;
  GLint unpackSkipRows;

  /* Store pixels to texture */
  if (i->texture == 0)
    glGenTextures(1, &i->texture);

  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRowLength);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glBindTexture(GL_TEXTURE_2D, i->texture);
  glTexImage2D(GL_TEXTURE_2D, 0, i->fd.glintformat,
               i->texwidth, i->texheight, 0,
               i->fd.glformat, i->fd.gltype, i->data);
  shApplyImageTextureSwizzle(i->fd.vgformat);

  glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRowLength);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
  glBindTexture(GL_TEXTURE_2D, previousTexture);
  glActiveTexture(activeTexture);

  i->gpuDataDirty = VG_FALSE;
}

static VGboolean shCheckedSizeMul(size_t a, size_t b, size_t *out)
{
  if (b != 0 && a > ((size_t)-1) / b)
    return VG_FALSE;

  if (out)
    *out = a * b;
  return VG_TRUE;
}

static VGboolean shImageDataSize(SHint width,
                                 SHint height,
                                 SHint bytesPerPixel,
                                 size_t *pixelCountOut,
                                 size_t *byteCountOut)
{
  size_t pixelCount;
  size_t byteCount;

  if (width <= 0 || height <= 0 || bytesPerPixel <= 0)
    return VG_FALSE;

  if (!shCheckedSizeMul((size_t)width, (size_t)height, &pixelCount) ||
      pixelCount > (size_t)SH_MAX_IMAGE_PIXELS)
    return VG_FALSE;

  if (!shCheckedSizeMul(pixelCount, (size_t)bytesPerPixel, &byteCount) ||
      byteCount > (size_t)SH_MAX_IMAGE_BYTES)
    return VG_FALSE;

  if (pixelCountOut)
    *pixelCountOut = pixelCount;
  if (byteCountOut)
    *byteCountOut = byteCount;
  return VG_TRUE;
}

static VGboolean shPixelBufferSize(SHint width,
                                   SHint height,
                                   SHint bytesPerPixel,
                                   size_t *byteCountOut)
{
  size_t pixelCount;
  size_t byteCount;

  if (width <= 0 || height <= 0 || bytesPerPixel <= 0)
    return VG_FALSE;

  if (!shCheckedSizeMul((size_t)width, (size_t)height, &pixelCount) ||
      !shCheckedSizeMul(pixelCount, (size_t)bytesPerPixel, &byteCount))
    return VG_FALSE;

  if (byteCount > (size_t)SH_MAX_INT)
    return VG_FALSE;

  if (byteCountOut)
    *byteCountOut = byteCount;
  return VG_TRUE;
}

/*----------------------------------------------------------
 * Creates a new image object and returns the handle to it
 *----------------------------------------------------------*/

VG_API_CALL VGImage vgCreateImage(VGImageFormat format,
                                  VGint width, VGint height,
                                  VGbitfield allowedQuality)
{
  SHImage *i = NULL;
  SHImageFormatDesc fd;
  size_t dataSize = 0;
  VG_GETCONTEXT(VG_INVALID_HANDLE);
  
  /* Reject invalid formats */
  VG_RETURN_ERR_IF(!shIsValidImageFormat(format),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_INVALID_HANDLE);
  
  /* Reject unsupported formats */
  VG_RETURN_ERR_IF(!shIsSupportedImageFormat(format),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_INVALID_HANDLE);
  
  /* Reject invalid sizes */
  shSetupImageFormat(format, &fd);
  VG_RETURN_ERR_IF(width <= 0 || width > SH_MAX_IMAGE_WIDTH ||
                   height <= 0 || height > SH_MAX_IMAGE_HEIGHT ||
                   !shImageDataSize(width, height, fd.bytes,
                                    NULL, &dataSize),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_INVALID_HANDLE);
  
  /* Reject invalid quality bits */
  VG_RETURN_ERR_IF(allowedQuality &
                   ~(VG_IMAGE_QUALITY_NONANTIALIASED |
                     VG_IMAGE_QUALITY_FASTER | VG_IMAGE_QUALITY_BETTER),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_INVALID_HANDLE);
  
  /* Create new image object */
  SH_NEWOBJ(SHImage, i);
  VG_RETURN_ERR_IF(!i, VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  i->width = width;
  i->height = height;
  i->fd = fd;
  
  /* Allocate data memory */
  shUpdateImageTextureSize(i);
  if (!shImageDataSize(i->texwidth, i->texheight, fd.bytes,
                       NULL, &dataSize)) {
    SH_DELETEOBJ(SHImage, i);
    VG_RETURN_ERR(VG_ILLEGAL_ARGUMENT_ERROR, VG_INVALID_HANDLE);
  }
  i->data = (SHuint8*)malloc(dataSize);
  
  if (i->data == NULL) {
    SH_DELETEOBJ(SHImage, i);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE); }
  
  /* Initialize data by zeroing-out */
  memset(i->data, 0, dataSize);
  shUpdateImageTexture(i, context);
  
  /* Add to resource list */
  if (!shImageArrayPushBack(&context->resources->images, i)) {
    SH_DELETEOBJ(SHImage, i);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  }
  
  VG_RETURN((VGImage)i);
}

VG_API_CALL void vgDestroyImage(VGImage image)
{
  SHint index;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  /* Check if valid resource */
  index = shImageArrayFind(&context->resources->images, (SHImage*)image);
  VG_RETURN_ERR_IF(index == -1, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  
  /* Remove the public handle; retained font glyphs may keep the object alive. */
  shImageArrayRemoveAt(&context->resources->images, index);
  shImageRelease((SHImage*)image);
  
  VG_RETURN(VG_NO_RETVAL);
}

static VGboolean shTryDirectClearImage(SHImage *image,
                                       const SHColor *clear,
                                       VGint x,
                                       VGint y,
                                       VGint width,
                                       VGint height);
static VGboolean shTryTransferCopyImage(VGContext *context,
                                        SHImage *dst,
                                        VGint dx,
                                        VGint dy,
                                        SHImage *src,
                                        VGint sx,
                                        VGint sy,
                                        VGint width,
                                        VGint height);
static VGboolean shTryTransferGetPixels(VGContext *context,
                                        SHImage *image,
                                        VGint dx,
                                        VGint dy,
                                        VGint sx,
                                        VGint sy,
                                        VGint width,
                                        VGint height);

/*---------------------------------------------------
 * Clear given rectangle area in the image data with
 * color set via vgSetfv(VG_CLEAR_COLOR, ...)
 *---------------------------------------------------*/

VG_API_CALL void vgClearImage(VGImage image,
                              VGint x, VGint y, VGint width, VGint height)
{
  SHImage *i;
  SHColor clear;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!shIsValidImage(context, image),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  i = (SHImage*)image;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(i),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  clear = context->clearColor;
  VG_RETURN_ERR_IF(!shTryDirectClearImage(i, &clear, x, y, width, height),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

/*------------------------------------------------------------
 * Generic function for copying a rectangle area of pixels
 * of size (width,height) among two data buffers. The size of
 * source (swidth,sheight) and destination (dwidth,dheight)
 * images may vary as well as the source coordinates (sx,sy)
 * and destination coordinates(dx, dy).
 *------------------------------------------------------------*/

void shCopyPixels(SHuint8 *dst, VGImageFormat dstFormat, SHint dstStride,
                  const SHuint8 *src, VGImageFormat srcFormat, SHint srcStride,
                  SHint dwidth, SHint dheight, SHint swidth, SHint sheight,
                  SHint dx, SHint dy, SHint sx, SHint sy,
                  SHint width, SHint height)
{
  SHint dxold, dyold;
  SHint SX, SY, DX, DY;
  const SHuint8 *SD;
  SHuint8 *DD;
  SHColor c;

  SHImageFormatDesc dfd;
  SHImageFormatDesc sfd;

  /* Setup image format descriptors */
  SH_ASSERT(shIsSupportedImageFormat(dstFormat));
  SH_ASSERT(shIsSupportedImageFormat(srcFormat));
  shSetupImageFormat(dstFormat, &dfd);
  shSetupImageFormat(srcFormat, &sfd);

  /*
    In order to optimize the copying loop and remove the
    if statements from it to check whether target pixel
    is in the source and destination surface, we clamp
    copy rectangle in advance. This is quite a tedious
    task though. Here is a picture of the scene. Note that
    (dx,dy) is actually an offset of the copy rectangle
    (clamped to src surface) from the (0,0) point on dst
    surface. A negative (dx,dy) (as in this picture) also
    affects src coords of the copy rectangle which have
    to be readjusted again (sx,sy,width,height).

                          src
    *----------------------*
    | (sx,sy)  copy rect   |
    | *-----------*        |
    | |\(dx, dy)  |        |          dst
    | | *------------------------------*
    | | |xxxxxxxxx|        |           |
    | | |xxxxxxxxx|        |           |
    | *-----------*        |           |
    |   |   (width,height) |           |
    *----------------------*           |
        |           (swidth,sheight)   |
        *------------------------------*
                                (dwidth,dheight)
  */

  /* Cancel if copy rect out of src bounds */
  if (sx >= swidth || sy >= sheight) return;
  if (sx + width < 0 || sy + height < 0) return;
  
  /* Clamp copy rectangle to src bounds */
  sx = SH_MAX(sx, 0);
  sy = SH_MAX(sy, 0);
  width = SH_MIN(width, swidth - sx);
  height = SH_MIN(height, sheight - sy);
  
  /* Cancel if copy rect out of dst bounds */
  if (dx >= dwidth || dy >= dheight) return;
  if (dx + width < 0 || dy + height < 0) return;
  
  /* Clamp copy rectangle to dst bounds */
  dxold = dx; dyold = dy;
  dx = SH_MAX(dx, 0);
  dy = SH_MAX(dy, 0);
  sx += dx - dxold;
  sy += dy - dyold;
  width -= dx - dxold;
  height -= dy - dyold;
  width = SH_MIN(width, dwidth  - dx);
  height = SH_MIN(height, dheight - dy);
  
  /* Calculate stride from format if not given */
  if (dstStride == -1) dstStride = dwidth * dfd.bytes;
  if (srcStride == -1) srcStride = swidth * sfd.bytes;
  
  if (srcFormat == dstFormat) {
    
    /* Walk pixels and copy */
    for (SY=sy, DY=dy; SY < sy+height; ++SY, ++DY) {
      SD = src + SY * srcStride + sx * sfd.bytes;
      DD = dst + DY * dstStride + dx * dfd.bytes;
      memcpy(DD, SD, width * sfd.bytes);
    }
    
  }else{
  
    /* Walk pixels and copy */
    for (SY=sy, DY=dy; SY < sy+height; ++SY, ++DY) {
      SD = src + SY * srcStride + sx * sfd.bytes;
      DD = dst + DY * dstStride + dx * dfd.bytes;
      
      for (SX=sx, DX=dx; SX < sx+width; ++SX, ++DX) {
        shLoadColor(&c, SD, &sfd);
        shStoreColor(&c, DD, &dfd);
        SD += sfd.bytes; DD += dfd.bytes;
    }}
  }
}

static VGboolean shClipSurfaceRead(VGContext *context,
                                   SHint *dx, SHint *dy,
                                   SHint *sx, SHint *sy,
                                   SHint *width, SHint *height,
                                   SHint destWidth,
                                   SHint destHeight);

static VGboolean shClipImageTransfer(SHint targetWidth,
                                     SHint targetHeight,
                                     SHint sourceWidth,
                                     SHint sourceHeight,
                                     SHint *dx,
                                     SHint *dy,
                                     SHint *sx,
                                     SHint *sy,
                                     SHint *width,
                                     SHint *height)
{
  SHint delta;

  if (targetWidth <= 0 ||
      targetHeight <= 0 ||
      sourceWidth <= 0 ||
      sourceHeight <= 0 ||
      *width <= 0 ||
      *height <= 0)
    return VG_FALSE;

  if (*sx < 0) {
    delta = -*sx;
    *sx = 0;
    *dx += delta;
    *width -= delta;
  }
  if (*sy < 0) {
    delta = -*sy;
    *sy = 0;
    *dy += delta;
    *height -= delta;
  }
  if (*sx >= sourceWidth || *sy >= sourceHeight)
    return VG_FALSE;
  if (*sx + *width > sourceWidth)
    *width = sourceWidth - *sx;
  if (*sy + *height > sourceHeight)
    *height = sourceHeight - *sy;

  if (*dx < 0) {
    delta = -*dx;
    *dx = 0;
    *sx += delta;
    *width -= delta;
  }
  if (*dy < 0) {
    delta = -*dy;
    *dy = 0;
    *sy += delta;
    *height -= delta;
  }
  if (*dx >= targetWidth || *dy >= targetHeight)
    return VG_FALSE;
  if (*dx + *width > targetWidth)
    *width = targetWidth - *dx;
  if (*dy + *height > targetHeight)
    *height = targetHeight - *dy;

  return (*width > 0 && *height > 0) ? VG_TRUE : VG_FALSE;
}

static VGboolean shFormatHasDirectGLStorage(const SHImageFormatDesc *format)
{
  SHuint32 baseFormat;

  if (!format)
    return VG_FALSE;

  baseFormat = format->vgformat & 0x1F;
  return (baseFormat == VG_sRGBX_8888 ||
          baseFormat == VG_sRGBA_8888 ||
          baseFormat == VG_lRGBX_8888 ||
          baseFormat == VG_lRGBA_8888 ||
          baseFormat == VG_sRGB_565 ||
          baseFormat == VG_sRGBA_5551 ||
          baseFormat == VG_sRGBA_4444 ||
          baseFormat == VG_sL_8 ||
          baseFormat == VG_lL_8 ||
          baseFormat == VG_A_8) ? VG_TRUE : VG_FALSE;
}

static VGboolean shCanDirectImageFormat(const SHImage *image,
                                        VGImageFormat dataFormat)
{
  return (image &&
          image->texture != 0 &&
          image->texwidth == image->width &&
          image->texheight == image->height &&
          image->fd.vgformat == dataFormat &&
          shFormatHasDirectGLStorage(&image->fd)) ? VG_TRUE : VG_FALSE;
}

static VGboolean shResolveTransferStride(VGint dataStride,
                                         SHint logicalWidth,
                                         SHint bytesPerPixel,
                                         SHint *resolvedStride,
                                         GLint *rowLength)
{
  size_t minStrideSize;
  SHint minStride;

  if (logicalWidth <= 0 || bytesPerPixel <= 0)
    return VG_FALSE;

  if (!shCheckedSizeMul((size_t)logicalWidth,
                        (size_t)bytesPerPixel,
                        &minStrideSize) ||
      minStrideSize > (size_t)SH_MAX_INT)
    return VG_FALSE;

  minStride = (SHint)minStrideSize;

  if (dataStride == -1)
    dataStride = minStride;

  if (dataStride < minStride ||
      dataStride % bytesPerPixel != 0)
    return VG_FALSE;

  if (resolvedStride)
    *resolvedStride = dataStride;
  if (rowLength)
    *rowLength = dataStride / bytesPerPixel;
  return VG_TRUE;
}

typedef struct
{
  GLint activeTexture;
  GLint textureBinding;
  GLint unpackAlignment;
  GLint unpackRowLength;
  GLint unpackSkipPixels;
  GLint unpackSkipRows;
} SHImageUploadGLState;

static void shSaveImageUploadGLState(SHImageUploadGLState *state)
{
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state->activeTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->textureBinding);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &state->unpackAlignment);
  glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state->unpackRowLength);
  glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &state->unpackSkipPixels);
  glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state->unpackSkipRows);
}

static void shRestoreImageUploadGLState(const SHImageUploadGLState *state)
{
  glPixelStorei(GL_UNPACK_ALIGNMENT, state->unpackAlignment);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, state->unpackRowLength);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, state->unpackSkipPixels);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, state->unpackSkipRows);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, state->textureBinding);
  glActiveTexture(state->activeTexture);
}

typedef struct
{
  GLint framebuffer;
  GLint renderbuffer;
  GLint drawBuffer;
  GLint readBuffer;
  GLint packAlignment;
  GLint packRowLength;
  GLint packSkipPixels;
  GLint packSkipRows;
} SHImageReadGLState;

static void shSaveImageReadGLState(SHImageReadGLState *state)
{
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &state->renderbuffer);
  glGetIntegerv(GL_DRAW_BUFFER, &state->drawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &state->readBuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &state->packAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &state->packRowLength);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &state->packSkipPixels);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &state->packSkipRows);
}

static void shRestoreImageReadGLState(const SHImageReadGLState *state)
{
  glPixelStorei(GL_PACK_ALIGNMENT, state->packAlignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, state->packRowLength);
  glPixelStorei(GL_PACK_SKIP_PIXELS, state->packSkipPixels);
  glPixelStorei(GL_PACK_SKIP_ROWS, state->packSkipRows);
  glBindFramebuffer(GL_FRAMEBUFFER, state->framebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, state->renderbuffer);
  glDrawBuffer(state->drawBuffer);
  glReadBuffer(state->readBuffer);
}

typedef struct
{
  GLint framebuffer;
  GLint renderbuffer;
  GLint viewport[4];
  GLint activeTexture;
  GLint textureBinding;
  GLint drawBuffer;
  GLint readBuffer;
  GLint scissorBox[4];
  GLfloat clearColor[4];
  GLboolean scissor;
  GLboolean colorMask[4];
} SHImageTargetGLState;

static void shSaveImageTargetGLState(SHImageTargetGLState *state)
{
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &state->renderbuffer);
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &state->activeTexture);
  glGetIntegerv(GL_DRAW_BUFFER, &state->drawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &state->readBuffer);
  glGetIntegerv(GL_SCISSOR_BOX, state->scissorBox);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clearColor);
  glGetBooleanv(GL_COLOR_WRITEMASK, state->colorMask);
  state->scissor = glIsEnabled(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->textureBinding);
}

static void shRestoreImageTargetGLState(const SHImageTargetGLState *state)
{
  if (state->scissor) glEnable(GL_SCISSOR_TEST);
  else glDisable(GL_SCISSOR_TEST);

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
  glViewport(state->viewport[0], state->viewport[1],
             state->viewport[2], state->viewport[3]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, state->textureBinding);
  glActiveTexture(state->activeTexture);
}

static VGboolean shTryDirectImageSubData(SHImage *image,
                                         const void *data,
                                         VGint dataStride,
                                         VGImageFormat dataFormat,
                                         VGint x,
                                         VGint y,
                                         VGint width,
                                         VGint height)
{
  SHint copyDx = x;
  SHint copyDy = y;
  SHint copySx = 0;
  SHint copySy = 0;
  SHint copyWidth = width;
  SHint copyHeight = height;
  SHint resolvedStride;
  GLint rowLength;
  const SHuint8 *source;
  SHImageUploadGLState state;

  if (!data)
    return VG_FALSE;

  if (!shClipImageTransfer(image->width, image->height,
                           width, height,
                           &copyDx, &copyDy,
                           &copySx, &copySy,
                           &copyWidth, &copyHeight))
    return VG_TRUE;

  if (!shCanDirectImageFormat(image, dataFormat) ||
      !shResolveTransferStride(dataStride, width, image->fd.bytes,
                               &resolvedStride, &rowLength))
    return VG_FALSE;

  source = (const SHuint8*)data +
           (size_t)copySy * (size_t)resolvedStride +
           (size_t)copySx * (size_t)image->fd.bytes;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, image->texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0,
                  copyDx, copyDy, copyWidth, copyHeight,
                  image->fd.glformat, image->fd.gltype, source);
  shRestoreImageUploadGLState(&state);

  if (glGetError() != GL_NO_ERROR)
    return VG_FALSE;

  image->gpuDataDirty = VG_TRUE;
  return VG_TRUE;
}

static VGboolean shBindTextureReadFramebuffer(GLuint texture,
                                              GLuint *framebuffer)
{
  GLenum status;

  glGenFramebuffers(1, framebuffer);
  if (*framebuffer == 0)
    return VG_FALSE;

  glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, texture, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  return status == GL_FRAMEBUFFER_COMPLETE ? VG_TRUE : VG_FALSE;
}

static VGboolean shCanDirectClearImage(const SHImage *image)
{
  GLenum internalFormat;

  if (!image ||
      image->texture == 0 ||
      image->texwidth != image->width ||
      image->texheight != image->height ||
      !shFormatHasDirectGLStorage(&image->fd))
    return VG_FALSE;

  internalFormat = image->fd.glintformat;
  if (internalFormat == GL_RGBA)
    return VG_TRUE;
  return (internalFormat == GL_RGB ||
          internalFormat == GL_R8) ? VG_TRUE : VG_FALSE;
}

static void shGetImageClearColor(const SHImage *image,
                                 const SHColor *clear,
                                 GLfloat out[4])
{
  SHuint32 baseFormat = image->fd.vgformat & 0x1F;

  if (baseFormat == VG_A_8) {
    out[0] = clear->a;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;
  } else if (baseFormat == VG_sL_8 || baseFormat == VG_lL_8) {
    out[0] = 0.2126f * clear->r +
             0.7152f * clear->g +
             0.0722f * clear->b;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 1.0f;
  } else {
    out[0] = clear->r;
    out[1] = clear->g;
    out[2] = clear->b;
    out[3] = image->fd.amask == 0 ? 1.0f : clear->a;
  }
}

static VGboolean shTryDirectClearImage(SHImage *image,
                                       const SHColor *clear,
                                       VGint x,
                                       VGint y,
                                       VGint width,
                                       VGint height)
{
  SHint copyDx = x;
  SHint copyDy = y;
  SHint copySx = 0;
  SHint copySy = 0;
  SHint copyWidth = width;
  SHint copyHeight = height;
  GLuint framebuffer = 0;
  GLfloat clearColor[4];
  VGboolean success = VG_FALSE;
  SHImageTargetGLState state;

  if (!shClipImageTransfer(image->width, image->height,
                           width, height,
                           &copyDx, &copyDy,
                           &copySx, &copySy,
                           &copyWidth, &copyHeight))
    return VG_TRUE;

  if (!shCanDirectClearImage(image))
    return VG_FALSE;

  shSaveImageTargetGLState(&state);
  if (!shBindTextureReadFramebuffer(image->texture, &framebuffer))
    goto cleanup;

  shGetImageClearColor(image, clear, clearColor);
  glViewport(0, 0, image->width, image->height);
  glEnable(GL_SCISSOR_TEST);
  glScissor(copyDx, copyDy, copyWidth, copyHeight);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(clearColor[0], clearColor[1],
               clearColor[2], clearColor[3]);
  glClear(GL_COLOR_BUFFER_BIT);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;

cleanup:
  shRestoreImageTargetGLState(&state);
  if (framebuffer != 0)
    glDeleteFramebuffers(1, &framebuffer);
  if (success)
    image->gpuDataDirty = VG_TRUE;
  return success;
}

static VGboolean shTryDirectGetImageSubData(SHImage *image,
                                            void *data,
                                            VGint dataStride,
                                            VGImageFormat dataFormat,
                                            VGint x,
                                            VGint y,
                                            VGint width,
                                            VGint height)
{
  SHint copyDx = 0;
  SHint copyDy = 0;
  SHint copySx = x;
  SHint copySy = y;
  SHint copyWidth = width;
  SHint copyHeight = height;
  SHint resolvedStride;
  GLint rowLength;
  SHuint8 *dest;
  GLuint framebuffer = 0;
  VGboolean success = VG_FALSE;
  SHImageReadGLState state;

  if (!data)
    return VG_FALSE;

  if (!shClipImageTransfer(width, height,
                           image->width, image->height,
                           &copyDx, &copyDy,
                           &copySx, &copySy,
                           &copyWidth, &copyHeight))
    return VG_TRUE;

  if (!shCanDirectImageFormat(image, dataFormat) ||
      !shResolveTransferStride(dataStride, width, image->fd.bytes,
                               &resolvedStride, &rowLength))
    return VG_FALSE;

  dest = (SHuint8*)data +
         (size_t)copyDy * (size_t)resolvedStride +
         (size_t)copyDx * (size_t)image->fd.bytes;

  shSaveImageReadGLState(&state);
  if (!shBindTextureReadFramebuffer(image->texture, &framebuffer))
    goto cleanup;

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glReadPixels(copySx, copySy, copyWidth, copyHeight,
               image->fd.glformat, image->fd.gltype, dest);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;

cleanup:
  shRestoreImageReadGLState(&state);
  if (framebuffer != 0)
    glDeleteFramebuffers(1, &framebuffer);
  return success;
}

static VGboolean shTryConvertedImageSubData(SHImage *image,
                                            const void *data,
                                            VGint dataStride,
                                            VGImageFormat dataFormat,
                                            VGint x,
                                            VGint y,
                                            VGint width,
                                            VGint height)
{
  SHImageFormatDesc sourceFormat;
  SHint copyDx = x;
  SHint copyDy = y;
  SHint copySx = 0;
  SHint copySy = 0;
  SHint copyWidth = width;
  SHint copyHeight = height;
  SHint resolvedStride;
  SHuint8 *converted = NULL;
  size_t convertedBytes;
  VGboolean success = VG_FALSE;
  SHImageUploadGLState state;

  if (!data || !image || image->texture == 0)
    return VG_FALSE;

  if (!shClipImageTransfer(image->width, image->height,
                           width, height,
                           &copyDx, &copyDy,
                           &copySx, &copySy,
                           &copyWidth, &copyHeight))
    return VG_TRUE;

  shSetupImageFormat(dataFormat, &sourceFormat);
  if (!shResolveTransferStride(dataStride, width, sourceFormat.bytes,
                               &resolvedStride, NULL) ||
      !shPixelBufferSize(copyWidth, copyHeight,
                         image->fd.bytes, &convertedBytes))
    return VG_FALSE;

  converted = (SHuint8*)malloc(convertedBytes);
  if (!converted)
    return VG_FALSE;

  shCopyPixels(converted, image->fd.vgformat,
               copyWidth * image->fd.bytes,
               (const SHuint8*)data, dataFormat, resolvedStride,
               copyWidth, copyHeight, width, height,
               0, 0, copySx, copySy, copyWidth, copyHeight);

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, image->texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0,
                  copyDx, copyDy, copyWidth, copyHeight,
                  image->fd.glformat, image->fd.gltype, converted);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  shRestoreImageUploadGLState(&state);

  free(converted);

  if (success)
    image->gpuDataDirty = VG_TRUE;
  return success;
}

static VGboolean shTryConvertedGetImageSubData(SHImage *image,
                                               void *data,
                                               VGint dataStride,
                                               VGImageFormat dataFormat,
                                               VGint x,
                                               VGint y,
                                               VGint width,
                                               VGint height)
{
  SHImageFormatDesc destFormat;
  SHint copyDx = 0;
  SHint copyDy = 0;
  SHint copySx = x;
  SHint copySy = y;
  SHint copyWidth = width;
  SHint copyHeight = height;
  SHint resolvedStride;
  SHuint8 *nativePixels = NULL;
  size_t nativeBytes;
  GLuint framebuffer = 0;
  VGboolean success = VG_FALSE;
  SHImageReadGLState state;

  if (!data || !image || image->texture == 0)
    return VG_FALSE;

  if (!shClipImageTransfer(width, height,
                           image->width, image->height,
                           &copyDx, &copyDy,
                           &copySx, &copySy,
                           &copyWidth, &copyHeight))
    return VG_TRUE;

  shSetupImageFormat(dataFormat, &destFormat);
  if (!shResolveTransferStride(dataStride, width, destFormat.bytes,
                               &resolvedStride, NULL) ||
      !shPixelBufferSize(copyWidth, copyHeight,
                         image->fd.bytes, &nativeBytes))
    return VG_FALSE;

  nativePixels = (SHuint8*)malloc(nativeBytes);
  if (!nativePixels)
    return VG_FALSE;

  shSaveImageReadGLState(&state);
  if (!shBindTextureReadFramebuffer(image->texture, &framebuffer))
    goto cleanup;

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glReadPixels(copySx, copySy, copyWidth, copyHeight,
               image->fd.glformat, image->fd.gltype, nativePixels);
  if (glGetError() != GL_NO_ERROR)
    goto cleanup;

  shCopyPixels((SHuint8*)data, dataFormat, resolvedStride,
               nativePixels, image->fd.vgformat,
               copyWidth * image->fd.bytes,
               width, height, copyWidth, copyHeight,
               copyDx, copyDy, 0, 0, copyWidth, copyHeight);
  success = VG_TRUE;

cleanup:
  shRestoreImageReadGLState(&state);
  if (framebuffer != 0)
    glDeleteFramebuffers(1, &framebuffer);
  free(nativePixels);
  return success;
}

typedef struct
{
  GLint packAlignment;
  GLint packRowLength;
  GLint packSkipPixels;
  GLint packSkipRows;
} SHSurfaceReadGLState;

static void shSaveSurfaceReadGLState(SHSurfaceReadGLState *state)
{
  glGetIntegerv(GL_PACK_ALIGNMENT, &state->packAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &state->packRowLength);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &state->packSkipPixels);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &state->packSkipRows);
}

static void shRestoreSurfaceReadGLState(const SHSurfaceReadGLState *state)
{
  glPixelStorei(GL_PACK_ALIGNMENT, state->packAlignment);
  glPixelStorei(GL_PACK_ROW_LENGTH, state->packRowLength);
  glPixelStorei(GL_PACK_SKIP_PIXELS, state->packSkipPixels);
  glPixelStorei(GL_PACK_SKIP_ROWS, state->packSkipRows);
}

static VGboolean shTryDirectReadPixels(VGContext *context,
                                       void *data,
                                       VGint dataStride,
                                       VGImageFormat dataFormat,
                                       VGint sx,
                                       VGint sy,
                                       VGint width,
                                       VGint height)
{
  SHint copyDx = 0;
  SHint copyDy = 0;
  SHint copySx = sx;
  SHint copySy = sy;
  SHint copyWidth = width;
  SHint copyHeight = height;
  SHint resolvedStride;
  GLint rowLength;
  SHuint8 *dest;
  SHSurfaceReadGLState state;

  if (!data || dataFormat != VG_sRGBA_8888)
    return VG_FALSE;

  if (!shClipSurfaceRead(context,
                         &copyDx, &copyDy,
                         &copySx, &copySy,
                         &copyWidth, &copyHeight,
                         width, height))
    return VG_TRUE;

  if (!shResolveTransferStride(dataStride, width, 4,
                               &resolvedStride, &rowLength))
    return VG_FALSE;

  dest = (SHuint8*)data +
         (size_t)copyDy * (size_t)resolvedStride +
         (size_t)copyDx * 4u;

  shSaveSurfaceReadGLState(&state);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glReadPixels(copySx, copySy, copyWidth, copyHeight,
               GL_RGBA, GL_UNSIGNED_BYTE, dest);
  shRestoreSurfaceReadGLState(&state);

  return glGetError() == GL_NO_ERROR ? VG_TRUE : VG_FALSE;
}

typedef struct
{
  GLint viewport[4];
  GLint program;
  GLint vertexArray;
  GLint arrayBuffer;
  GLint activeTexture;
  GLint textureBinding;
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
} SHSurfacePixelGLState;

static void shSaveSurfacePixelGLState(SHSurfacePixelGLState *state)
{
  glGetIntegerv(GL_VIEWPORT, state->viewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vertexArray);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->arrayBuffer);
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
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->textureBinding);
}

static void shRestoreSurfacePixelGLState(const SHSurfacePixelGLState *state)
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
  if (state->vertexArray == 0 ||
      glIsVertexArray((GLuint)state->vertexArray))
    glBindVertexArray((GLuint)state->vertexArray);
  else
    glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->arrayBuffer);
  glViewport(state->viewport[0], state->viewport[1],
             state->viewport[2], state->viewport[3]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, state->textureBinding);
  glActiveTexture(state->activeTexture);
}

static VGboolean shClipSurfaceTransfer(VGContext *context,
                                       SHint *dx, SHint *dy,
                                       SHint *sx, SHint *sy,
                                       SHint *width, SHint *height,
                                       SHint sourceWidth,
                                       SHint sourceHeight)
{
  SHint delta;

  if (!context ||
      context->surfaceWidth <= 0 ||
      context->surfaceHeight <= 0 ||
      sourceWidth <= 0 ||
      sourceHeight <= 0 ||
      *width <= 0 ||
      *height <= 0)
    return VG_FALSE;

  if (*sx < 0) {
    delta = -*sx;
    *sx = 0;
    *dx += delta;
    *width -= delta;
  }
  if (*sy < 0) {
    delta = -*sy;
    *sy = 0;
    *dy += delta;
    *height -= delta;
  }
  if (*sx >= sourceWidth || *sy >= sourceHeight)
    return VG_FALSE;
  if (*sx + *width > sourceWidth)
    *width = sourceWidth - *sx;
  if (*sy + *height > sourceHeight)
    *height = sourceHeight - *sy;

  if (*dx < 0) {
    delta = -*dx;
    *dx = 0;
    *sx += delta;
    *width -= delta;
  }
  if (*dy < 0) {
    delta = -*dy;
    *dy = 0;
    *sy += delta;
    *height -= delta;
  }
  if (*dx >= context->surfaceWidth || *dy >= context->surfaceHeight)
    return VG_FALSE;
  if (*dx + *width > context->surfaceWidth)
    *width = context->surfaceWidth - *dx;
  if (*dy + *height > context->surfaceHeight)
    *height = context->surfaceHeight - *dy;

  return (*width > 0 && *height > 0) ? VG_TRUE : VG_FALSE;
}

static VGboolean shClipSurfaceRead(VGContext *context,
                                   SHint *dx, SHint *dy,
                                   SHint *sx, SHint *sy,
                                   SHint *width, SHint *height,
                                   SHint destWidth,
                                   SHint destHeight)
{
  SHint delta;

  if (!context ||
      context->surfaceWidth <= 0 ||
      context->surfaceHeight <= 0 ||
      destWidth <= 0 ||
      destHeight <= 0 ||
      *width <= 0 ||
      *height <= 0)
    return VG_FALSE;

  if (*sx < 0) {
    delta = -*sx;
    *sx = 0;
    *dx += delta;
    *width -= delta;
  }
  if (*sy < 0) {
    delta = -*sy;
    *sy = 0;
    *dy += delta;
    *height -= delta;
  }
  if (*sx >= context->surfaceWidth || *sy >= context->surfaceHeight)
    return VG_FALSE;
  if (*sx + *width > context->surfaceWidth)
    *width = context->surfaceWidth - *sx;
  if (*sy + *height > context->surfaceHeight)
    *height = context->surfaceHeight - *sy;

  if (*dx < 0) {
    delta = -*dx;
    *dx = 0;
    *sx += delta;
    *width -= delta;
  }
  if (*dy < 0) {
    delta = -*dy;
    *dy = 0;
    *sy += delta;
    *height -= delta;
  }
  if (*dx >= destWidth || *dy >= destHeight)
    return VG_FALSE;
  if (*dx + *width > destWidth)
    *width = destWidth - *dx;
  if (*dy + *height > destHeight)
    *height = destHeight - *dy;

  return (*width > 0 && *height > 0) ? VG_TRUE : VG_FALSE;
}

static VGboolean shCanDrawSurfaceTextureFormat(const SHImageFormatDesc *format)
{
  GLenum internalFormat;

  if (!format || !shFormatHasDirectGLStorage(format))
    return VG_FALSE;

  internalFormat = format->glintformat;
  return (internalFormat == GL_RGBA ||
          internalFormat == GL_RGB ||
          internalFormat == GL_R8) ? VG_TRUE : VG_FALSE;
}

static VGboolean shCanDrawSurfaceImageTexture(const SHImage *image)
{
  return (image &&
          image->texture != 0 &&
          image->texwidth == image->width &&
          image->texheight == image->height &&
          shCanDrawSurfaceTextureFormat(&image->fd)) ? VG_TRUE : VG_FALSE;
}

static VGboolean shDrawSurfaceTexture(VGContext *context,
                                      GLuint texture,
                                      SHint dx, SHint dy,
                                      SHint width, SHint height,
                                      SHint textureWidth,
                                      SHint textureHeight,
                                      SHint sx, SHint sy)
{
  typedef struct
  {
    GLfloat x;
    GLfloat y;
    GLfloat u;
    GLfloat v;
  } SHSurfacePixelVertex;

  static const GLfloat identity4[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  };

  SHSurfacePixelVertex vertices[4];
  SHVertexState vertexState;
  SHSurfacePixelGLState glState;
  VGboolean success;
  GLfloat u0;
  GLfloat u1;
  GLfloat v0;
  GLfloat v1;
  SHint i;

  if (width <= 0 || height <= 0)
    return VG_TRUE;
  if (!context || texture == 0 || textureWidth <= 0 || textureHeight <= 0)
    return VG_FALSE;

  u0 = (GLfloat)sx / (GLfloat)textureWidth;
  u1 = (GLfloat)(sx + width) / (GLfloat)textureWidth;
  v0 = (GLfloat)sy / (GLfloat)textureHeight;
  v1 = (GLfloat)(sy + height) / (GLfloat)textureHeight;

  vertices[0].x = (GLfloat)dx;
  vertices[0].y = (GLfloat)dy;
  vertices[0].u = u0;
  vertices[0].v = v0;
  vertices[1].x = (GLfloat)(dx + width);
  vertices[1].y = (GLfloat)dy;
  vertices[1].u = u1;
  vertices[1].v = v0;
  vertices[2].x = (GLfloat)dx;
  vertices[2].y = (GLfloat)(dy + height);
  vertices[2].u = u0;
  vertices[2].v = v1;
  vertices[3].x = (GLfloat)(dx + width);
  vertices[3].y = (GLfloat)(dy + height);
  vertices[3].u = u1;
  vertices[3].v = v1;

  shSaveSurfacePixelGLState(&glState);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  glViewport(0, 0, context->surfaceWidth, context->surfaceHeight);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glUseProgram(context->progDraw);
  glUniformMatrix4fv(context->locationDraw.model, 1, GL_FALSE, identity4);
  glUniform1i(context->locationDraw.drawMode, 1);
  glUniform1i(context->locationDraw.imageMode, VG_DRAW_IMAGE_NORMAL);
  glUniform1i(context->locationDraw.imageSampler, 0);
  glUniform1i(context->locationDraw.maskEnabled, 0);
  shLoadOneColorMesh(&context->defaultPaint);

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationDraw.pos);
  glVertexAttribPointer(context->locationDraw.pos, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHSurfacePixelVertex), (const GLvoid*)0);
  glEnableVertexAttribArray(context->locationDraw.textureUV);
  glVertexAttribPointer(context->locationDraw.textureUV, 2, GL_FLOAT, GL_FALSE,
                        sizeof(SHSurfacePixelVertex),
                        (const GLvoid*)(2 * sizeof(GLfloat)));

  if (context->scissoring == VG_TRUE) {
    glEnable(GL_SCISSOR_TEST);
    for (i=0; i<context->scissor.size; ++i) {
      SHRectangle *rect = &context->scissor.items[i];
      if (rect->w <= 0.0f || rect->h <= 0.0f)
        continue;
      glScissor((GLint)rect->x, (GLint)rect->y,
                (GLint)rect->w, (GLint)rect->h);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
  } else {
    glDisable(GL_SCISSOR_TEST);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }

  glDisableVertexAttribArray(context->locationDraw.textureUV);
  glDisableVertexAttribArray(context->locationDraw.pos);
  shRestoreVertexState(&vertexState);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  shRestoreSurfacePixelGLState(&glState);

  if (success)
    shMarkRenderTargetDirty(context);
  return success;
}

static VGboolean shTryDirectSetPixels(VGContext *context,
                                      SHImage *image,
                                      VGint dx,
                                      VGint dy,
                                      VGint sx,
                                      VGint sy,
                                      VGint width,
                                      VGint height)
{
  SHint copyDx = dx;
  SHint copyDy = dy;
  SHint copySx = sx;
  SHint copySy = sy;
  SHint copyWidth = width;
  SHint copyHeight = height;

  if (!shClipSurfaceTransfer(context,
                             &copyDx, &copyDy,
                             &copySx, &copySy,
                             &copyWidth, &copyHeight,
                             image->width, image->height))
    return VG_TRUE;

  if (!shCanDrawSurfaceImageTexture(image))
    return VG_FALSE;

  return shDrawSurfaceTexture(context, image->texture,
                              copyDx, copyDy,
                              copyWidth, copyHeight,
                              image->width, image->height,
                              copySx, copySy);
}

static VGboolean shTryDirectWritePixels(VGContext *context,
                                        const void *data,
                                        VGint dataStride,
                                        VGImageFormat dataFormat,
                                        VGint dx,
                                        VGint dy,
                                        VGint width,
                                        VGint height)
{
  SHImageFormatDesc fd;
  SHint copyDx = dx;
  SHint copyDy = dy;
  SHint copySx = 0;
  SHint copySy = 0;
  SHint copyWidth = width;
  SHint copyHeight = height;
  SHint resolvedStride;
  GLint rowLength;
  const SHuint8 *source;
  GLuint texture = 0;
  VGboolean success = VG_FALSE;
  SHImageUploadGLState state;

  if (!data)
    return VG_FALSE;

  if (!shClipSurfaceTransfer(context,
                             &copyDx, &copyDy,
                             &copySx, &copySy,
                             &copyWidth, &copyHeight,
                             width, height))
    return VG_TRUE;

  shSetupImageFormat(dataFormat, &fd);
  if (!shCanDrawSurfaceTextureFormat(&fd) ||
      !shResolveTransferStride(dataStride, width, fd.bytes,
                               &resolvedStride, &rowLength))
    return VG_FALSE;

  source = (const SHuint8*)data +
           (size_t)copySy * (size_t)resolvedStride +
           (size_t)copySx * (size_t)fd.bytes;

  glGenTextures(1, &texture);
  if (texture == 0)
    return VG_FALSE;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  shApplyImageTextureSwizzle(dataFormat);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, fd.glintformat,
               copyWidth, copyHeight, 0,
               fd.glformat, fd.gltype, source);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  shRestoreImageUploadGLState(&state);

  if (success)
    success = shDrawSurfaceTexture(context, texture,
                                   copyDx, copyDy,
                                   copyWidth, copyHeight,
                                   copyWidth, copyHeight, 0, 0);

  glDeleteTextures(1, &texture);
  return success;
}

static VGboolean shTryDirectCopyPixels(VGContext *context,
                                       VGint dx,
                                       VGint dy,
                                       VGint sx,
                                       VGint sy,
                                       VGint width,
                                       VGint height)
{
  SHint copyDx = dx;
  SHint copyDy = dy;
  SHint copySx = sx;
  SHint copySy = sy;
  SHint copyWidth = width;
  SHint copyHeight = height;
  GLuint texture = 0;
  VGboolean success = VG_FALSE;
  SHImageUploadGLState state;

  if (!shClipSurfaceTransfer(context,
                             &copyDx, &copyDy,
                             &copySx, &copySy,
                             &copyWidth, &copyHeight,
                             context->surfaceWidth,
                             context->surfaceHeight))
    return VG_TRUE;

  glGenTextures(1, &texture);
  if (texture == 0)
    return VG_FALSE;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
               copyWidth, copyHeight, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  if (glGetError() == GL_NO_ERROR) {
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0,
                        0, 0,
                        copySx, copySy,
                        copyWidth, copyHeight);
    success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  }
  shRestoreImageUploadGLState(&state);

  if (success)
    success = shDrawSurfaceTexture(context, texture,
                                   copyDx, copyDy,
                                   copyWidth, copyHeight,
                                   copyWidth, copyHeight, 0, 0);

  glDeleteTextures(1, &texture);
  return success;
}

/*---------------------------------------------------------
 * Copies a rectangle area of pixels of size (width,height)
 * from given data buffer to image surface at destination
 * coordinates (x,y)
 *---------------------------------------------------------*/

VG_API_CALL void vgImageSubData(VGImage image,
                                const void * data, VGint dataStride,
                                VGImageFormat dataFormat,
                                VGint x, VGint y, VGint width, VGint height)
{
  SHImage *i;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!shIsValidImage(context, image),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  i = (SHImage*)image;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(i),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  
  /* Reject invalid formats */
  VG_RETURN_ERR_IF(!shIsValidImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);
  
  /* Reject unsupported image formats */
  VG_RETURN_ERR_IF(!shIsSupportedImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(width <= 0 || height <= 0 || !data,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  
  /* TODO: check data array alignment */

  if (shTryDirectImageSubData(i, data, dataStride, dataFormat,
                              x, y, width, height))
    VG_RETURN(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shTryConvertedImageSubData(i, data, dataStride,
                                               dataFormat,
                                               x, y, width, height),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

/*---------------------------------------------------------
 * Copies a rectangle area of pixels of size (width,height)
 * from image surface at source coordinates (x,y) to given
 * data buffer
 *---------------------------------------------------------*/

VG_API_CALL void vgGetImageSubData(VGImage image,
                                   void * data, VGint dataStride,
                                   VGImageFormat dataFormat,
                                   VGint x, VGint y,
                                   VGint width, VGint height)
{
  SHImage *i;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!shIsValidImage(context, image),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  i = (SHImage*)image;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(i),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  
  /* Reject invalid formats */
  VG_RETURN_ERR_IF(!shIsValidImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);
  
  /* Reject unsupported formats */
  VG_RETURN_ERR_IF(!shIsSupportedImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(width <= 0 || height <= 0 || !data,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  
  /* TODO: check data array alignment */

  if (shTryDirectGetImageSubData(i, data, dataStride, dataFormat,
                                 x, y, width, height))
    VG_RETURN(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shTryConvertedGetImageSubData(i, data, dataStride,
                                                  dataFormat,
                                                  x, y, width, height),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

/*----------------------------------------------------------
 * Copies a rectangle area of pixels of size (width,height)
 * from src image surface at source coordinates (sx,sy) to
 * dst image surface at destination cordinates (dx,dy)
 *---------------------------------------------------------*/

VG_API_CALL void vgCopyImage(VGImage dst, VGint dx, VGint dy,
                             VGImage src, VGint sx, VGint sy,
                             VGint width, VGint height,
                             VGboolean dither)
{
  SHImage *s, *d;

  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src; d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shTryTransferCopyImage(context, d, dx, dy, s, sx, sy,
                                           width, height),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

/*---------------------------------------------------------
 * Copies a rectangle area of pixels of size (width,height)
 * from src image surface at source coordinates (sx,sy) to
 * window surface at destination coordinates (dx,dy)
 *---------------------------------------------------------*/

VG_API_CALL void vgSetPixels(VGint dx, VGint dy,
                             VGImage src, VGint sx, VGint sy,
                             VGint width, VGint height)
{
  SHImage *i;

  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!shIsValidImage(context, src),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  i = (SHImage*)src;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(i),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shTryDirectSetPixels(context, i, dx, dy, sx, sy,
                                         width, height),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

/*---------------------------------------------------------
 * Copies a rectangle area of pixels of size (width,height)
 * from given data buffer at source coordinates (sx,sy) to
 * window surface at destination coordinates (dx,dy)
 *---------------------------------------------------------*/

VG_API_CALL void vgWritePixels(const void * data, VGint dataStride,
                               VGImageFormat dataFormat,
                               VGint dx, VGint dy,
                               VGint width, VGint height)
{
  VG_GETCONTEXT(VG_NO_RETVAL);

  /* Reject invalid formats */
  VG_RETURN_ERR_IF(!shIsValidImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);
  
  /* Reject unsupported formats */
  VG_RETURN_ERR_IF(!shIsSupportedImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);

  /* TODO: check output data array alignment */
  
  VG_RETURN_ERR_IF(width <= 0 || height <= 0 || !data,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  if (shTryDirectWritePixels(context, data, dataStride, dataFormat,
                             dx, dy, width, height))
    VG_RETURN(VG_NO_RETVAL);

  VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
}

/*-----------------------------------------------------------
 * Copies a rectangle area of pixels of size (width, height)
 * from window surface at source coordinates (sx, sy) to
 * image surface at destination coordinates (dx, dy)
 *-----------------------------------------------------------*/

VG_API_CALL void vgGetPixels(VGImage dst, VGint dx, VGint dy,
                             VGint sx, VGint sy,
                             VGint width, VGint height)
{
  SHImage *i;
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(!shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  i = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(i),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shTryTransferGetPixels(context, i, dx, dy, sx, sy,
                                           width, height),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  VG_RETURN(VG_NO_RETVAL);
}

/*-----------------------------------------------------------
 * Copies a rectangle area of pixels of size (width, height)
 * from window surface at source coordinates (sx, sy) to
 * to given output data buffer.
 *-----------------------------------------------------------*/

VG_API_CALL void vgReadPixels(void * data, VGint dataStride,
                              VGImageFormat dataFormat,
                              VGint sx, VGint sy,
                              VGint width, VGint height)
{
  SHuint8 *pixels;
  SHImageFormatDesc winfd;
  size_t pixelBytes;
  SHSurfaceReadGLState state;
  SHint copyDx = 0;
  SHint copyDy = 0;
  SHint copySx = sx;
  SHint copySy = sy;
  SHint copyWidth = width;
  SHint copyHeight = height;
  VG_GETCONTEXT(VG_NO_RETVAL);

  /* Reject invalid formats */
  VG_RETURN_ERR_IF(!shIsValidImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);
  
  /* Reject unsupported image formats */
  VG_RETURN_ERR_IF(!shIsSupportedImageFormat(dataFormat),
                   VG_UNSUPPORTED_IMAGE_FORMAT_ERROR,
                   VG_NO_RETVAL);

  /* TODO: check output data array alignment */
  
  VG_RETURN_ERR_IF(width <= 0 || height <= 0 || !data,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  if (shTryDirectReadPixels(context, data, dataStride, dataFormat,
                            sx, sy, width, height))
    VG_RETURN(VG_NO_RETVAL);

  if (!shClipSurfaceRead(context,
                         &copyDx, &copyDy,
                         &copySx, &copySy,
                         &copyWidth, &copyHeight,
                         width, height))
    VG_RETURN(VG_NO_RETVAL);

  /* Setup window image format descriptor */
  /* TODO: this actually depends on the target framebuffer type
     if we really want the copy to be optimized */
  shSetupImageFormat(VG_sRGBA_8888, &winfd);

  if (!shPixelBufferSize(copyWidth, copyHeight,
                         winfd.bytes, &pixelBytes))
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  pixels = (SHuint8*)malloc(pixelBytes);
  SH_RETURN_ERR_IF(!pixels, VG_OUT_OF_MEMORY_ERROR, SH_NO_RETVAL);

  shSaveSurfaceReadGLState(&state);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  glReadPixels(copySx, copySy, copyWidth, copyHeight,
               GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  shRestoreSurfaceReadGLState(&state);
  
  shCopyPixels(data, dataFormat, dataStride,
               pixels, winfd.vgformat, -1,
               width, height, copyWidth, copyHeight,
               copyDx, copyDy, 0, 0, copyWidth, copyHeight);

  free(pixels);
  
  VG_RETURN(VG_NO_RETVAL);
}

/*----------------------------------------------------------
 * Copies a rectangle area of pixels of size (width,height)
 * from window surface at source coordinates (sx,sy) to
 * windows surface at destination cordinates (dx,dy)
 *---------------------------------------------------------*/

VG_API_CALL void vgCopyPixels(VGint dx, VGint dy,
                              VGint sx, VGint sy,
                              VGint width, VGint height)
{
  VG_GETCONTEXT(VG_NO_RETVAL);
  
  VG_RETURN_ERR_IF(width <= 0 || height <= 0,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  if (shTryDirectCopyPixels(context, dx, dy, sx, sy, width, height))
    VG_RETURN(VG_NO_RETVAL);

  VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
}

VG_API_CALL VGImage vgChildImage(VGImage parent,
                                 VGint x, VGint y, VGint width, VGint height)
{
  return VG_INVALID_HANDLE;
}

VG_API_CALL VGImage vgGetParent(VGImage image)
{
  return VG_INVALID_HANDLE;
}

static VGboolean shImageFilterImagesOverlap(const SHImage *dst,
                                            const SHImage *src)
{
  return (dst == src || (dst && src && dst->data == src->data)) ?
    VG_TRUE : VG_FALSE;
}

static VGboolean shImageFilterValidTilingMode(VGTilingMode tilingMode)
{
  return (tilingMode == VG_TILE_FILL ||
          tilingMode == VG_TILE_PAD ||
          tilingMode == VG_TILE_REPEAT ||
          tilingMode == VG_TILE_REFLECT) ? VG_TRUE : VG_FALSE;
}

static VGboolean shImageFilterAligned(const void *ptr, size_t alignment)
{
  uintptr_t value;

  if (!ptr || alignment == 0)
    return VG_FALSE;

  value = (uintptr_t)ptr;
  return (value % alignment) == 0 ? VG_TRUE : VG_FALSE;
}

static VGboolean shImageFilterIsLuminanceFormat(VGImageFormat format)
{
  SHuint32 base = (SHuint32)format & 0x1Fu;

  return (base == VG_sL_8 || base == VG_lL_8) ? VG_TRUE : VG_FALSE;
}

static VGboolean shImageFilterFormatIsLinear(VGImageFormat format)
{
  SHuint32 base = (SHuint32)format & 0x1Fu;

  return (base == VG_lRGBX_8888 ||
          base == VG_lRGBA_8888 ||
          base == VG_lRGBA_8888_PRE ||
          base == VG_lL_8) ? VG_TRUE : VG_FALSE;
}

enum
{
  SH_IMAGE_FILTER_COLOR_MATRIX = 0,
  SH_IMAGE_FILTER_CONVOLVE = 1,
  SH_IMAGE_FILTER_SEPARABLE_X = 2,
  SH_IMAGE_FILTER_SEPARABLE_Y = 3,
  SH_IMAGE_FILTER_GAUSSIAN_X = 4,
  SH_IMAGE_FILTER_GAUSSIAN_Y = 5,
  SH_IMAGE_FILTER_LOOKUP = 6,
  SH_IMAGE_FILTER_LOOKUP_SINGLE = 7,
  SH_IMAGE_FILTER_TRANSFER = 8
};

enum
{
  SH_IMAGE_FILTER_STORE_RGBA = 0,
  SH_IMAGE_FILTER_STORE_ALPHA = 1,
  SH_IMAGE_FILTER_STORE_LUMINANCE = 2,
  SH_IMAGE_FILTER_STORE_FLOAT = 3
};

typedef struct
{
  GLint framebuffer;
  GLint renderbuffer;
  GLint viewport[4];
  GLint program;
  GLint vertexArray;
  GLint arrayBuffer;
  GLint activeTexture;
  GLint sourceTextureBinding;
  GLint auxTextureBinding;
  GLint drawBuffer;
  GLint readBuffer;
  GLint scissorBox[4];
  GLboolean blend;
  GLboolean scissor;
  GLboolean depth;
  GLboolean stencil;
  GLboolean colorMask[4];
} SHImageFilterGLState;

typedef struct
{
  SHint mode;
  SHint sourceWidth;
  SHint sourceHeight;
  SHint kernelWidth;
  SHint kernelHeight;
  SHint shiftX;
  SHint shiftY;
  SHfloat scale;
  SHfloat bias;
  VGTilingMode tilingMode;
  GLfloat colorMatrix[16];
  GLfloat colorBias[4];
  GLfloat tileFillColor[4];
  VGboolean sourceLinear;
  VGboolean filterLinear;
  VGboolean outputLinear;
  VGboolean dstLinear;
  VGboolean premultiplyInput;
  VGboolean unpremultiplyOutput;
  SHint dstStorageMode;
  SHint lookupSourceChannel;
} SHImageFilterPass;

static void shSaveImageFilterGLState(SHImageFilterGLState *state)
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
  glGetBooleanv(GL_COLOR_WRITEMASK, state->colorMask);
  state->blend = glIsEnabled(GL_BLEND);
  state->scissor = glIsEnabled(GL_SCISSOR_TEST);
  state->depth = glIsEnabled(GL_DEPTH_TEST);
  state->stencil = glIsEnabled(GL_STENCIL_TEST);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->sourceTextureBinding);
  glActiveTexture(GL_TEXTURE1);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->auxTextureBinding);
  glActiveTexture(state->activeTexture);
}

static void shRestoreImageFilterGLState(const SHImageFilterGLState *state)
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
  glBindTexture(GL_TEXTURE_2D, (GLuint)state->auxTextureBinding);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)state->sourceTextureBinding);
  glActiveTexture(state->activeTexture);
}

static void shImageFilterPremultiplyColor(GLfloat color[4])
{
  color[0] *= color[3];
  color[1] *= color[3];
  color[2] *= color[3];
}

static void shImageFilterEdgeColor(VGContext *context, GLfloat color[4])
{
  color[0] = context->tileFillColor.r;
  color[1] = context->tileFillColor.g;
  color[2] = context->tileFillColor.b;
  color[3] = context->tileFillColor.a;

  if (context->filterFormatPremultiplied)
    shImageFilterPremultiplyColor(color);
}

static SHint shImageFilterStorageMode(const SHImage *image)
{
  SHuint32 base;

  if (!image)
    return SH_IMAGE_FILTER_STORE_RGBA;

  base = (SHuint32)image->fd.vgformat & 0x1Fu;
  if (base == VG_A_8)
    return SH_IMAGE_FILTER_STORE_ALPHA;
  if (shImageFilterIsLuminanceFormat(image->fd.vgformat))
    return SH_IMAGE_FILTER_STORE_LUMINANCE;
  return SH_IMAGE_FILTER_STORE_RGBA;
}

static void shImageFilterSetColorMask(const SHImage *dst,
                                      VGbitfield channelMask,
                                      SHint storageMode)
{
  channelMask &= VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA;

  if (storageMode == SH_IMAGE_FILTER_STORE_FLOAT) {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  } else if (storageMode == SH_IMAGE_FILTER_STORE_LUMINANCE) {
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
  } else if (storageMode == SH_IMAGE_FILTER_STORE_ALPHA) {
    glColorMask((channelMask & VG_ALPHA) ? GL_TRUE : GL_FALSE,
                GL_FALSE, GL_FALSE, GL_FALSE);
  } else {
    glColorMask((channelMask & VG_RED) ? GL_TRUE : GL_FALSE,
                (channelMask & VG_GREEN) ? GL_TRUE : GL_FALSE,
                (channelMask & VG_BLUE) ? GL_TRUE : GL_FALSE,
                ((!dst || dst->fd.amask != 0) &&
                 (channelMask & VG_ALPHA)) ? GL_TRUE : GL_FALSE);
  }
}

static VGboolean shImageFilterEnsureFramebuffer(VGContext *context)
{
  if (context->filterFramebuffer == 0)
    glGenFramebuffers(1, &context->filterFramebuffer);
  return context->filterFramebuffer != 0 ? VG_TRUE : VG_FALSE;
}

static VGboolean shImageFilterAttachTarget(VGContext *context,
                                           GLuint texture)
{
  GLenum status;

  if (!shImageFilterEnsureFramebuffer(context))
    return VG_FALSE;

  glBindFramebuffer(GL_FRAMEBUFFER, context->filterFramebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, texture, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  return status == GL_FRAMEBUFFER_COMPLETE ? VG_TRUE : VG_FALSE;
}

static void shImageFilterSetTextureParams(GLuint texture)
{
  if (texture == 0)
    return;

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

static VGboolean shImageFilterEnsureScratch(VGContext *context,
                                            SHint width,
                                            SHint height)
{
  SHImageUploadGLState state;

  if (width <= 0 || height <= 0)
    return VG_FALSE;

  if (context->filterScratchTexture != 0 &&
      context->filterScratchWidth == width &&
      context->filterScratchHeight == height)
    return VG_TRUE;

  if (context->filterScratchTexture == 0)
    glGenTextures(1, &context->filterScratchTexture);
  if (context->filterScratchTexture == 0)
    return VG_FALSE;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, context->filterScratchTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
               width, height, 0, GL_RGBA, GL_FLOAT, NULL);
  shRestoreImageUploadGLState(&state);

  if (glGetError() != GL_NO_ERROR)
    return VG_FALSE;

  context->filterScratchWidth = width;
  context->filterScratchHeight = height;
  return VG_TRUE;
}

static VGboolean shImageFilterCreateFloatTexture(GLuint *texture,
                                                 SHint width,
                                                 SHint height,
                                                 const void *data,
                                                 GLenum internalFormat,
                                                 GLenum format,
                                                 GLenum type)
{
  SHImageUploadGLState state;
  VGboolean success;

  *texture = 0;
  if (width <= 0 || height <= 0 || !data)
    return VG_FALSE;

  glGenTextures(1, texture);
  if (*texture == 0)
    return VG_FALSE;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, *texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
               width, height, 0, format, type, data);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  shRestoreImageUploadGLState(&state);

  if (!success) {
    glDeleteTextures(1, texture);
    *texture = 0;
  }
  return success;
}

static VGboolean shImageFilterCreateConvolveKernelTexture(
  const VGshort *kernel,
  SHint kernelWidth,
  SHint kernelHeight,
  GLuint *texture)
{
  GLfloat *data;
  size_t count;
  SHint x, y;
  VGboolean success;

  count = (size_t)kernelWidth * (size_t)kernelHeight;
  data = (GLfloat*)malloc(count * sizeof(GLfloat));
  if (!data)
    return VG_FALSE;

  for (y=0; y<kernelHeight; ++y)
    for (x=0; x<kernelWidth; ++x)
      data[y * kernelWidth + x] =
        (GLfloat)kernel[x * kernelHeight + y];

  success = shImageFilterCreateFloatTexture(texture,
                                            kernelWidth,
                                            kernelHeight,
                                            data,
                                            GL_R32F,
                                            GL_RED,
                                            GL_FLOAT);
  free(data);
  return success;
}

static VGboolean shImageFilterCreateShortKernelTexture(
  const VGshort *kernel,
  SHint kernelSize,
  GLuint *texture)
{
  GLfloat *data;
  SHint i;
  VGboolean success;

  data = (GLfloat*)malloc((size_t)kernelSize * sizeof(GLfloat));
  if (!data)
    return VG_FALSE;

  for (i=0; i<kernelSize; ++i)
    data[i] = (GLfloat)kernel[i];

  success = shImageFilterCreateFloatTexture(texture,
                                            kernelSize,
                                            1,
                                            data,
                                            GL_R32F,
                                            GL_RED,
                                            GL_FLOAT);
  free(data);
  return success;
}

static VGboolean shImageFilterCreateFloatKernelTexture(
  const GLfloat *kernel,
  SHint kernelSize,
  GLuint *texture)
{
  return shImageFilterCreateFloatTexture(texture,
                                         kernelSize,
                                         1,
                                         kernel,
                                         GL_R32F,
                                         GL_RED,
                                         GL_FLOAT);
}

static VGboolean shImageFilterCreateLookupTexture(
  const VGubyte *redLUT,
  const VGubyte *greenLUT,
  const VGubyte *blueLUT,
  const VGubyte *alphaLUT,
  GLuint *texture)
{
  SHuint8 data[256 * 4];
  SHint i;

  for (i=0; i<256; ++i) {
    data[i * 4 + 0] = redLUT[i];
    data[i * 4 + 1] = greenLUT[i];
    data[i * 4 + 2] = blueLUT[i];
    data[i * 4 + 3] = alphaLUT[i];
  }

  return shImageFilterCreateFloatTexture(texture,
                                         256,
                                         1,
                                         data,
                                         GL_RGBA,
                                         GL_RGBA,
                                         GL_UNSIGNED_BYTE);
}

static VGboolean shImageFilterCreateSingleLookupTexture(
  const VGuint *lookupTable,
  GLuint *texture)
{
  SHuint8 data[256 * 4];
  SHint i;

  for (i=0; i<256; ++i) {
    VGuint value = lookupTable[i];
    data[i * 4 + 0] = (SHuint8)((value >> 24) & 0xffu);
    data[i * 4 + 1] = (SHuint8)((value >> 16) & 0xffu);
    data[i * 4 + 2] = (SHuint8)((value >> 8) & 0xffu);
    data[i * 4 + 3] = (SHuint8)(value & 0xffu);
  }

  return shImageFilterCreateFloatTexture(texture,
                                         256,
                                         1,
                                         data,
                                         GL_RGBA,
                                         GL_RGBA,
                                         GL_UNSIGNED_BYTE);
}

static void shImageFilterInitPass(VGContext *context,
                                  const SHImage *src,
                                  const SHImage *dst,
                                  SHImageFilterPass *pass)
{
  memset(pass, 0, sizeof(*pass));
  pass->sourceWidth = src ? src->width : 0;
  pass->sourceHeight = src ? src->height : 0;
  pass->scale = 1.0f;
  pass->bias = 0.0f;
  pass->tilingMode = VG_TILE_PAD;
  pass->sourceLinear = src ?
    shImageFilterFormatIsLinear(src->fd.vgformat) : VG_FALSE;
  pass->filterLinear = context->filterFormatLinear;
  pass->outputLinear = context->filterFormatLinear;
  pass->dstLinear = dst ?
    shImageFilterFormatIsLinear(dst->fd.vgformat) : context->filterFormatLinear;
  pass->premultiplyInput = context->filterFormatPremultiplied;
  pass->unpremultiplyOutput = context->filterFormatPremultiplied;
  pass->dstStorageMode = dst ?
    shImageFilterStorageMode(dst) : SH_IMAGE_FILTER_STORE_RGBA;
  shImageFilterEdgeColor(context, pass->tileFillColor);
}

static VGboolean shImageFilterRunPass(VGContext *context,
                                      SHImage *dst,
                                      GLuint targetTexture,
                                      SHint targetWidth,
                                      SHint targetHeight,
                                      GLuint sourceTexture,
                                      GLuint auxTexture,
                                      VGbitfield channelMask,
                                      const SHImageFilterPass *pass)
{
  typedef struct
  {
    GLfloat x;
    GLfloat y;
  } SHImageFilterVertex;

  SHImageFilterVertex vertices[4];
  SHImageFilterGLState glState;
  SHVertexState vertexState;
  VGboolean success = VG_FALSE;

  if (!context || targetTexture == 0 || sourceTexture == 0 ||
      targetWidth <= 0 || targetHeight <= 0)
    return VG_FALSE;

  vertices[0].x = 0.0f;
  vertices[0].y = 0.0f;
  vertices[1].x = (GLfloat)targetWidth;
  vertices[1].y = 0.0f;
  vertices[2].x = 0.0f;
  vertices[2].y = (GLfloat)targetHeight;
  vertices[3].x = (GLfloat)targetWidth;
  vertices[3].y = (GLfloat)targetHeight;

  shSaveImageFilterGLState(&glState);

  if (!shImageFilterAttachTarget(context, targetTexture))
    goto cleanup;

  glViewport(0, 0, targetWidth, targetHeight);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  shImageFilterSetColorMask(dst, channelMask, pass->dstStorageMode);

  glUseProgram(context->progImageFilter);
  glUniform1i(context->locationImageFilter.mode, pass->mode);
  glUniform1i(context->locationImageFilter.sourceSampler, 0);
  glUniform1i(context->locationImageFilter.auxSampler, 1);
  glUniform2f(context->locationImageFilter.targetSize,
              (GLfloat)targetWidth, (GLfloat)targetHeight);
  glUniform2i(context->locationImageFilter.sourceSize,
              pass->sourceWidth, pass->sourceHeight);
  glUniform2i(context->locationImageFilter.sourceOrigin, 0, 0);
  glUniform2i(context->locationImageFilter.targetOrigin, 0, 0);
  glUniform2i(context->locationImageFilter.kernelSize,
              pass->kernelWidth, pass->kernelHeight);
  glUniform2i(context->locationImageFilter.shift,
              pass->shiftX, pass->shiftY);
  glUniform1f(context->locationImageFilter.scale, pass->scale);
  glUniform1f(context->locationImageFilter.bias, pass->bias);
  glUniform1i(context->locationImageFilter.tilingMode, pass->tilingMode);
  glUniform4fv(context->locationImageFilter.colorMatrix,
               4, pass->colorMatrix);
  glUniform4fv(context->locationImageFilter.colorBias,
               1, pass->colorBias);
  glUniform4fv(context->locationImageFilter.tileFillColor,
               1, pass->tileFillColor);
  glUniform1i(context->locationImageFilter.sourceLinear,
              pass->sourceLinear ? 1 : 0);
  glUniform1i(context->locationImageFilter.filterLinear,
              pass->filterLinear ? 1 : 0);
  glUniform1i(context->locationImageFilter.outputLinear,
              pass->outputLinear ? 1 : 0);
  glUniform1i(context->locationImageFilter.dstLinear,
              pass->dstLinear ? 1 : 0);
  glUniform1i(context->locationImageFilter.premultiplyInput,
              pass->premultiplyInput ? 1 : 0);
  glUniform1i(context->locationImageFilter.unpremultiplyOutput,
              pass->unpremultiplyOutput ? 1 : 0);
  glUniform1i(context->locationImageFilter.dstStorageMode,
              pass->dstStorageMode);
  glUniform1i(context->locationImageFilter.lookupSourceChannel,
              pass->lookupSourceChannel);

  glActiveTexture(GL_TEXTURE0);
  shImageFilterSetTextureParams(sourceTexture);
  glActiveTexture(GL_TEXTURE1);
  shImageFilterSetTextureParams(auxTexture);

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationImageFilter.pos);
  glVertexAttribPointer(context->locationImageFilter.pos, 2, GL_FLOAT,
                        GL_FALSE, sizeof(SHImageFilterVertex),
                        (const GLvoid*)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(context->locationImageFilter.pos);
  shRestoreVertexState(&vertexState);

  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;

cleanup:
  shRestoreImageFilterGLState(&glState);
  return success;
}

static VGboolean shImageTransferCanUseImage(const SHImage *image)
{
  return (image &&
          image->texture != 0 &&
          image->texwidth == image->width &&
          image->texheight == image->height &&
          shFormatHasDirectGLStorage(&image->fd)) ? VG_TRUE : VG_FALSE;
}

static VGboolean shImageTransferCreateTexture(GLuint *texture,
                                              SHint width,
                                              SHint height)
{
  SHImageUploadGLState state;
  VGboolean success;

  *texture = 0;
  if (width <= 0 || height <= 0)
    return VG_FALSE;

  glGenTextures(1, texture);
  if (*texture == 0)
    return VG_FALSE;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, *texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  shApplyImageTextureSwizzle(VG_sRGBA_8888);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
               width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  shRestoreImageUploadGLState(&state);

  if (!success) {
    glDeleteTextures(1, texture);
    *texture = 0;
  }
  return success;
}

static VGboolean shImageTransferCopySurfaceToTexture(GLuint texture,
                                                     SHint sx,
                                                     SHint sy,
                                                     SHint width,
                                                     SHint height)
{
  SHImageUploadGLState state;
  VGboolean success;

  if (texture == 0 || width <= 0 || height <= 0)
    return VG_FALSE;

  shSaveImageUploadGLState(&state);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                      sx, sy, width, height);
  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;
  shRestoreImageUploadGLState(&state);
  return success;
}

static VGboolean shImageTransferRunPass(VGContext *context,
                                        SHImage *dst,
                                        GLuint targetTexture,
                                        SHint targetWidth,
                                        SHint targetHeight,
                                        GLuint sourceTexture,
                                        SHint sourceWidth,
                                        SHint sourceHeight,
                                        SHint dx,
                                        SHint dy,
                                        SHint sx,
                                        SHint sy,
                                        SHint width,
                                        SHint height,
                                        SHint dstStorageMode)
{
  typedef struct
  {
    GLfloat x;
    GLfloat y;
  } SHImageTransferVertex;

  SHImageTransferVertex vertices[4];
  SHImageFilterGLState glState;
  SHVertexState vertexState;
  VGboolean success = VG_FALSE;

  if (width <= 0 || height <= 0)
    return VG_TRUE;
  if (!context || targetTexture == 0 || sourceTexture == 0 ||
      targetWidth <= 0 || targetHeight <= 0 ||
      sourceWidth <= 0 || sourceHeight <= 0)
    return VG_FALSE;

  vertices[0].x = (GLfloat)dx;
  vertices[0].y = (GLfloat)dy;
  vertices[1].x = (GLfloat)(dx + width);
  vertices[1].y = (GLfloat)dy;
  vertices[2].x = (GLfloat)dx;
  vertices[2].y = (GLfloat)(dy + height);
  vertices[3].x = (GLfloat)(dx + width);
  vertices[3].y = (GLfloat)(dy + height);

  shSaveImageFilterGLState(&glState);

  if (!shImageFilterAttachTarget(context, targetTexture))
    goto cleanup;

  glViewport(0, 0, targetWidth, targetHeight);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  shImageFilterSetColorMask(dst,
                            VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA,
                            dstStorageMode);

  glUseProgram(context->progImageFilter);
  glUniform1i(context->locationImageFilter.mode, SH_IMAGE_FILTER_TRANSFER);
  glUniform1i(context->locationImageFilter.sourceSampler, 0);
  glUniform1i(context->locationImageFilter.auxSampler, 1);
  glUniform2f(context->locationImageFilter.targetSize,
              (GLfloat)targetWidth, (GLfloat)targetHeight);
  glUniform2i(context->locationImageFilter.sourceSize,
              sourceWidth, sourceHeight);
  glUniform2i(context->locationImageFilter.sourceOrigin, sx, sy);
  glUniform2i(context->locationImageFilter.targetOrigin, dx, dy);
  glUniform1i(context->locationImageFilter.outputLinear, 0);
  glUniform1i(context->locationImageFilter.dstLinear, 0);
  glUniform1i(context->locationImageFilter.premultiplyInput, 0);
  glUniform1i(context->locationImageFilter.unpremultiplyOutput, 0);
  glUniform1i(context->locationImageFilter.dstStorageMode,
              dstStorageMode);

  glActiveTexture(GL_TEXTURE0);
  shImageFilterSetTextureParams(sourceTexture);

  shBindContextVertexState(context, &vertexState);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(context->locationImageFilter.pos);
  glVertexAttribPointer(context->locationImageFilter.pos, 2, GL_FLOAT,
                        GL_FALSE, sizeof(SHImageTransferVertex),
                        (const GLvoid*)0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(context->locationImageFilter.pos);
  shRestoreVertexState(&vertexState);

  success = (glGetError() == GL_NO_ERROR) ? VG_TRUE : VG_FALSE;

cleanup:
  shRestoreImageFilterGLState(&glState);
  return success;
}

static VGboolean shTryTransferCopyImage(VGContext *context,
                                        SHImage *dst,
                                        VGint dx,
                                        VGint dy,
                                        SHImage *src,
                                        VGint sx,
                                        VGint sy,
                                        VGint width,
                                        VGint height)
{
  SHint copyDx = dx;
  SHint copyDy = dy;
  SHint copySx = sx;
  SHint copySy = sy;
  SHint copyWidth = width;
  SHint copyHeight = height;
  GLuint sourceTexture;
  SHint sourceWidth;
  SHint sourceHeight;
  GLuint tempTexture = 0;
  VGboolean success;

  if (!shClipImageTransfer(dst->width, dst->height,
                           src->width, src->height,
                           &copyDx, &copyDy,
                           &copySx, &copySy,
                           &copyWidth, &copyHeight))
    return VG_TRUE;

  if (!shImageTransferCanUseImage(dst) ||
      !shImageTransferCanUseImage(src))
    return VG_FALSE;

  sourceTexture = src->texture;
  sourceWidth = src->width;
  sourceHeight = src->height;

  if (shImageFilterImagesOverlap(dst, src)) {
    if (!shImageTransferCreateTexture(&tempTexture,
                                      copyWidth, copyHeight))
      return VG_FALSE;

    if (!shImageTransferRunPass(context, NULL, tempTexture,
                                copyWidth, copyHeight,
                                src->texture, src->width, src->height,
                                0, 0, copySx, copySy,
                                copyWidth, copyHeight,
                                SH_IMAGE_FILTER_STORE_RGBA)) {
      glDeleteTextures(1, &tempTexture);
      return VG_FALSE;
    }

    sourceTexture = tempTexture;
    sourceWidth = copyWidth;
    sourceHeight = copyHeight;
    copySx = 0;
    copySy = 0;
  }

  success = shImageTransferRunPass(context, dst, dst->texture,
                                   dst->width, dst->height,
                                   sourceTexture,
                                   sourceWidth, sourceHeight,
                                   copyDx, copyDy, copySx, copySy,
                                   copyWidth, copyHeight,
                                   shImageFilterStorageMode(dst));

  if (tempTexture != 0)
    glDeleteTextures(1, &tempTexture);
  if (success)
    dst->gpuDataDirty = VG_TRUE;
  return success;
}

static VGboolean shTryTransferGetPixels(VGContext *context,
                                        SHImage *image,
                                        VGint dx,
                                        VGint dy,
                                        VGint sx,
                                        VGint sy,
                                        VGint width,
                                        VGint height)
{
  SHint copyDx = dx;
  SHint copyDy = dy;
  SHint copySx = sx;
  SHint copySy = sy;
  SHint copyWidth = width;
  SHint copyHeight = height;
  GLuint tempTexture = 0;
  VGboolean success;

  if (!shClipSurfaceRead(context,
                         &copyDx, &copyDy,
                         &copySx, &copySy,
                         &copyWidth, &copyHeight,
                         image->width, image->height))
    return VG_TRUE;

  if (!shImageTransferCanUseImage(image))
    return VG_FALSE;

  if (!shImageTransferCreateTexture(&tempTexture,
                                    copyWidth, copyHeight))
    return VG_FALSE;

  if (!shImageTransferCopySurfaceToTexture(tempTexture, copySx, copySy,
                                           copyWidth, copyHeight)) {
    glDeleteTextures(1, &tempTexture);
    return VG_FALSE;
  }

  success = shImageTransferRunPass(context, image, image->texture,
                                   image->width, image->height,
                                   tempTexture, copyWidth, copyHeight,
                                   copyDx, copyDy, 0, 0,
                                   copyWidth, copyHeight,
                                   shImageFilterStorageMode(image));

  glDeleteTextures(1, &tempTexture);
  if (success)
    image->gpuDataDirty = VG_TRUE;
  return success;
}

VG_API_CALL void vgColorMatrix(VGImage dst, VGImage src,
                               const VGfloat * matrix)
{
  SHImage *s, *d;
  SHint width, height;
  SHImageFilterPass pass;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!matrix ||
                   !shImageFilterAligned(matrix, sizeof(VGfloat)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src;
  d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(shImageFilterImagesOverlap(d, s),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  width = SH_MIN(d->width, s->width);
  height = SH_MIN(d->height, s->height);

  shImageFilterInitPass(context, s, d, &pass);
  pass.mode = SH_IMAGE_FILTER_COLOR_MATRIX;
  pass.colorMatrix[0] = matrix[0];
  pass.colorMatrix[1] = matrix[4];
  pass.colorMatrix[2] = matrix[8];
  pass.colorMatrix[3] = matrix[12];
  pass.colorMatrix[4] = matrix[1];
  pass.colorMatrix[5] = matrix[5];
  pass.colorMatrix[6] = matrix[9];
  pass.colorMatrix[7] = matrix[13];
  pass.colorMatrix[8] = matrix[2];
  pass.colorMatrix[9] = matrix[6];
  pass.colorMatrix[10] = matrix[10];
  pass.colorMatrix[11] = matrix[14];
  pass.colorMatrix[12] = matrix[3];
  pass.colorMatrix[13] = matrix[7];
  pass.colorMatrix[14] = matrix[11];
  pass.colorMatrix[15] = matrix[15];
  pass.colorBias[0] = matrix[16];
  pass.colorBias[1] = matrix[17];
  pass.colorBias[2] = matrix[18];
  pass.colorBias[3] = matrix[19];

  VG_RETURN_ERR_IF(!shImageFilterRunPass(context, d, d->texture,
                                         width, height,
                                         s->texture, 0,
                                         context->filterChannelMask,
                                         &pass),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  d->gpuDataDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgConvolve(VGImage dst, VGImage src,
                            VGint kernelWidth, VGint kernelHeight,
                            VGint shiftX, VGint shiftY,
                            const VGshort * kernel,
                            VGfloat scale,
                            VGfloat bias,
                            VGTilingMode tilingMode)
{
  SHImage *s, *d;
  SHint width, height;
  GLuint kernelTexture = 0;
  SHImageFilterPass pass;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!kernel ||
                   !shImageFilterAligned(kernel, sizeof(VGshort)) ||
                   kernelWidth <= 0 ||
                   kernelHeight <= 0 ||
                   kernelWidth > SH_MAX_KERNEL_SIZE ||
                   kernelHeight > SH_MAX_KERNEL_SIZE,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!shImageFilterValidTilingMode(tilingMode),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src;
  d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(shImageFilterImagesOverlap(d, s),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shImageFilterCreateConvolveKernelTexture(
                     kernel, kernelWidth, kernelHeight, &kernelTexture),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  width = SH_MIN(d->width, s->width);
  height = SH_MIN(d->height, s->height);

  shImageFilterInitPass(context, s, d, &pass);
  pass.mode = SH_IMAGE_FILTER_CONVOLVE;
  pass.kernelWidth = kernelWidth;
  pass.kernelHeight = kernelHeight;
  pass.shiftX = shiftX;
  pass.shiftY = shiftY;
  pass.scale = scale;
  pass.bias = bias;
  pass.tilingMode = tilingMode;

  if (!shImageFilterRunPass(context, d, d->texture,
                            width, height,
                            s->texture, kernelTexture,
                            context->filterChannelMask,
                            &pass)) {
    glDeleteTextures(1, &kernelTexture);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  }

  glDeleteTextures(1, &kernelTexture);
  d->gpuDataDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgSeparableConvolve(VGImage dst, VGImage src,
                                     VGint kernelWidth,
                                     VGint kernelHeight,
                                     VGint shiftX, VGint shiftY,
                                     const VGshort * kernelX,
                                     const VGshort * kernelY,
                                     VGfloat scale,
                                     VGfloat bias,
                                     VGTilingMode tilingMode)
{
  SHImage *s, *d;
  GLuint kernelXTexture = 0;
  GLuint kernelYTexture = 0;
  SHfloat kernelXSum = 0.0f;
  SHint width, height;
  SHint k;
  SHImageFilterPass pass;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!kernelX ||
                   !kernelY ||
                   !shImageFilterAligned(kernelX, sizeof(VGshort)) ||
                   !shImageFilterAligned(kernelY, sizeof(VGshort)) ||
                   kernelWidth <= 0 ||
                   kernelHeight <= 0 ||
                   kernelWidth > SH_MAX_SEPARABLE_KERNEL_SIZE ||
                   kernelHeight > SH_MAX_SEPARABLE_KERNEL_SIZE,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!shImageFilterValidTilingMode(tilingMode),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src;
  d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(shImageFilterImagesOverlap(d, s),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  for (k=0; k<kernelWidth; ++k)
    kernelXSum += (SHfloat)kernelX[k];

  if (!shImageFilterCreateShortKernelTexture(kernelX,
                                             kernelWidth,
                                             &kernelXTexture) ||
      !shImageFilterCreateShortKernelTexture(kernelY,
                                             kernelHeight,
                                             &kernelYTexture) ||
      !shImageFilterEnsureScratch(context, s->width, s->height))
    goto separable_out_of_memory;

  shImageFilterInitPass(context, s, NULL, &pass);
  pass.mode = SH_IMAGE_FILTER_SEPARABLE_X;
  pass.kernelWidth = kernelWidth;
  pass.kernelHeight = 1;
  pass.shiftX = shiftX;
  pass.tilingMode = tilingMode;
  pass.dstStorageMode = SH_IMAGE_FILTER_STORE_FLOAT;

  if (!shImageFilterRunPass(context, NULL, context->filterScratchTexture,
                            s->width, s->height,
                            s->texture, kernelXTexture,
                            VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA,
                            &pass))
    goto separable_out_of_memory;

  width = SH_MIN(d->width, s->width);
  height = SH_MIN(d->height, s->height);
  shImageFilterInitPass(context, s, d, &pass);
  pass.mode = SH_IMAGE_FILTER_SEPARABLE_Y;
  pass.sourceWidth = s->width;
  pass.sourceHeight = s->height;
  pass.kernelWidth = 1;
  pass.kernelHeight = kernelHeight;
  pass.shiftY = shiftY;
  pass.scale = scale;
  pass.bias = bias;
  pass.tilingMode = tilingMode;
  pass.premultiplyInput = VG_FALSE;
  pass.tileFillColor[0] *= kernelXSum;
  pass.tileFillColor[1] *= kernelXSum;
  pass.tileFillColor[2] *= kernelXSum;
  pass.tileFillColor[3] *= kernelXSum;

  if (!shImageFilterRunPass(context, d, d->texture,
                            width, height,
                            context->filterScratchTexture,
                            kernelYTexture,
                            context->filterChannelMask,
                            &pass))
    goto separable_out_of_memory;

  glDeleteTextures(1, &kernelYTexture);
  glDeleteTextures(1, &kernelXTexture);
  d->gpuDataDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);

separable_out_of_memory:
  if (kernelYTexture != 0)
    glDeleteTextures(1, &kernelYTexture);
  if (kernelXTexture != 0)
    glDeleteTextures(1, &kernelXTexture);
  VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
}

VG_API_CALL void vgGaussianBlur(VGImage dst, VGImage src,
                                VGfloat stdDeviationX,
                                VGfloat stdDeviationY,
                                VGTilingMode tilingMode)
{
  SHImage *s, *d;
  SHfloat *kernelX = NULL;
  SHfloat *kernelY = NULL;
  GLuint kernelXTexture = 0;
  GLuint kernelYTexture = 0;
  SHint radiusX, radiusY;
  SHint sizeX, sizeY;
  SHint width, height;
  SHint k;
  SHImageFilterPass pass;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!shImageFilterValidTilingMode(tilingMode) ||
                   SH_ISNAN(stdDeviationX) ||
                   SH_ISNAN(stdDeviationY) ||
                   stdDeviationX <= 0.0f ||
                   stdDeviationY <= 0.0f ||
                   stdDeviationX > SH_MAX_GAUSSIAN_STD_DEVIATION ||
                   stdDeviationY > SH_MAX_GAUSSIAN_STD_DEVIATION,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src;
  d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(shImageFilterImagesOverlap(d, s),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  radiusX = (SHint)SH_CEIL(stdDeviationX * 3.0f);
  radiusY = (SHint)SH_CEIL(stdDeviationY * 3.0f);
  sizeX = radiusX * 2 + 1;
  sizeY = radiusY * 2 + 1;

  kernelX = (SHfloat*)malloc((size_t)sizeX * sizeof(SHfloat));
  kernelY = (SHfloat*)malloc((size_t)sizeY * sizeof(SHfloat));
  if (!kernelX || !kernelY)
    goto gaussian_out_of_memory;

  {
    SHfloat sumX = 0.0f;
    SHfloat sumY = 0.0f;
    SHfloat factorX = -1.0f / (2.0f * stdDeviationX * stdDeviationX);
    SHfloat factorY = -1.0f / (2.0f * stdDeviationY * stdDeviationY);

    for (k=0; k<sizeX; ++k) {
      SHfloat distance = (SHfloat)(k - radiusX);
      kernelX[k] = (SHfloat)expf(distance * distance * factorX);
      sumX += kernelX[k];
    }
    for (k=0; k<sizeX; ++k)
      kernelX[k] /= sumX;

    for (k=0; k<sizeY; ++k) {
      SHfloat distance = (SHfloat)(k - radiusY);
      kernelY[k] = (SHfloat)expf(distance * distance * factorY);
      sumY += kernelY[k];
    }
    for (k=0; k<sizeY; ++k)
      kernelY[k] /= sumY;
  }

  if (!shImageFilterCreateFloatKernelTexture(kernelX,
                                             sizeX,
                                             &kernelXTexture) ||
      !shImageFilterCreateFloatKernelTexture(kernelY,
                                             sizeY,
                                             &kernelYTexture) ||
      !shImageFilterEnsureScratch(context, s->width, s->height))
    goto gaussian_out_of_memory;

  shImageFilterInitPass(context, s, NULL, &pass);
  pass.mode = SH_IMAGE_FILTER_GAUSSIAN_X;
  pass.kernelWidth = sizeX;
  pass.kernelHeight = 1;
  pass.shiftX = radiusX;
  pass.tilingMode = tilingMode;
  pass.dstStorageMode = SH_IMAGE_FILTER_STORE_FLOAT;

  if (!shImageFilterRunPass(context, NULL, context->filterScratchTexture,
                            s->width, s->height,
                            s->texture, kernelXTexture,
                            VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA,
                            &pass))
    goto gaussian_out_of_memory;

  width = SH_MIN(d->width, s->width);
  height = SH_MIN(d->height, s->height);
  shImageFilterInitPass(context, s, d, &pass);
  pass.mode = SH_IMAGE_FILTER_GAUSSIAN_Y;
  pass.sourceWidth = s->width;
  pass.sourceHeight = s->height;
  pass.kernelWidth = 1;
  pass.kernelHeight = sizeY;
  pass.shiftY = radiusY;
  pass.tilingMode = tilingMode;
  pass.premultiplyInput = VG_FALSE;

  if (!shImageFilterRunPass(context, d, d->texture,
                            width, height,
                            context->filterScratchTexture,
                            kernelYTexture,
                            context->filterChannelMask,
                            &pass))
    goto gaussian_out_of_memory;

  glDeleteTextures(1, &kernelYTexture);
  glDeleteTextures(1, &kernelXTexture);
  free(kernelY);
  free(kernelX);
  d->gpuDataDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);

gaussian_out_of_memory:
  if (kernelYTexture != 0)
    glDeleteTextures(1, &kernelYTexture);
  if (kernelXTexture != 0)
    glDeleteTextures(1, &kernelXTexture);
  free(kernelY);
  free(kernelX);
  VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
}

VG_API_CALL void vgLookup(VGImage dst, VGImage src,
                          const VGubyte * redLUT,
                          const VGubyte * greenLUT,
                          const VGubyte * blueLUT,
                          const VGubyte * alphaLUT,
                          VGboolean outputLinear,
                          VGboolean outputPremultiplied)
{
  SHImage *s, *d;
  SHint width, height;
  GLuint lookupTexture = 0;
  SHImageFilterPass pass;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!redLUT ||
                   !greenLUT ||
                   !blueLUT ||
                   !alphaLUT,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src;
  d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(shImageFilterImagesOverlap(d, s),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shImageFilterCreateLookupTexture(redLUT,
                                                     greenLUT,
                                                     blueLUT,
                                                     alphaLUT,
                                                     &lookupTexture),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  width = SH_MIN(d->width, s->width);
  height = SH_MIN(d->height, s->height);
  shImageFilterInitPass(context, s, d, &pass);
  pass.mode = SH_IMAGE_FILTER_LOOKUP;
  pass.outputLinear = outputLinear ? VG_TRUE : VG_FALSE;
  pass.unpremultiplyOutput = outputPremultiplied ? VG_TRUE : VG_FALSE;

  if (!shImageFilterRunPass(context, d, d->texture,
                            width, height,
                            s->texture, lookupTexture,
                            context->filterChannelMask,
                            &pass)) {
    glDeleteTextures(1, &lookupTexture);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  }

  glDeleteTextures(1, &lookupTexture);
  d->gpuDataDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgLookupSingle(VGImage dst, VGImage src,
                                const VGuint * lookupTable,
                                VGImageChannel sourceChannel,
                                VGboolean outputLinear,
                                VGboolean outputPremultiplied)
{
  SHImage *s, *d;
  SHint width, height;
  GLuint lookupTexture = 0;
  SHImageFilterPass pass;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidImage(context, src) ||
                   !shIsValidImage(context, dst),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!lookupTable ||
                   !shImageFilterAligned(lookupTable, sizeof(VGuint)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(sourceChannel != VG_RED &&
                   sourceChannel != VG_GREEN &&
                   sourceChannel != VG_BLUE &&
                   sourceChannel != VG_ALPHA,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  s = (SHImage*)src;
  d = (SHImage*)dst;
  VG_RETURN_ERR_IF(shImageIsRenderTarget(s) ||
                   shImageIsRenderTarget(d),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(shImageFilterImagesOverlap(d, s),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shImageFilterCreateSingleLookupTexture(lookupTable,
                                                           &lookupTexture),
                   VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (shImageFilterIsLuminanceFormat(s->fd.vgformat))
    sourceChannel = VG_RED;
  else if (((SHuint32)s->fd.vgformat & 0x1Fu) == VG_A_8)
    sourceChannel = VG_ALPHA;

  width = SH_MIN(d->width, s->width);
  height = SH_MIN(d->height, s->height);

  shImageFilterInitPass(context, s, d, &pass);
  pass.mode = SH_IMAGE_FILTER_LOOKUP_SINGLE;
  pass.outputLinear = outputLinear ? VG_TRUE : VG_FALSE;
  pass.unpremultiplyOutput = outputPremultiplied ? VG_TRUE : VG_FALSE;
  switch (sourceChannel) {
  case VG_GREEN:
    pass.lookupSourceChannel = 1;
    break;
  case VG_BLUE:
    pass.lookupSourceChannel = 2;
    break;
  case VG_ALPHA:
    pass.lookupSourceChannel = 3;
    break;
  case VG_RED:
  default:
    pass.lookupSourceChannel = 0;
    break;
  }

  if (!shImageFilterRunPass(context, d, d->texture,
                            width, height,
                            s->texture, lookupTexture,
                            context->filterChannelMask,
                            &pass)) {
    glDeleteTextures(1, &lookupTexture);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  }

  glDeleteTextures(1, &lookupTexture);
  d->gpuDataDirty = VG_TRUE;
  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgBindImageSH(VGImage image, VGImageUnitSH unit){

  VG_GETCONTEXT(VG_NO_RETVAL);
  SH_RETURN_ERR_IF(unit < VG_IMAGE_UNIT_OFFSET_SH, VG_ILLEGAL_ARGUMENT_ERROR, SH_NO_RETVAL);
  SH_RETURN_ERR_IF(image == VG_INVALID_HANDLE,     VG_ILLEGAL_ARGUMENT_ERROR, SH_NO_RETVAL);
  SH_RETURN_ERR_IF(!shIsValidImage(context, image), VG_BAD_HANDLE_ERROR, SH_NO_RETVAL);
  SHImage *i = (SHImage*)image;
  SH_RETURN_ERR_IF(shImageIsRenderTarget(i), VG_IMAGE_IN_USE_ERROR, SH_NO_RETVAL);
  
  glActiveTexture(GL_TEXTURE0 + unit);

  glBindTexture(GL_TEXTURE_2D, i->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  glEnable(GL_TEXTURE_2D);
  GL_CHECK_ERROR;
}
