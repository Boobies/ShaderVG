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
#define VG_KHR_advanced_blending 1

#define VG_MAX_AVERAGE_BLUR_DIMENSION_KHR        0x116B
#define VG_AVERAGE_BLUR_DIMENSION_RESOLUTION_KHR 0x116C
#define VG_MAX_AVERAGE_BLUR_ITERATIONS_KHR       0x116D

#define VG_BLEND_OVERLAY_KHR        0x2010
#define VG_BLEND_HARDLIGHT_KHR      0x2011
#define VG_BLEND_SOFTLIGHT_SVG_KHR  0x2012
#define VG_BLEND_SOFTLIGHT_KHR      0x2013
#define VG_BLEND_COLORDODGE_KHR     0x2014
#define VG_BLEND_COLORBURN_KHR      0x2015
#define VG_BLEND_DIFFERENCE_KHR     0x2016
#define VG_BLEND_SUBTRACT_KHR       0x2017
#define VG_BLEND_INVERT_KHR         0x2018
#define VG_BLEND_EXCLUSION_KHR      0x2019
#define VG_BLEND_LINEARDODGE_KHR    0x201A
#define VG_BLEND_LINEARBURN_KHR     0x201B
#define VG_BLEND_VIVIDLIGHT_KHR     0x201C
#define VG_BLEND_LINEARLIGHT_KHR    0x201D
#define VG_BLEND_PINLIGHT_KHR       0x201E
#define VG_BLEND_HARDMIX_KHR        0x201F
#define VG_BLEND_CLEAR_KHR          0x2020
#define VG_BLEND_DST_KHR            0x2021
#define VG_BLEND_SRC_OUT_KHR        0x2022
#define VG_BLEND_DST_OUT_KHR        0x2023
#define VG_BLEND_SRC_ATOP_KHR       0x2024
#define VG_BLEND_DST_ATOP_KHR       0x2025
#define VG_BLEND_XOR_KHR            0x2026

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
