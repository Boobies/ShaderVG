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
#include <VG/vgu.h>
#include <VG/vgext.h>
#include "shDefs.h"
#include "shContext.h"
#include <math.h>

#define SH_WARP_EPSILON 0.000001f

static int shIsFiniteWarpValue(VGfloat value)
{
  return isfinite(value);
}

static int shWarpIsNearZero(VGfloat value)
{
  return SH_ABS(value) <= SH_WARP_EPSILON;
}

static VGfloat shWarpGet(const VGfloat *matrix, int row, int column)
{
  return matrix[column * 3 + row];
}

static void shWarpSet(VGfloat *matrix,
                      VGfloat m00, VGfloat m01, VGfloat m02,
                      VGfloat m10, VGfloat m11, VGfloat m12,
                      VGfloat m20, VGfloat m21, VGfloat m22)
{
  matrix[0] = m00; matrix[3] = m01; matrix[6] = m02;
  matrix[1] = m10; matrix[4] = m11; matrix[7] = m12;
  matrix[2] = m20; matrix[5] = m21; matrix[8] = m22;
}

static void shWarpCopy(VGfloat *dst, const VGfloat *src)
{
  int i;
  for (i=0; i<9; ++i)
    dst[i] = src[i];
}

static int shWarpIsFinite(const VGfloat *matrix)
{
  int i;
  for (i=0; i<9; ++i) {
    if (!shIsFiniteWarpValue(matrix[i]))
      return 0;
  }

  return 1;
}

static VGfloat shWarpDeterminant(const VGfloat *matrix)
{
  VGfloat a = matrix[0], b = matrix[3], c = matrix[6];
  VGfloat d = matrix[1], e = matrix[4], f = matrix[7];
  VGfloat g = matrix[2], h = matrix[5], i = matrix[8];

  return a * (e * i - f * h) -
         b * (d * i - f * g) +
         c * (d * h - e * g);
}

static int shWarpIsUsable(const VGfloat *matrix)
{
  return shWarpIsFinite(matrix) &&
         !shWarpIsNearZero(shWarpDeterminant(matrix));
}

static int shWarpInvert(const VGfloat *matrix, VGfloat *inverse)
{
  VGfloat a = matrix[0], b = matrix[3], c = matrix[6];
  VGfloat d = matrix[1], e = matrix[4], f = matrix[7];
  VGfloat g = matrix[2], h = matrix[5], i = matrix[8];
  VGfloat det = shWarpDeterminant(matrix);
  VGfloat invdet;

  if (shWarpIsNearZero(det))
    return 0;

  invdet = 1.0f / det;
  shWarpSet(inverse,
            (e * i - f * h) * invdet,
            (c * h - b * i) * invdet,
            (b * f - c * e) * invdet,
            (f * g - d * i) * invdet,
            (a * i - c * g) * invdet,
            (c * d - a * f) * invdet,
            (d * h - e * g) * invdet,
            (b * g - a * h) * invdet,
            (a * e - b * d) * invdet);

  return shWarpIsUsable(inverse);
}

static int shWarpMultiply(const VGfloat *left, const VGfloat *right,
                          VGfloat *product)
{
  int row, column;

  for (row=0; row<3; ++row) {
    for (column=0; column<3; ++column) {
      product[column * 3 + row] =
        shWarpGet(left, row, 0) * shWarpGet(right, 0, column) +
        shWarpGet(left, row, 1) * shWarpGet(right, 1, column) +
        shWarpGet(left, row, 2) * shWarpGet(right, 2, column);
    }
  }

  return shWarpIsUsable(product);
}

static int shComputeWarpSquareToQuad(VGfloat dx0, VGfloat dy0,
                                     VGfloat dx1, VGfloat dy1,
                                     VGfloat dx2, VGfloat dy2,
                                     VGfloat dx3, VGfloat dy3,
                                     VGfloat *matrix)
{
  VGfloat a = dx1 - dx3;
  VGfloat b = dx2 - dx3;
  VGfloat c = dx3 - dx1 - dx2 + dx0;
  VGfloat d = dy1 - dy3;
  VGfloat e = dy2 - dy3;
  VGfloat f = dy3 - dy1 - dy2 + dy0;
  VGfloat det = a * e - b * d;
  VGfloat w0, w1;

  if (shWarpIsNearZero(det))
    return 0;

  w0 = (c * e - b * f) / det;
  w1 = (a * f - c * d) / det;

  if (shWarpIsNearZero(1.0f + w0) ||
      shWarpIsNearZero(1.0f + w1) ||
      shWarpIsNearZero(1.0f + w0 + w1))
    return 0;

  shWarpSet(matrix,
            dx1 * (w0 + 1.0f) - dx0,
            dx2 * (w1 + 1.0f) - dx0,
            dx0,
            dy1 * (w0 + 1.0f) - dy0,
            dy2 * (w1 + 1.0f) - dy0,
            dy0,
            w0,
            w1,
            1.0f);

  return shWarpIsUsable(matrix);
}

static VGUErrorCode shAppend(VGPath path, SHint commSize, const VGubyte *comm,
                             SHint dataSize, const VGfloat *data)
{
  VGErrorCode err = VG_NO_ERROR;
  VGPathDatatype type = vgGetParameterf(path, VG_PATH_DATATYPE);
  VGfloat scale = vgGetParameterf(path, VG_PATH_SCALE);
  VGfloat bias = vgGetParameterf(path, VG_PATH_BIAS);
  SH_ASSERT(dataSize <= 26);
  
  switch(type)
  {
  case VG_PATH_DATATYPE_S_8: {
      
      SHint8 data8[26]; int i;
      for (i=0; i<dataSize; ++i)
        data8[i] = (SHint8)SH_FLOOR((data[i] - bias) / scale + 0.5f);
      vgAppendPathData(path, commSize, comm, data8);
      
      break;}
  case VG_PATH_DATATYPE_S_16: {
      
      SHint16 data16[26]; int i;
      for (i=0; i<dataSize; ++i)
        data16[i] = (SHint16)SH_FLOOR((data[i] - bias) / scale + 0.5f);
      vgAppendPathData(path, commSize, comm, data16);
      
      break;}
  case VG_PATH_DATATYPE_S_32: {
      
      SHint32 data32[26]; int i;
      for (i=0; i<dataSize; ++i)
        data32[i] = (SHint32)SH_FLOOR((data[i] - bias) / scale + 0.5f);
      vgAppendPathData(path, commSize, comm, data32);
      
      break;}
  default: {
      
      VGfloat dataF[26]; int i;
      for (i=0; i<dataSize; ++i)
        dataF[i] = (data[i] - bias) / scale;
      vgAppendPathData(path, commSize, comm, dataF);
      
      break;}
  }
  
  err = vgGetError();
  if (err == VG_PATH_CAPABILITY_ERROR)
    return VGU_PATH_CAPABILITY_ERROR;
  else if (err == VG_BAD_HANDLE_ERROR)
    return VGU_BAD_HANDLE_ERROR;
  else if (err == VG_OUT_OF_MEMORY_ERROR)
    return VGU_OUT_OF_MEMORY_ERROR;
  
  return VGU_NO_ERROR;
}

VGU_API_CALL VGUErrorCode vguLine(VGPath path,
                                  VGfloat x0, VGfloat y0,
                                  VGfloat x1, VGfloat y1)
{
  VGUErrorCode err = VGU_NO_ERROR;
  const VGubyte comm[] = {VG_MOVE_TO_ABS, VG_LINE_TO_ABS};
  
  VGfloat data[4];
  data[0] = x0; data[1] = y0;
  data[2] = x1; data[3] = y1;
  
  err = shAppend(path, 2, comm, 4, data);
  return err;
}

VGU_API_CALL VGUErrorCode vguPolygon(VGPath path,
                                     const VGfloat * points, VGint count,
                                     VGboolean closed)
{
  VGint i;
  VGint commSize;
  VGubyte stackComm[64];
  VGubyte *comm = NULL;
  VGUErrorCode err = VGU_NO_ERROR;
  
  if (points == NULL || count <= 0)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (!shIsAligned(points, sizeof(VGfloat)))
    return VGU_ILLEGAL_ARGUMENT_ERROR;

  if (closed && count == SH_MAX_INT)
    return VGU_OUT_OF_MEMORY_ERROR;

  commSize = count + (closed ? 1 : 0);
  if (commSize <= (VGint)sizeof(stackComm)) {
    comm = stackComm;
  } else {
    comm = (VGubyte*)malloc((size_t)commSize * sizeof(VGubyte));
    if (comm == NULL) return VGU_OUT_OF_MEMORY_ERROR;
  }

  comm[0] = VG_MOVE_TO_ABS;
  for (i=1; i<count; ++i)
    comm[i] = VG_LINE_TO_ABS;
  if (closed)
    comm[count] = VG_CLOSE_PATH;
  
  err = shAppend(path, commSize, comm, count*2, points);
  
  if (comm != stackComm)
    free(comm);
  return err;
}

VGU_API_CALL VGUErrorCode vguRect(VGPath path,
                                  VGfloat x, VGfloat y,
                                  VGfloat width, VGfloat height)
{
  VGUErrorCode err = VGU_NO_ERROR;
  
  VGubyte comm[5] = {
    VG_MOVE_TO_ABS, VG_HLINE_TO_REL,
    VG_VLINE_TO_REL, VG_HLINE_TO_REL,
    VG_CLOSE_PATH };
  
  VGfloat data[5];
  
  if (width <= 0 || height <= 0)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  
  data[0] =  x;     data[1] = y;
  data[2] =  width; data[3] = height;
  data[4] = -width;
  
  err = shAppend(path, 5, comm, 5, data);
  return err;
}

VGU_API_CALL VGUErrorCode vguRoundRect(VGPath path,
                                       VGfloat x, VGfloat y,
                                       VGfloat width, VGfloat height,
                                       VGfloat arcWidth, VGfloat arcHeight)
{
  VGUErrorCode err = VGU_NO_ERROR;
  
  VGubyte comm[10] = {
    VG_MOVE_TO_ABS,
    VG_HLINE_TO_REL, VG_SCCWARC_TO_REL,
    VG_VLINE_TO_REL, VG_SCCWARC_TO_REL,
    VG_HLINE_TO_REL, VG_SCCWARC_TO_REL,
    VG_VLINE_TO_REL, VG_SCCWARC_TO_REL,
    VG_CLOSE_PATH };
  
  VGfloat data[26];
  VGfloat rx, ry;
  
  if (width <= 0 || height <= 0)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  
  SH_CLAMP(arcWidth, 0.0f, width);
  SH_CLAMP(arcHeight, 0.0f, height);
  rx = arcWidth/2;
  ry = arcHeight/2;
  
  data[0]  =  x + rx; data[1] = y;
  
  data[2]  =  width - arcWidth;
  data[3]  =  rx; data[4]  =  ry; data[5]  = 0;
  data[6]  =  rx; data[7]  =  ry;
  
  data[8]  =  height - arcHeight;
  data[9]  =  rx; data[10] =  ry; data[11] = 0;
  data[12] = -rx; data[13] =  ry;
  
  data[14] = -(width - arcWidth);
  data[15] =  rx; data[16] =  ry; data[17] = 0;
  data[18] = -rx; data[19] = -ry;
  
  data[20] = -(height - arcHeight);
  data[21] =  rx; data[22] =  ry; data[23] = 0;
  data[24] =  rx; data[25] = -ry;
  
  err = shAppend(path, 10, comm, 26, data);
  return err;
}

VGU_API_CALL VGUErrorCode vguEllipse(VGPath path,
                                     VGfloat cx, VGfloat cy,
                                     VGfloat width, VGfloat height)
{
  VGUErrorCode err = VGU_NO_ERROR;
  
  const VGubyte comm[] = {
    VG_MOVE_TO_ABS, VG_SCCWARC_TO_REL,
    VG_SCCWARC_TO_REL, VG_CLOSE_PATH};
  
  VGfloat data[12];
  
  if (width <= 0 || height <= 0)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  
  data[0] = cx + width/2; data[1] = cy;
  
  data[2] = width/2; data[3] = height/2; data[4] = 0;
  data[5] = -width; data[6] = 0;
  
  data[7] = width/2; data[8] = height/2; data[9] = 0;
  data[10] = width; data[11] = 0;
  
  err = shAppend(path, 4, comm, 12, data);
  return err;
}

#include <stdio.h>

VGU_API_CALL VGUErrorCode vguArc(VGPath path,
                                 VGfloat x, VGfloat y,
                                 VGfloat width, VGfloat height,
                                 VGfloat startAngle, VGfloat angleExtent,
                                 VGUArcType arcType)
{
  VGUErrorCode err = VGU_NO_ERROR;
  
  VGubyte commStart[1] = {VG_MOVE_TO_ABS};
  VGfloat dataStart[2];
  
  VGubyte commArcCCW[1] = {VG_SCCWARC_TO_ABS};
  VGubyte commArcCW[1]  = {VG_SCWARC_TO_ABS};
  VGfloat dataArc[5];
  
  VGubyte commEndPie[2] = {VG_LINE_TO_ABS, VG_CLOSE_PATH};
  VGfloat dataEndPie[2];
  
  VGubyte commEndChord[1] = {VG_CLOSE_PATH};
  VGfloat dataEndChord[1] = {0.0f};
  
  VGfloat alast, a = 0.0f;
  VGfloat rx = width/2, ry = height/2;
  
  if (width <= 0 || height <= 0)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  
  if (arcType != VGU_ARC_OPEN &&
      arcType != VGU_ARC_CHORD &&
      arcType != VGU_ARC_PIE)
      return VGU_ILLEGAL_ARGUMENT_ERROR;
  
  startAngle = SH_DEG2RAD(startAngle);
  angleExtent = SH_DEG2RAD(angleExtent);
  alast = startAngle + angleExtent;
  
  dataStart[0] = x + SH_COS(startAngle) * rx;
  dataStart[1] = y + SH_SIN(startAngle) * ry;
  err = shAppend(path, 1, commStart, 2, dataStart);
  if (err != VGU_NO_ERROR) return err;
  
  dataArc[0] = rx;
  dataArc[1] = ry;
  dataArc[2] = 0.0f;
  
  if (angleExtent > 0) {
    
    a = startAngle + PI;
    while (a < alast) {
      dataArc[3] = x + SH_COS(a) * rx;
      dataArc[4] = y + SH_SIN(a) * ry;
      err = shAppend(path, 1, commArcCCW, 5, dataArc);
      if (err != VGU_NO_ERROR) return err;
      a += PI; }
    
    dataArc[3] = x + SH_COS(alast) * rx;
    dataArc[4] = y + SH_SIN(alast) * ry;
    err = shAppend(path, 1, commArcCCW, 5, dataArc);
    if (err != VGU_NO_ERROR) return err;
    
  }else{
    
    a = startAngle - PI;
    while (a > alast) {
      dataArc[3] = x + SH_COS(a) * rx;
      dataArc[4] = y + SH_SIN(a) * ry;
      err = shAppend(path, 1, commArcCW, 5, dataArc);
      if (err != VGU_NO_ERROR) return err;
      a -= PI; }
    
    dataArc[3] = x + SH_COS(alast) * rx;
    dataArc[4] = y + SH_SIN(alast) * ry;
    err = shAppend(path, 1, commArcCW, 5, dataArc);
    if (err != VGU_NO_ERROR) return err;
  }
  
  
  if (arcType == VGU_ARC_PIE) {
    dataEndPie[0] = x; dataEndPie[1] = y;
    err = shAppend(path, 2, commEndPie, 2, dataEndPie);
  }else if (arcType == VGU_ARC_CHORD) {
    err = shAppend(path, 1, commEndChord, 0, dataEndChord);
  }
  
  return err;
}

VGU_API_CALL VGUErrorCode vguComputeWarpQuadToSquare(VGfloat sx0, VGfloat sy0,
                                                    VGfloat sx1, VGfloat sy1,
                                                    VGfloat sx2, VGfloat sy2,
                                                    VGfloat sx3, VGfloat sy3,
                                                    VGfloat * matrix)
{
  VGfloat squareToQuad[9];
  VGfloat quadToSquare[9];

  if (matrix == NULL)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (!shIsAligned(matrix, sizeof(VGfloat)))
    return VGU_ILLEGAL_ARGUMENT_ERROR;

  if (!shComputeWarpSquareToQuad(sx0, sy0,
                                 sx1, sy1,
                                 sx2, sy2,
                                 sx3, sy3,
                                 squareToQuad))
    return VGU_BAD_WARP_ERROR;

  if (!shWarpInvert(squareToQuad, quadToSquare))
    return VGU_BAD_WARP_ERROR;

  shWarpCopy(matrix, quadToSquare);
  return VGU_NO_ERROR;
}

VGU_API_CALL VGUErrorCode vguComputeWarpSquareToQuad(VGfloat dx0, VGfloat dy0,
                                                    VGfloat dx1, VGfloat dy1,
                                                    VGfloat dx2, VGfloat dy2,
                                                    VGfloat dx3, VGfloat dy3,
                                                    VGfloat * matrix)
{
  VGfloat squareToQuad[9];

  if (matrix == NULL)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (!shIsAligned(matrix, sizeof(VGfloat)))
    return VGU_ILLEGAL_ARGUMENT_ERROR;

  if (!shComputeWarpSquareToQuad(dx0, dy0,
                                 dx1, dy1,
                                 dx2, dy2,
                                 dx3, dy3,
                                 squareToQuad))
    return VGU_BAD_WARP_ERROR;

  shWarpCopy(matrix, squareToQuad);
  return VGU_NO_ERROR;
}

VGU_API_CALL VGUErrorCode vguComputeWarpQuadToQuad(VGfloat dx0, VGfloat dy0,
                                                  VGfloat dx1, VGfloat dy1,
                                                  VGfloat dx2, VGfloat dy2,
                                                  VGfloat dx3, VGfloat dy3,
												                          VGfloat sx0, VGfloat sy0,
                                                  VGfloat sx1, VGfloat sy1,
                                                  VGfloat sx2, VGfloat sy2,
                                                  VGfloat sx3, VGfloat sy3,
                                                  VGfloat * matrix)
{
  VGfloat sourceToSquare[9];
  VGfloat squareToDestination[9];
  VGfloat quadToQuad[9];

  if (matrix == NULL)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (!shIsAligned(matrix, sizeof(VGfloat)))
    return VGU_ILLEGAL_ARGUMENT_ERROR;

  if (vguComputeWarpQuadToSquare(sx0, sy0,
                                 sx1, sy1,
                                 sx2, sy2,
                                 sx3, sy3,
                                 sourceToSquare) != VGU_NO_ERROR)
    return VGU_BAD_WARP_ERROR;

  if (!shComputeWarpSquareToQuad(dx0, dy0,
                                 dx1, dy1,
                                 dx2, dy2,
                                 dx3, dy3,
                                 squareToDestination))
    return VGU_BAD_WARP_ERROR;

  if (!shWarpMultiply(squareToDestination, sourceToSquare, quadToQuad))
    return VGU_BAD_WARP_ERROR;

  shWarpCopy(matrix, quadToQuad);
  return VGU_NO_ERROR;
}

static VGUErrorCode shVguMapFilterError(void)
{
  VGErrorCode error = vgGetError();

  if (error == VG_BAD_HANDLE_ERROR)
    return VGU_BAD_HANDLE_ERROR;
  if (error == VG_IMAGE_IN_USE_ERROR)
    return VGU_IMAGE_IN_USE_ERROR;
  if (error == VG_ILLEGAL_ARGUMENT_ERROR)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (error == VG_OUT_OF_MEMORY_ERROR)
    return VGU_OUT_OF_MEMORY_ERROR;
  return VGU_NO_ERROR;
}

static VGPaint shVguCreateColorPaint(VGuint rgba)
{
  VGPaint paint = vgCreatePaint();

  if (paint != VG_INVALID_HANDLE)
    vgSetColor(paint, rgba);
  return paint;
}

static VGPaint shVguCreateGradientPaint(VGuint stopsCount,
                                        const VGfloat *stops)
{
  VGPaint paint = vgCreatePaint();

  if (paint == VG_INVALID_HANDLE)
    return paint;

  vgSetParameterfv(paint, VG_PAINT_COLOR_RAMP_STOPS,
                   (VGint)(stopsCount * 5u), stops);
  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_SPREAD_MODE,
                  VG_COLOR_RAMP_SPREAD_PAD);
  vgSetParameteri(paint, VG_PAINT_TYPE,
                  VG_PAINT_TYPE_LINEAR_GRADIENT);
  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED,
                  VG_FALSE);
  return paint;
}

static void shVguFilterWithPaints(VGImage dst,
                                  VGImage src,
                                  VGfloat dimX,
                                  VGfloat dimY,
                                  VGuint iterative,
                                  VGfloat strength,
                                  VGfloat offsetX,
                                  VGfloat offsetY,
                                  VGbitfield filterFlags,
                                  VGbitfield allowedQuality,
                                  VGPaint highlightPaint,
                                  VGPaint shadowPaint)
{
  VGint width = vgGetParameteri(src, VG_IMAGE_WIDTH);
  VGint height = vgGetParameteri(src, VG_IMAGE_HEIGHT);
  VGImage blur = vgCreateImage(VG_A_8, width, height, allowedQuality);

  if (blur != VG_INVALID_HANDLE) {
    vgIterativeAverageBlurKHR(blur, src, dimX, dimY, iterative, VG_TILE_PAD);
    vgParametricFilterKHR(dst, src, blur, strength,
                          offsetX, offsetY, filterFlags,
                          highlightPaint, shadowPaint);
    vgDestroyImage(blur);
  }
}

static VGUErrorCode shVguCheckStops(VGuint stopsCount,
                                    const VGfloat *stops)
{
  if (stopsCount > (VGuint)(SH_MAX_INT / 5))
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (stopsCount > 0 && !stops)
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  if (stopsCount > 0 && !shIsAligned(stops, sizeof(VGfloat)))
    return VGU_ILLEGAL_ARGUMENT_ERROR;
  return VGU_NO_ERROR;
}

VGU_API_CALL VGUErrorCode vguDropShadowKHR(VGImage dst,
                                           VGImage src,
                                           VGfloat dimX,
                                           VGfloat dimY,
                                           VGuint iterative,
                                           VGfloat strength,
                                           VGfloat distance,
                                           VGfloat angle,
                                           VGbitfield filterFlags,
                                           VGbitfield allowedQuality,
                                           VGuint shadowColorRGBA)
{
  VGPaint shadowPaint;
  VGfloat radians = SH_DEG2RAD(angle);

  vgGetError();
  shadowPaint = shVguCreateColorPaint(shadowColorRGBA);
  shVguFilterWithPaints(dst, src, dimX, dimY, iterative,
                        strength,
                        distance * SH_COS(radians),
                        distance * SH_SIN(radians),
                        filterFlags, allowedQuality,
                        VG_INVALID_HANDLE, shadowPaint);
  if (shadowPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(shadowPaint);
  return shVguMapFilterError();
}

VGU_API_CALL VGUErrorCode vguGlowKHR(VGImage dst,
                                     VGImage src,
                                     VGfloat dimX,
                                     VGfloat dimY,
                                     VGuint iterative,
                                     VGfloat strength,
                                     VGbitfield filterFlags,
                                     VGbitfield allowedQuality,
                                     VGuint glowColorRGBA)
{
  VGPaint glowPaint;

  vgGetError();
  glowPaint = shVguCreateColorPaint(glowColorRGBA);
  shVguFilterWithPaints(dst, src, dimX, dimY, iterative,
                        strength, 0.0f, 0.0f,
                        filterFlags, allowedQuality,
                        VG_INVALID_HANDLE, glowPaint);
  if (glowPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(glowPaint);
  return shVguMapFilterError();
}

VGU_API_CALL VGUErrorCode vguBevelKHR(VGImage dst,
                                      VGImage src,
                                      VGfloat dimX,
                                      VGfloat dimY,
                                      VGuint iterative,
                                      VGfloat strength,
                                      VGfloat distance,
                                      VGfloat angle,
                                      VGbitfield filterFlags,
                                      VGbitfield allowedQuality,
                                      VGuint highlightColorRGBA,
                                      VGuint shadowColorRGBA)
{
  VGPaint highlightPaint;
  VGPaint shadowPaint;
  VGfloat radians = SH_DEG2RAD(angle);

  vgGetError();
  highlightPaint = shVguCreateColorPaint(highlightColorRGBA);
  shadowPaint = shVguCreateColorPaint(shadowColorRGBA);
  shVguFilterWithPaints(dst, src, dimX, dimY, iterative,
                        strength,
                        distance * SH_COS(radians),
                        distance * SH_SIN(radians),
                        filterFlags, allowedQuality,
                        highlightPaint, shadowPaint);
  if (shadowPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(shadowPaint);
  if (highlightPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(highlightPaint);
  return shVguMapFilterError();
}

VGU_API_CALL VGUErrorCode vguGradientGlowKHR(VGImage dst,
                                             VGImage src,
                                             VGfloat dimX,
                                             VGfloat dimY,
                                             VGuint iterative,
                                             VGfloat strength,
                                             VGfloat distance,
                                             VGfloat angle,
                                             VGbitfield filterFlags,
                                             VGbitfield allowedQuality,
                                             VGuint stopsCount,
                                             const VGfloat *glowColorRampStops)
{
  VGPaint glowPaint;
  VGfloat radians;
  VGUErrorCode error = shVguCheckStops(stopsCount, glowColorRampStops);

  if (error != VGU_NO_ERROR)
    return error;

  vgGetError();
  radians = SH_DEG2RAD(angle);
  glowPaint = shVguCreateGradientPaint(stopsCount, glowColorRampStops);
  shVguFilterWithPaints(dst, src, dimX, dimY, iterative,
                        strength,
                        -distance * SH_COS(radians),
                        -distance * SH_SIN(radians),
                        filterFlags, allowedQuality,
                        glowPaint, VG_INVALID_HANDLE);
  if (glowPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(glowPaint);
  return shVguMapFilterError();
}

static void shVguSetStop(VGfloat *stops,
                         VGint index,
                         VGfloat offset,
                         VGfloat r,
                         VGfloat g,
                         VGfloat b,
                         VGfloat a)
{
  stops[index * 5 + 0] = offset;
  stops[index * 5 + 1] = r;
  stops[index * 5 + 2] = g;
  stops[index * 5 + 3] = b;
  stops[index * 5 + 4] = a;
}

VGU_API_CALL VGUErrorCode vguGradientBevelKHR(VGImage dst,
                                              VGImage src,
                                              VGfloat dimX,
                                              VGfloat dimY,
                                              VGuint iterative,
                                              VGfloat strength,
                                              VGfloat distance,
                                              VGfloat angle,
                                              VGbitfield filterFlags,
                                              VGbitfield allowedQuality,
                                              VGuint stopsCount,
                                              const VGfloat *bevelColorRampStops)
{
  VGPaint highlightPaint = VG_INVALID_HANDLE;
  VGPaint shadowPaint = VG_INVALID_HANDLE;
  VGfloat *highlightStops = NULL;
  VGfloat *shadowStops = NULL;
  VGuint midPos;
  VGboolean addStop = VG_FALSE;
  VGuint shadowCount;
  VGuint highlightCount;
  VGuint j;
  VGfloat radians;
  VGUErrorCode error = shVguCheckStops(stopsCount, bevelColorRampStops);

  if (error != VGU_NO_ERROR)
    return error;

  for (midPos=0; midPos<stopsCount; ++midPos) {
    VGfloat offset = bevelColorRampStops[midPos * 5 + 0];
    if (offset == 0.5f)
      break;
    if (offset > 0.5f) {
      addStop = VG_TRUE;
      break;
    }
  }

  if (addStop)
    shadowCount = midPos + 1u;
  else
    shadowCount = midPos < stopsCount ? midPos + 1u : stopsCount;
  highlightCount = (addStop ? 1u : 0u) + stopsCount - midPos;
  if (shadowCount > 0) {
    shadowStops = (VGfloat*)malloc((size_t)shadowCount * 5u *
                                   sizeof(VGfloat));
    if (!shadowStops)
      return VGU_OUT_OF_MEMORY_ERROR;
  }
  if (highlightCount > 0) {
    highlightStops = (VGfloat*)malloc((size_t)highlightCount * 5u *
                                      sizeof(VGfloat));
    if (!highlightStops) {
      free(shadowStops);
      return VGU_OUT_OF_MEMORY_ERROR;
    }
  }

  j = 0;
  if (addStop) {
    shVguSetStop(shadowStops, (VGint)j, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    ++j;
  }
  if (shadowCount > j) {
    VGint i;
    VGint first = addStop ? (VGint)midPos - 1 :
                  (midPos < stopsCount ? (VGint)midPos :
                   (VGint)stopsCount - 1);
    for (i=first; i>=0; --i) {
      shVguSetStop(shadowStops, (VGint)j,
                   2.0f * (0.5f - bevelColorRampStops[i * 5 + 0]),
                   bevelColorRampStops[i * 5 + 1],
                   bevelColorRampStops[i * 5 + 2],
                   bevelColorRampStops[i * 5 + 3],
                   bevelColorRampStops[i * 5 + 4]);
      ++j;
    }
  }

  j = 0;
  if (addStop) {
    shVguSetStop(highlightStops, (VGint)j, 0.0f,
                 0.0f, 0.0f, 0.0f, 0.0f);
    ++j;
  }
  for (; midPos<stopsCount; ++midPos) {
    shVguSetStop(highlightStops, (VGint)j,
                 2.0f * (bevelColorRampStops[midPos * 5 + 0] - 0.5f),
                 bevelColorRampStops[midPos * 5 + 1],
                 bevelColorRampStops[midPos * 5 + 2],
                 bevelColorRampStops[midPos * 5 + 3],
                 bevelColorRampStops[midPos * 5 + 4]);
    ++j;
  }

  vgGetError();
  radians = SH_DEG2RAD(angle);
  shadowPaint = shVguCreateGradientPaint(shadowCount, shadowStops);
  highlightPaint = shVguCreateGradientPaint(highlightCount, highlightStops);
  shVguFilterWithPaints(dst, src, dimX, dimY, iterative,
                        strength,
                        distance * SH_COS(radians),
                        distance * SH_SIN(radians),
                        filterFlags, allowedQuality,
                        highlightPaint, shadowPaint);
  if (highlightPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(highlightPaint);
  if (shadowPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(shadowPaint);
  free(highlightStops);
  free(shadowStops);
  return shVguMapFilterError();
}
