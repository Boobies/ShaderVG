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

#ifndef SH_VGEXT_H
#define SH_VGEXT_H

#include "openvg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VG_KHR_iterative_average_blur 1

#define VG_MAX_AVERAGE_BLUR_DIMENSION_KHR        0x116B
#define VG_AVERAGE_BLUR_DIMENSION_RESOLUTION_KHR 0x116C
#define VG_MAX_AVERAGE_BLUR_ITERATIONS_KHR       0x116D

VG_API_CALL void vgIterativeAverageBlurKHR(VGImage dst,
                                           VGImage src,
                                           VGfloat dimX,
                                           VGfloat dimY,
                                           VGuint iterative,
                                           VGTilingMode tilingMode);

#ifdef __cplusplus
}
#endif

#endif
