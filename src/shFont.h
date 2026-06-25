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

#ifndef __SHFONT_H
#define __SHFONT_H

#include "shPath.h"
#include "shImage.h"
#include "shThread.h"

typedef enum
{
  SH_GLYPH_EMPTY = 0,
  SH_GLYPH_PATH,
  SH_GLYPH_IMAGE
} SHGlyphType;

typedef struct
{
  VGuint glyphIndex;
  SHGlyphType type;
  SHPath *path;
  SHImage *image;
  VGboolean isHinted;
  SHVector2 origin;
  SHVector2 escapement;
} SHGlyph;

#define _ITEM_T SHGlyph
#define _ARRAY_T SHGlyphArray
#define _FUNC_T shGlyphArray
#define _ARRAY_DECLARE
#include "shArrayBase.h"

typedef struct
{
  VGHandle handle;
  SHRecursiveMutex mutex;
  SHAtomicInt refCount;
  SHint glyphCapacityHint;
  SHGlyphArray glyphs;
} SHFont;

typedef struct
{
  SHFont *font;
  VGboolean locked;
  VGboolean retained;
} SHFontAccess;

void SHFont_ctor(SHFont *f);
void SHFont_dtor(SHFont *f);
void shFontLock(SHFont *f);
void shFontUnlock(SHFont *f);
void shFontAddRef(SHFont *f);
void shFontRelease(SHFont *f);
void shFontAccessInit(SHFontAccess *access);
void shFontAccessCleanup(SHFontAccess *access);
SHGlyph* shFontFindGlyph(SHFont *f, VGuint glyphIndex);
SHGlyph* shFontEnsureGlyph(SHFont *f, VGuint glyphIndex);
void shFontReleaseGlyph(SHGlyph *glyph);

#define _ITEM_T SHFont*
#define _ARRAY_T SHFontArray
#define _FUNC_T shFontArray
#define _ARRAY_DECLARE
#include "shArrayBase.h"

#endif /* __SHFONT_H */
