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
#include "shFont.h"
#include "shPipeline.h"

#define _ITEM_T SHGlyph
#define _ARRAY_T SHGlyphArray
#define _FUNC_T shGlyphArray
#define _COMPARE_T(x,y) ((x).glyphIndex == (y).glyphIndex)
#define _ARRAY_DEFINE
#include "shArrayBase.h"

#define _ITEM_T SHFont*
#define _ARRAY_T SHFontArray
#define _FUNC_T shFontArray
#define _ARRAY_DEFINE
#include "shArrayBase.h"

static SHint shFontFindGlyphIndex(SHFont *f, VGuint glyphIndex)
{
  SHint i;

  for (i=0; i<f->glyphs.size; ++i) {
    if (f->glyphs.items[i].glyphIndex == glyphIndex)
      return i;
  }

  return -1;
}

void shFontReleaseGlyph(SHGlyph *glyph)
{
  if (!glyph)
    return;

  if (glyph->type == SH_GLYPH_PATH)
    shPathRelease(glyph->path);
  else if (glyph->type == SH_GLYPH_IMAGE) {
    shImageReleaseGlyphRef(glyph->image);
    shImageRelease(glyph->image);
  }

  glyph->type = SH_GLYPH_EMPTY;
  glyph->path = NULL;
  glyph->image = NULL;
}

void SHFont_ctor(SHFont *f)
{
  f->glyphCapacityHint = 0;
  SH_INITOBJ(SHGlyphArray, f->glyphs);
}

void SHFont_dtor(SHFont *f)
{
  SHint i;

  for (i=0; i<f->glyphs.size; ++i)
    shFontReleaseGlyph(&f->glyphs.items[i]);

  SH_DEINITOBJ(SHGlyphArray, f->glyphs);
}

SHGlyph* shFontFindGlyph(SHFont *f, VGuint glyphIndex)
{
  SHint index = shFontFindGlyphIndex(f, glyphIndex);
  return (index < 0) ? NULL : &f->glyphs.items[index];
}

SHGlyph* shFontEnsureGlyph(SHFont *f, VGuint glyphIndex)
{
  SHGlyph glyph;
  SHint index = shFontFindGlyphIndex(f, glyphIndex);

  if (index >= 0)
    return &f->glyphs.items[index];

  glyph.glyphIndex = glyphIndex;
  glyph.type = SH_GLYPH_EMPTY;
  glyph.path = NULL;
  glyph.image = NULL;
  glyph.isHinted = VG_FALSE;
  SET2(glyph.origin, 0.0f, 0.0f);
  SET2(glyph.escapement, 0.0f, 0.0f);

  if (!shGlyphArrayPushBackP(&f->glyphs, &glyph))
    return NULL;

  return &f->glyphs.items[f->glyphs.size - 1];
}

VG_API_CALL VGFont vgCreateFont(VGint glyphCapacityHint)
{
  SHFont *f = NULL;
  VG_GETCONTEXT(VG_INVALID_HANDLE);

  VG_RETURN_ERR_IF(glyphCapacityHint < 0,
                   VG_ILLEGAL_ARGUMENT_ERROR,
                   VG_INVALID_HANDLE);

  SH_NEWOBJ(SHFont, f);
  VG_RETURN_ERR_IF(!f, VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);

  f->glyphCapacityHint = glyphCapacityHint;

  if (!shFontArrayPushBack(&context->resources->fonts, f)) {
    SH_DELETEOBJ(SHFont, f);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  }

  VG_RETURN((VGFont)f);
}

VG_API_CALL void vgDestroyFont(VGFont font)
{
  SHint index;
  VG_GETCONTEXT(VG_NO_RETVAL);

  index = shFontArrayFind(&context->resources->fonts, (SHFont*)font);
  VG_RETURN_ERR_IF(index == -1, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  SH_DELETEOBJ(SHFont, (SHFont*)font);
  shFontArrayRemoveAt(&context->resources->fonts, index);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgSetGlyphToPath(VGFont font,
                                  VGuint glyphIndex,
                                  VGPath path,
                                  VGboolean isHinted,
                                  const VGfloat glyphOrigin[2],
                                  const VGfloat escapement[2])
{
  SHFont *f;
  SHGlyph *glyph;
  SHPath *newPath = NULL;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidFont(context, font),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!glyphOrigin || !escapement,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(path != VG_INVALID_HANDLE &&
                   !shIsValidPath(context, path),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  f = (SHFont*)font;
  glyph = shFontEnsureGlyph(f, glyphIndex);
  VG_RETURN_ERR_IF(!glyph, VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (path != VG_INVALID_HANDLE) {
    newPath = (SHPath*)path;
    shPathAddRef(newPath);
  }

  shFontReleaseGlyph(glyph);
  glyph->glyphIndex = glyphIndex;
  glyph->type = newPath ? SH_GLYPH_PATH : SH_GLYPH_EMPTY;
  glyph->path = newPath;
  glyph->image = NULL;
  glyph->isHinted = isHinted ? VG_TRUE : VG_FALSE;
  SET2(glyph->origin, glyphOrigin[0], glyphOrigin[1]);
  SET2(glyph->escapement, escapement[0], escapement[1]);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgSetGlyphToImage(VGFont font,
                                   VGuint glyphIndex,
                                   VGImage image,
                                   const VGfloat glyphOrigin[2],
                                   const VGfloat escapement[2])
{
  SHFont *f;
  SHGlyph *glyph;
  SHImage *newImage = NULL;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidFont(context, font),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!glyphOrigin || !escapement,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(image != VG_INVALID_HANDLE &&
                   !shIsValidImage(context, image),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(image != VG_INVALID_HANDLE &&
                   shImageIsRenderTarget((SHImage*)image),
                   VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);

  f = (SHFont*)font;
  glyph = shFontEnsureGlyph(f, glyphIndex);
  VG_RETURN_ERR_IF(!glyph, VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (image != VG_INVALID_HANDLE) {
    newImage = (SHImage*)image;
    shImageAddRef(newImage);
    shImageAddGlyphRef(newImage);
  }

  shFontReleaseGlyph(glyph);
  glyph->glyphIndex = glyphIndex;
  glyph->type = newImage ? SH_GLYPH_IMAGE : SH_GLYPH_EMPTY;
  glyph->path = NULL;
  glyph->image = newImage;
  glyph->isHinted = VG_FALSE;
  SET2(glyph->origin, glyphOrigin[0], glyphOrigin[1]);
  SET2(glyph->escapement, escapement[0], escapement[1]);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgClearGlyph(VGFont font, VGuint glyphIndex)
{
  SHFont *f;
  SHint index;
  VG_GETCONTEXT(VG_NO_RETVAL);

  VG_RETURN_ERR_IF(!shIsValidFont(context, font),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  f = (SHFont*)font;
  index = shFontFindGlyphIndex(f, glyphIndex);
  VG_RETURN_ERR_IF(index == -1, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  shFontReleaseGlyph(&f->glyphs.items[index]);
  shGlyphArrayRemoveAt(&f->glyphs, index);

  VG_RETURN(VG_NO_RETVAL);
}

static void shDrawGlyphObject(VGContext *context, SHGlyph *glyph,
                              VGbitfield paintModes)
{
  SHMatrix3x3 saved;

  if (paintModes == 0 || glyph->type == SH_GLYPH_EMPTY)
    return;

  if (glyph->type == SH_GLYPH_PATH) {
    SETMATMAT(saved, context->pathTransform);
    SETMATMAT(context->pathTransform, context->glyphTransform);
    TRANSLATEMATR(context->pathTransform,
                  context->glyphOrigin.x - glyph->origin.x,
                  context->glyphOrigin.y - glyph->origin.y);
    shDrawPath(context, glyph->path, paintModes);
    SETMATMAT(context->pathTransform, saved);
  } else if (glyph->type == SH_GLYPH_IMAGE) {
    SETMATMAT(saved, context->imageTransform);
    SETMATMAT(context->imageTransform, context->glyphTransform);
    TRANSLATEMATR(context->imageTransform,
                  context->glyphOrigin.x - glyph->origin.x,
                  context->glyphOrigin.y - glyph->origin.y);
    shDrawImage(context, glyph->image);
    SETMATMAT(context->imageTransform, saved);
  }
}

VG_API_CALL void vgDrawGlyph(VGFont font,
                             VGuint glyphIndex,
                             VGbitfield paintModes,
                             VGboolean allowAutoHinting)
{
  SHGlyph *glyph;
  VG_GETCONTEXT(VG_NO_RETVAL);

  (void)allowAutoHinting;

  VG_RETURN_ERR_IF(!shIsValidFont(context, font),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  glyph = shFontFindGlyph((SHFont*)font, glyphIndex);
  VG_RETURN_ERR_IF(!glyph, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  shDrawGlyphObject(context, glyph, paintModes);
  ADD2(context->glyphOrigin, glyph->escapement.x, glyph->escapement.y);

  VG_RETURN(VG_NO_RETVAL);
}

VG_API_CALL void vgDrawGlyphs(VGFont font,
                              VGint glyphCount,
                              const VGuint *glyphIndices,
                              const VGfloat *adjustments_x,
                              const VGfloat *adjustments_y,
                              VGbitfield paintModes,
                              VGboolean allowAutoHinting)
{
  SHFont *f;
  SHGlyph *glyph;
  VGint i;
  VG_GETCONTEXT(VG_NO_RETVAL);

  (void)allowAutoHinting;

  VG_RETURN_ERR_IF(!shIsValidFont(context, font),
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(glyphCount <= 0 || !glyphIndices,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  f = (SHFont*)font;
  for (i=0; i<glyphCount; ++i) {
    VG_RETURN_ERR_IF(!shFontFindGlyph(f, glyphIndices[i]),
                     VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  }

  for (i=0; i<glyphCount; ++i) {
    glyph = shFontFindGlyph(f, glyphIndices[i]);
    shDrawGlyphObject(context, glyph, paintModes);
    context->glyphOrigin.x += glyph->escapement.x;
    context->glyphOrigin.y += glyph->escapement.y;
    if (adjustments_x)
      context->glyphOrigin.x += adjustments_x[i];
    if (adjustments_y)
      context->glyphOrigin.y += adjustments_y[i];
  }

  VG_RETURN(VG_NO_RETVAL);
}
