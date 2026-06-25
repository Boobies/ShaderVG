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
    SHImageLockSet imageLocks;
    shImageLockSetInit(&imageLocks);
    shImageLockSetAddImage(&imageLocks, glyph->image);
    shImageLockSetLock(&imageLocks);
    shImageReleaseGlyphRef(glyph->image);
    shImageLockSetCleanup(&imageLocks);
    shImageRelease(glyph->image);
  }

  glyph->type = SH_GLYPH_EMPTY;
  glyph->path = NULL;
  glyph->image = NULL;
}

void SHFont_ctor(SHFont *f)
{
  f->handle = VG_INVALID_HANDLE;
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

static int shComparePathPointers(const void *a, const void *b)
{
  uintptr_t pathA = (uintptr_t)*(SHPath* const*)a;
  uintptr_t pathB = (uintptr_t)*(SHPath* const*)b;

  if (pathA < pathB)
    return -1;
  if (pathA > pathB)
    return 1;
  return 0;
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

  if (shRegisterResource(context, SH_RESOURCE_FONT, f) == VG_INVALID_HANDLE) {
    shFontArrayRemoveAt(&context->resources->fonts,
                        context->resources->fonts.size - 1);
    SH_DELETEOBJ(SHFont, f);
    VG_RETURN_ERR(VG_OUT_OF_MEMORY_ERROR, VG_INVALID_HANDLE);
  }

  VG_RETURN((VGFont)f->handle);
}

VG_API_CALL void vgDestroyFont(VGFont font)
{
  SHint index;
  SHFont *f;
  VG_GETCONTEXT(VG_NO_RETVAL);

  f = shGetFont(context, font);
  index = f ? shFontArrayFind(&context->resources->fonts, f) : -1;
  VG_RETURN_ERR_IF(index == -1, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  shUnregisterResource(context, font, SH_RESOURCE_FONT, f);
  SH_DELETEOBJ(SHFont, f);
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

  f = shGetFont(context, font);
  VG_RETURN_ERR_IF(!f,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!glyphOrigin || !escapement,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  if (path != VG_INVALID_HANDLE) {
    newPath = shGetPath(context, path);
    VG_RETURN_ERR_IF(!newPath, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  }
  VG_RETURN_ERR_IF(!shIsAligned(glyphOrigin, sizeof(VGfloat)) ||
                   !shIsAligned(escapement, sizeof(VGfloat)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  glyph = shFontEnsureGlyph(f, glyphIndex);
  VG_RETURN_ERR_IF(!glyph, VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (path != VG_INVALID_HANDLE) {
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
  SHImageLockSet imageLocks;
  VG_GETCONTEXT(VG_NO_RETVAL);

  f = shGetFont(context, font);
  VG_RETURN_ERR_IF(!f,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!glyphOrigin || !escapement,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  if (image != VG_INVALID_HANDLE) {
    newImage = shGetImage(context, image);
    VG_RETURN_ERR_IF(!newImage, VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
    shImageLockSetInit(&imageLocks);
    shImageLockSetAddImage(&imageLocks, newImage);
    shImageLockSetLock(&imageLocks);
    if (shImageIsRenderTarget(newImage)) {
      shImageLockSetCleanup(&imageLocks);
      VG_RETURN_ERR(VG_IMAGE_IN_USE_ERROR, VG_NO_RETVAL);
    }
    shImageLockSetCleanup(&imageLocks);
  }
  VG_RETURN_ERR_IF(!shIsAligned(glyphOrigin, sizeof(VGfloat)) ||
                   !shIsAligned(escapement, sizeof(VGfloat)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  glyph = shFontEnsureGlyph(f, glyphIndex);
  VG_RETURN_ERR_IF(!glyph, VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);

  if (image != VG_INVALID_HANDLE) {
    shImageLockSetInit(&imageLocks);
    shImageLockSetAddImage(&imageLocks, newImage);
    shImageLockSetLock(&imageLocks);
    shImageAddRef(newImage);
    shImageAddGlyphRef(newImage);
    shImageLockSetCleanup(&imageLocks);
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

  f = shGetFont(context, font);
  VG_RETURN_ERR_IF(!f,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);

  index = shFontFindGlyphIndex(f, glyphIndex);
  VG_RETURN_ERR_IF(index == -1, VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  shFontReleaseGlyph(&f->glyphs.items[index]);
  shGlyphArrayRemoveAt(&f->glyphs, index);

  VG_RETURN(VG_NO_RETVAL);
}

static void shDrawGlyphObject(VGContext *context, SHGlyph *glyph,
                              VGbitfield paintModes)
{
  SHPaintLockSet paintLocks;
  SHImageLockSet imageLocks;
  SHMatrix3x3 saved;

  if (paintModes == 0 || glyph->type == SH_GLYPH_EMPTY)
    return;

  if (glyph->type == SH_GLYPH_PATH) {
    shPathLock(glyph->path);
    shPaintLockSelected(context, paintModes, &paintLocks);
    SETMATMAT(saved, context->pathTransform);
    SETMATMAT(context->pathTransform, context->glyphTransform);
    TRANSLATEMATR(context->pathTransform,
                  context->glyphOrigin.x - glyph->origin.x,
                  context->glyphOrigin.y - glyph->origin.y);
    shDrawPath(context, glyph->path, paintModes);
    SETMATMAT(context->pathTransform, saved);
    shPaintLockSetCleanup(&paintLocks);
    shPathUnlock(glyph->path);
  } else if (glyph->type == SH_GLYPH_IMAGE) {
    shImageLockSetInit(&imageLocks);
    shImageLockSetAddImage(&imageLocks, glyph->image);
    shImageLockSetLock(&imageLocks);
    shPaintLockSelected(context, VG_FILL_PATH, &paintLocks);
    SETMATMAT(saved, context->imageTransform);
    SETMATMAT(context->imageTransform, context->glyphTransform);
    TRANSLATEMATR(context->imageTransform,
                  context->glyphOrigin.x - glyph->origin.x,
                  context->glyphOrigin.y - glyph->origin.y);
    shDrawImage(context, glyph->image);
    SETMATMAT(context->imageTransform, saved);
    shPaintLockSetCleanup(&paintLocks);
    shImageLockSetCleanup(&imageLocks);
  }
}

static VGboolean shCanBatchImageGlyphs(VGContext *context)
{
  SHPaintLockSet paintLocks;
  SHPaint *fill = (context->fillPaint ?
                   context->fillPaint : &context->defaultPaint);
  VGboolean canBatch;

  /* Gradient and pattern image-multiply use image-local paint coordinates. */
  shPaintLockSelected(context, VG_FILL_PATH, &paintLocks);
  canBatch = (context->imageMode != VG_DRAW_IMAGE_MULTIPLY ||
              fill->type == VG_PAINT_TYPE_COLOR) ? VG_TRUE : VG_FALSE;
  shPaintLockSetCleanup(&paintLocks);
  return canBatch;
}

static SHImage* shImageGlyphBatchRoot(SHGlyph *glyph)
{
  SHImageLockSet imageLocks;
  SHImage *root;

  if (!glyph ||
      glyph->type != SH_GLYPH_IMAGE ||
      !glyph->image)
    return NULL;

  shImageLockSetInit(&imageLocks);
  shImageLockSetAddImage(&imageLocks, glyph->image);
  shImageLockSetLock(&imageLocks);

  if (shImageIsRenderTarget(glyph->image)) {
    shImageLockSetCleanup(&imageLocks);
    return NULL;
  }

  root = shImageRoot(glyph->image);
  if (!root ||
      root->texture == 0 ||
      root->texwidth <= 0 ||
      root->texheight <= 0 ||
      glyph->image->width <= 0 ||
      glyph->image->height <= 0) {
    shImageLockSetCleanup(&imageLocks);
    return NULL;
  }

  shImageLockSetCleanup(&imageLocks);
  return root;
}

static void shSetImageQuadVertex(SHImageQuadVertex *vertex,
                                 SHVector2 position,
                                 GLfloat u,
                                 GLfloat v)
{
  vertex->x = position.x;
  vertex->y = position.y;
  vertex->u = u;
  vertex->v = v;
}

static void shBuildImageGlyphQuad(VGContext *context,
                                  SHGlyph *glyph,
                                  SHImage *root,
                                  SHImageQuad *quad)
{
  SHImage *image = glyph->image;
  SHImageLockSet imageLocks;
  SHMatrix3x3 transform;
  SHVector2 p0;
  SHVector2 p1;
  SHVector2 p2;
  SHVector2 p3;
  GLfloat u0;
  GLfloat u1;
  GLfloat v0;
  GLfloat v1;

  shImageLockSetInit(&imageLocks);
  shImageLockSetAddImage(&imageLocks, image);
  shImageLockSetLock(&imageLocks);

  SETMATMAT(transform, context->glyphTransform);
  TRANSLATEMATR(transform,
                context->glyphOrigin.x - glyph->origin.x,
                context->glyphOrigin.y - glyph->origin.y);

  SET2(p0, 0.0f, 0.0f);
  SET2(p1, (SHfloat)image->width, 0.0f);
  SET2(p2, 0.0f, (SHfloat)image->height);
  SET2(p3, (SHfloat)image->width, (SHfloat)image->height);
  TRANSFORM2(p0, transform);
  TRANSFORM2(p1, transform);
  TRANSFORM2(p2, transform);
  TRANSFORM2(p3, transform);

  u0 = (GLfloat)image->storageX / (GLfloat)root->texwidth;
  u1 = (GLfloat)(image->storageX + image->width) /
       (GLfloat)root->texwidth;
  v0 = (GLfloat)image->storageY / (GLfloat)root->texheight;
  v1 = (GLfloat)(image->storageY + image->height) /
       (GLfloat)root->texheight;

  shSetImageQuadVertex(&quad->vertices[0], p0, u0, v0);
  shSetImageQuadVertex(&quad->vertices[1], p1, u1, v0);
  shSetImageQuadVertex(&quad->vertices[2], p2, u0, v1);
  shSetImageQuadVertex(&quad->vertices[3], p2, u0, v1);
  shSetImageQuadVertex(&quad->vertices[4], p1, u1, v0);
  shSetImageQuadVertex(&quad->vertices[5], p3, u1, v1);
  shImageLockSetCleanup(&imageLocks);
}

static void shFlushImageGlyphBatch(VGContext *context,
                                   SHImage **batchRoot,
                                   SHImageQuad *batchQuads,
                                   SHint *batchCount)
{
  if (*batchCount > 0 && *batchRoot) {
    SHPaintLockSet paintLocks;
    SHImageLockSet imageLocks;
    shImageLockSetInit(&imageLocks);
    shImageLockSetAddImage(&imageLocks, *batchRoot);
    shImageLockSetLock(&imageLocks);
    shPaintLockSelected(context, VG_FILL_PATH, &paintLocks);
    shDrawImageQuadBatch(context, (*batchRoot)->texture,
                         batchQuads, *batchCount);
    shPaintLockSetCleanup(&paintLocks);
    shImageLockSetCleanup(&imageLocks);
  }

  *batchRoot = NULL;
  *batchCount = 0;
}

static VGboolean shCanBuildPathGlyphBatch(SHFont *font,
                                          const VGuint *glyphIndices,
                                          VGint glyphCount)
{
  SHGlyph *glyph;
  VGboolean hasPathGlyph = VG_FALSE;
  VGint i;

  for (i=0; i<glyphCount; ++i) {
    glyph = shFontFindGlyph(font, glyphIndices[i]);
    if (glyph->type == SH_GLYPH_PATH) {
      hasPathGlyph = VG_TRUE;
    } else if (glyph->type != SH_GLYPH_EMPTY) {
      return VG_FALSE;
    }
  }

  return hasPathGlyph;
}

static SHPathGlyphBatchResult shTryDrawPathGlyphBatch(
    VGContext *context,
    SHFont *font,
    VGint glyphCount,
    const VGuint *glyphIndices,
    const VGfloat *adjustments_x,
    const VGfloat *adjustments_y)
{
  SHGlyph *glyph;
  SHPathGlyph *batchGlyphs = NULL;
  SHPath **lockedPaths = NULL;
  SHPaintLockSet paintLocks;
  SHVector2 origin;
  SHint batchCount = 0;
  SHint lockedPathCount = 0;
  SHPathGlyphBatchResult result;
  VGint i;

  if ((size_t)glyphCount > ((size_t)-1) / sizeof(SHPathGlyph)) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  batchGlyphs = (SHPathGlyph*)malloc((size_t)glyphCount *
                                     sizeof(SHPathGlyph));
  if (!batchGlyphs) {
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  lockedPaths = (SHPath**)malloc((size_t)glyphCount * sizeof(SHPath*));
  if (!lockedPaths) {
    free(batchGlyphs);
    shSetError(context, VG_OUT_OF_MEMORY_ERROR);
    return SH_PATH_GLYPH_BATCH_ERROR;
  }

  SET2V(origin, context->glyphOrigin);
  for (i=0; i<glyphCount; ++i) {
    glyph = shFontFindGlyph(font, glyphIndices[i]);
    if (glyph->type == SH_GLYPH_PATH) {
      batchGlyphs[batchCount].path = glyph->path;
      lockedPaths[lockedPathCount++] = glyph->path;
      SETMATMAT(batchGlyphs[batchCount].transform,
                context->glyphTransform);
      TRANSLATEMATR(batchGlyphs[batchCount].transform,
                    origin.x - glyph->origin.x,
                    origin.y - glyph->origin.y);
      ++batchCount;
    }

    origin.x += glyph->escapement.x;
    origin.y += glyph->escapement.y;
    if (adjustments_x)
      origin.x += adjustments_x[i];
    if (adjustments_y)
      origin.y += adjustments_y[i];
  }

  qsort(lockedPaths,
        (size_t)lockedPathCount,
        sizeof(SHPath*),
        shComparePathPointers);
  for (i=0; i<lockedPathCount; ++i)
    shPathLock(lockedPaths[i]);

  shPaintLockSelected(context, VG_FILL_PATH, &paintLocks);
  result = shDrawPathGlyphBatch(context, batchGlyphs, batchCount);
  shPaintLockSetCleanup(&paintLocks);

  while (lockedPathCount > 0) {
    --lockedPathCount;
    shPathUnlock(lockedPaths[lockedPathCount]);
  }

  if (result == SH_PATH_GLYPH_BATCH_DRAWN)
    SET2V(context->glyphOrigin, origin);

  free(lockedPaths);
  free(batchGlyphs);
  return result;
}

static void shAdvanceGlyphOrigin(VGContext *context,
                                 SHGlyph *glyph,
                                 const VGfloat *adjustments_x,
                                 const VGfloat *adjustments_y,
                                 VGint index)
{
  context->glyphOrigin.x += glyph->escapement.x;
  context->glyphOrigin.y += glyph->escapement.y;
  if (adjustments_x)
    context->glyphOrigin.x += adjustments_x[index];
  if (adjustments_y)
    context->glyphOrigin.y += adjustments_y[index];
}

VG_API_CALL void vgDrawGlyph(VGFont font,
                             VGuint glyphIndex,
                             VGbitfield paintModes,
                             VGboolean allowAutoHinting)
{
  SHFont *f;
  SHGlyph *glyph;
  VG_GETCONTEXT(VG_NO_RETVAL);

  (void)allowAutoHinting;

  f = shGetFont(context, font);
  VG_RETURN_ERR_IF(!f,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  glyph = shFontFindGlyph(f, glyphIndex);
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
  SHImage *root;
  SHImage *batchRoot = NULL;
  SHImageQuad *batchQuads = NULL;
  SHint batchCount = 0;
  VGboolean canBatchImages;
  VGboolean hasBatchCandidate = VG_FALSE;
  VGint i;
  VG_GETCONTEXT(VG_NO_RETVAL);

  (void)allowAutoHinting;

  f = shGetFont(context, font);
  VG_RETURN_ERR_IF(!f,
                   VG_BAD_HANDLE_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(glyphCount <= 0 || !glyphIndices,
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(paintModes & (~(VG_STROKE_PATH | VG_FILL_PATH)),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  VG_RETURN_ERR_IF(!shIsAligned(glyphIndices, sizeof(VGuint)) ||
                   (adjustments_x &&
                    !shIsAligned(adjustments_x, sizeof(VGfloat))) ||
                   (adjustments_y &&
                    !shIsAligned(adjustments_y, sizeof(VGfloat))),
                   VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);

  for (i=0; i<glyphCount; ++i) {
    VG_RETURN_ERR_IF(!shFontFindGlyph(f, glyphIndices[i]),
                     VG_ILLEGAL_ARGUMENT_ERROR, VG_NO_RETVAL);
  }

  if (paintModes == 0) {
    for (i=0; i<glyphCount; ++i) {
      glyph = shFontFindGlyph(f, glyphIndices[i]);
      shAdvanceGlyphOrigin(context, glyph,
                           adjustments_x, adjustments_y, i);
    }
    VG_RETURN(VG_NO_RETVAL);
  }

  if (paintModes == VG_FILL_PATH &&
      shCanBuildPathGlyphBatch(f, glyphIndices, glyphCount)) {
    SHPathGlyphBatchResult pathBatchResult;
    pathBatchResult = shTryDrawPathGlyphBatch(context, f, glyphCount,
                                              glyphIndices,
                                              adjustments_x,
                                              adjustments_y);
    if (pathBatchResult == SH_PATH_GLYPH_BATCH_DRAWN)
      VG_RETURN(VG_NO_RETVAL);
    if (pathBatchResult == SH_PATH_GLYPH_BATCH_ERROR)
      VG_RETURN(VG_NO_RETVAL);
  }

  canBatchImages = shCanBatchImageGlyphs(context);
  if (canBatchImages) {
    for (i=0; i<glyphCount; ++i) {
      glyph = shFontFindGlyph(f, glyphIndices[i]);
      if (shImageGlyphBatchRoot(glyph)) {
        hasBatchCandidate = VG_TRUE;
        break;
      }
    }
  }

  if (hasBatchCandidate) {
    VG_RETURN_ERR_IF((size_t)glyphCount >
                     ((size_t)-1) / sizeof(SHImageQuad),
                     VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
    batchQuads = (SHImageQuad*)malloc((size_t)glyphCount *
                                      sizeof(SHImageQuad));
    VG_RETURN_ERR_IF(!batchQuads,
                     VG_OUT_OF_MEMORY_ERROR, VG_NO_RETVAL);
  }

  for (i=0; i<glyphCount; ++i) {
    glyph = shFontFindGlyph(f, glyphIndices[i]);
    root = batchQuads ? shImageGlyphBatchRoot(glyph) : NULL;
    if (root) {
      if (batchRoot && batchRoot != root)
        shFlushImageGlyphBatch(context, &batchRoot,
                               batchQuads, &batchCount);
      batchRoot = root;
      shBuildImageGlyphQuad(context, glyph, root,
                            &batchQuads[batchCount]);
      ++batchCount;
    } else {
      shFlushImageGlyphBatch(context, &batchRoot,
                             batchQuads, &batchCount);
      shDrawGlyphObject(context, glyph, paintModes);
    }
    shAdvanceGlyphOrigin(context, glyph,
                         adjustments_x, adjustments_y, i);
  }

  shFlushImageGlyphBatch(context, &batchRoot, batchQuads, &batchCount);
  free(batchQuads);

  VG_RETURN(VG_NO_RETVAL);
}
