/*
 * Copyright (c) 2021 Takuma Hayashi
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

#include <VG/openvg.h>
#include "shContext.h"
#include "shDefs.h"
#include "shaders.h"
#include <string.h>
#include <stdio.h>

static const char* vgShaderVertexPipeline =
"#version 330\n"
"\n"
"in vec2 pos;\n"
"in vec2 textureUV;\n"
"uniform mat4 sh_Model;\n"
"uniform mat4 sh_Ortho;\n"
"uniform mat3 paintInverted;\n"
"\n"
"out vec2 texImageCoord;\n"
"out vec2 paintCoord;\n"
"\n"
"vec4 sh_Vertex;\n"
"\n"
"void shMain(void);\n"
"\n"
"void main() {\n"
"    sh_Vertex = vec4(pos, 0, 1);\n"
"    shMain();\n"
"    texImageCoord = textureUV;\n"
"    paintCoord = (paintInverted * vec3(pos, 1)).xy;\n"
"}\n";

static const char* vgShaderVertexUserDefault =
"void shMain(){ gl_Position = sh_Ortho * sh_Model * sh_Vertex; }\n";

static const char* vgShaderFragmentPipelineA =
"#version 330\n"
"\n"
"#define PAINT_TYPE_COLOR           0x1B00\n"
"#define PAINT_TYPE_LINEAR_GRADIENT 0x1B01\n"
"#define PAINT_TYPE_RADIAL_GRADIENT 0x1B02\n"
"#define PAINT_TYPE_PATTERN         0x1B03\n"
"\n"
"#define DRAW_IMAGE_NORMAL          0x1F00\n"
"#define DRAW_IMAGE_MULTIPLY        0x1F01\n"
"\n"
"#define DRAW_MODE_PATH             0\n"
"#define DRAW_MODE_IMAGE            1\n"
"\n"
"#define BLEND_MODE_NONE            0\n"
"#define BLEND_SRC                  0x2000\n"
"#define BLEND_SRC_OVER             0x2001\n"
"#define BLEND_DST_OVER             0x2002\n"
"#define BLEND_SRC_IN               0x2003\n"
"#define BLEND_DST_IN               0x2004\n"
"#define BLEND_MULTIPLY             0x2005\n"
"#define BLEND_SCREEN               0x2006\n"
"#define BLEND_DARKEN               0x2007\n"
"#define BLEND_LIGHTEN              0x2008\n"
"#define BLEND_ADDITIVE             0x2009\n"
"#define BLEND_SRC_OUT              0x200A\n"
"#define BLEND_DST_OUT              0x200B\n"
"#define BLEND_SRC_ATOP             0x200C\n"
"#define BLEND_DST_ATOP             0x200D\n"
"#define BLEND_OVERLAY              0x2010\n"
"#define BLEND_HARDLIGHT            0x2011\n"
"#define BLEND_SOFTLIGHT_SVG        0x2012\n"
"#define BLEND_SOFTLIGHT            0x2013\n"
"#define BLEND_COLORDODGE           0x2014\n"
"#define BLEND_COLORBURN            0x2015\n"
"#define BLEND_DIFFERENCE           0x2016\n"
"#define BLEND_SUBTRACT             0x2017\n"
"#define BLEND_INVERT               0x2018\n"
"#define BLEND_EXCLUSION            0x2019\n"
"#define BLEND_LINEARDODGE          0x201A\n"
"#define BLEND_LINEARBURN           0x201B\n"
"#define BLEND_VIVIDLIGHT           0x201C\n"
"#define BLEND_LINEARLIGHT          0x201D\n"
"#define BLEND_PINLIGHT             0x201E\n"
"#define BLEND_HARDMIX              0x201F\n"
"#define BLEND_CLEAR                0x2020\n"
"#define BLEND_DST                  0x2021\n"
"#define BLEND_SRC_OUT_KHR          0x2022\n"
"#define BLEND_DST_OUT_KHR          0x2023\n"
"#define BLEND_SRC_ATOP_KHR         0x2024\n"
"#define BLEND_DST_ATOP_KHR         0x2025\n"
"#define BLEND_XOR                  0x2026\n"
"\n"
"in vec2 texImageCoord;\n"
"in vec2 paintCoord;\n"
"\n"
"uniform int drawMode;\n"
"uniform sampler2D imageSampler;\n"
"uniform int imagePremultiplied;\n"
"uniform int imageMode;\n"
"uniform int paintType;\n"
"uniform vec4 paintColor;\n"
"uniform vec2 paintParams[3];\n"
"uniform sampler2D rampSampler;\n"
"uniform sampler2D patternSampler;\n"
"uniform vec4 scaleFactorBias[2];\n"
"uniform int maskEnabled;\n"
"uniform sampler2D maskSampler;\n"
"uniform vec2 maskSurfaceSize;\n"
"uniform int blendMode;\n"
"uniform sampler2D blendSampler;\n"
"uniform vec2 blendSurfaceSize;\n"
"uniform int coverageEnabled;\n"
"uniform sampler2D coverageSampler;\n"
"uniform vec2 coverageSurfaceSize;\n"
"uniform int coveragePass;\n"
"\n"
"out vec4 fragColor;\n"
"vec4 sh_Color;\n"
"\n"
"vec4 premultiplyColor(vec4 color)\n"
"{\n"
"    color = clamp(color, 0.0, 1.0);\n"
"    color.rgb *= color.a;\n"
"    return color;\n"
"}\n"
"\n"
"vec4 unpremultiplyColor(vec4 color)\n"
"{\n"
"    color = clamp(color, 0.0, 1.0);\n"
"    if (color.a <= 0.0)\n"
"        return vec4(0.0);\n"
"    color.rgb = min(color.rgb, vec3(color.a)) / color.a;\n"
"    return color;\n"
"}\n"
"\n"
"float linearGradient(vec2 fragCoord, vec2 p0, vec2 p1){\n"
"    float x  = fragCoord.x;\n"
"    float y  = fragCoord.y;\n"
"    float x0 = p0.x;\n"
"    float y0 = p0.y;\n"
"    float x1 = p1.x;\n"
"    float y1 = p1.y;\n"
"    float dx = x1 - x0;\n"
"    float dy = y1 - y0;\n"
"    return ( dx * (x - x0) + dy * (y - y0) ) / ( dx*dx + dy*dy );\n"
"}\n"
"\n";

static const char* vgShaderFragmentPipelineB =
"\n"
"float radialGradient(vec2 fragCoord, vec2 centerCoord, vec2 focalCoord, float r){\n"
"    float x   = fragCoord.x;\n"
"    float y   = fragCoord.y;\n"
"    float cx  = centerCoord.x;\n"
"    float cy  = centerCoord.y;\n"
"    float fx  = focalCoord.x;\n"
"    float fy  = focalCoord.y;\n"
"    float dx  = x - fx;\n"
"    float dy  = y - fy;\n"
"    float dfx = fx - cx;\n"
"    float dfy = fy - cy;\n"
"    return ( (dx * dfx + dy * dfy) + sqrt(r*r*(dx*dx + dy*dy) - pow(dx*dfy - dy*dfx, 2.0)) )\n"
"         / ( r*r - (dfx*dfx + dfy*dfy) );\n"
"}\n"
"\n"
"vec4 applyBlendMode(vec4 src, vec4 coveredSrc, float coverage);\n"
"void shMain(void);\n"
"\n"
"void main()\n"
"{\n"
"    vec4 col;\n"
"    float coverageValue = 1.0;\n"
"    if(coveragePass != 0) {\n"
"        fragColor = vec4(1.0);\n"
"        return;\n"
"    }\n"
"    switch(paintType){\n"
"    case PAINT_TYPE_LINEAR_GRADIENT:\n"
"        {\n"
"            vec2 x0 = paintParams[0];\n"
"            vec2 x1 = paintParams[1];\n"
"            float factor = linearGradient(paintCoord, x0, x1);\n"
"            col = texture(rampSampler, vec2(factor, 0.5));\n"
"        }\n"
"        break;\n"
"    case PAINT_TYPE_RADIAL_GRADIENT:\n"
"        {\n"
"            vec2 center = paintParams[0];\n"
"            vec2 focal = paintParams[1];\n"
"            float radius = paintParams[2].x;\n"
"            float factor = radialGradient(paintCoord, center, focal, radius);\n"
"            col = texture(rampSampler, vec2(factor, 0.5));\n"
"        }\n"
"        break;\n"
"    case PAINT_TYPE_PATTERN:\n"
"        {\n"
"            float width = paintParams[0].x;\n"
"            float height = paintParams[0].y;\n"
"            vec2 texCoord = vec2(paintCoord.x / width, paintCoord.y / height);\n"
"            col = texture(patternSampler, texCoord);\n"
"        }\n"
"        break;\n"
"    default:\n"
"    case PAINT_TYPE_COLOR:\n"
"        col = paintColor;\n"
"        break;\n"
"    }\n"
"    if(drawMode == DRAW_MODE_IMAGE) {\n"
"        vec4 imageColor = texture(imageSampler, texImageCoord);\n"
"        if (imagePremultiplied != 0)\n"
"            imageColor = unpremultiplyColor(imageColor);\n"
"        col = imageColor * (imageMode == DRAW_IMAGE_MULTIPLY ? col : vec4(1.0));\n"
"    }\n"
"    sh_Color = col * scaleFactorBias[0] + scaleFactorBias[1];\n"
"    shMain();\n"
"    if(coverageEnabled != 0) {\n"
"        coverageValue *= texture(coverageSampler, gl_FragCoord.xy / coverageSurfaceSize).r;\n"
"    }\n"
"    if(maskEnabled != 0) {\n"
"        coverageValue *= texture(maskSampler, gl_FragCoord.xy / maskSurfaceSize).r;\n"
"    }\n"
"    fragColor = premultiplyColor(fragColor);\n"
"    fragColor = applyBlendMode(fragColor, fragColor * coverageValue, coverageValue);\n"
"}\n";

static const char* vgShaderFragmentBlendPipelineA =
"float safeDivide(float numerator, float denominator)\n"
"{\n"
"    return denominator == 0.0 ? 0.0 : numerator / denominator;\n"
"}\n"
"\n"
"vec3 safeDivide(vec3 numerator, float denominator)\n"
"{\n"
"    return denominator == 0.0 ? vec3(0.0) : numerator / denominator;\n"
"}\n"
"\n"
"vec3 safeDivide(vec3 numerator, vec3 denominator)\n"
"{\n"
"    return vec3(safeDivide(numerator.r, denominator.r),\n"
"                safeDivide(numerator.g, denominator.g),\n"
"                safeDivide(numerator.b, denominator.b));\n"
"}\n"
"\n"
"vec3 premultipliedCommon(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    return sp * (1.0 - da) + dp * (1.0 - sa);\n"
"}\n"
"\n"
"vec3 blendOverlay(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 multiplyPart = 2.0 * sp * dp + blendCommon;\n"
"    vec3 screenPart = sa * da - 2.0 * (da - dp) * (sa - sp) + blendCommon;\n"
"    vec3 useMultiply = 1.0 - step(vec3(da - 0.000001), 2.0 * dp);\n"
"    return mix(screenPart, multiplyPart, useMultiply);\n"
"}\n"
"\n"
"vec3 blendHardlight(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 multiplyPart = 2.0 * sp * dp + blendCommon;\n"
"    vec3 screenPart = sa * da - 2.0 * (da - dp) * (sa - sp) + blendCommon;\n"
"    vec3 useMultiply = 1.0 - step(vec3(sa - 0.000001), 2.0 * sp);\n"
"    return mix(screenPart, multiplyPart, useMultiply);\n"
"}\n"
"\n"
"vec3 blendSoftlightSvg(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 dOverDa = safeDivide(dp, da);\n"
"    vec3 first = dp * (sa - (1.0 - dOverDa) * (2.0 * sp - sa)) + blendCommon;\n"
"    vec3 secondLow = dp * (sa - (1.0 - dOverDa) *\n"
"                     (2.0 * sp - sa) * (3.0 - 8.0 * dOverDa)) + blendCommon;\n"
"    vec3 secondHigh = dp * sa + (sqrt(dOverDa) * da - dp) *\n"
"                      (2.0 * sp - sa) + blendCommon;\n"
"    vec3 second = mix(secondLow, secondHigh, step(vec3(da), 8.0 * dp));\n"
"    return mix(second, first, 1.0 - step(vec3(sa - 0.000001), 2.0 * sp));\n"
"}\n"
"\n"
"vec3 blendSoftlight(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 dOverDa = safeDivide(dp, da);\n"
"    vec3 first = dp * sa + dp * (1.0 - dOverDa) *\n"
"                 (2.0 * sp - sa) + blendCommon;\n"
"    vec3 secondLow = dp * sa + dp *\n"
"                     ((16.0 * dOverDa - 12.0) * dOverDa + 3.0) *\n"
"                     (2.0 * sp - sa) + blendCommon;\n"
"    vec3 secondHigh = dp * sa + (sqrt(dOverDa) * da - dp) *\n"
"                      (2.0 * sp - sa) + blendCommon;\n"
"    vec3 second = mix(secondLow, secondHigh, step(vec3(da), 4.0 * dp));\n"
"    return mix(second, first, 1.0 - step(vec3(sa - 0.000001), 2.0 * sp));\n"
"}\n";

static const char* vgShaderFragmentBlendPipelineB =
"\n"
"vec3 blendColorDodge(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 ratio = safeDivide(sp, sa);\n"
"    vec3 dodge = min(vec3(sa * da), safeDivide(dp * sa, 1.0 - ratio)) + blendCommon;\n"
"    vec3 full = vec3(sa * da) + blendCommon;\n"
"    return mix(full, dodge, 1.0 - step(vec3(sa - 0.000001), sp));\n"
"}\n"
"\n"
"vec3 blendColorBurn(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 burn = vec3(sa * da) -\n"
"                min(vec3(sa * da), safeDivide(vec3(sa * sa) * (da - dp), sp)) +\n"
"                blendCommon;\n"
"    return mix(blendCommon, burn, step(vec3(0.000001), sp));\n"
"}\n"
"\n"
"vec3 blendVividLight(vec3 sp, vec3 dp, float sa, float da)\n"
"{\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    vec3 burn = vec3(sa * da) -\n"
"                min(vec3(sa * da), safeDivide(vec3(sa * sa) * (da - dp), 2.0 * sp)) +\n"
"                blendCommon;\n"
"    burn = mix(blendCommon, burn, step(vec3(0.000001), sp));\n"
"    vec3 ratio = safeDivide(sp, sa);\n"
"    vec3 dodge = min(vec3(sa * da), safeDivide(dp * sa, 2.0 * (1.0 - ratio))) + blendCommon;\n"
"    vec3 full = vec3(sa * da) + blendCommon;\n"
"    dodge = mix(full, dodge, 1.0 - step(vec3(sa - 0.000001), sp));\n"
"    return mix(dodge, burn, 1.0 - step(vec3(sa - 0.000001), 2.0 * sp));\n"
"}\n"
"\n"
"vec3 clampPremultiplied(vec3 color, float alpha)\n"
"{\n"
"    return clamp(color, vec3(0.0), vec3(alpha));\n"
"}\n";

static const char* vgShaderFragmentBlendPipelineC =
"\n"
"vec4 applyBlendMode(vec4 src, vec4 coveredSrc, float coverage)\n"
"{\n"
"    if (blendMode == BLEND_MODE_NONE)\n"
"        return coveredSrc;\n"
"\n"
"    vec4 dst = texture(blendSampler, gl_FragCoord.xy / blendSurfaceSize);\n"
"    float sa = clamp(src.a, 0.0, 1.0);\n"
"    float da = clamp(dst.a, 0.0, 1.0);\n"
"    vec3 sp = clamp(src.rgb, vec3(0.0), vec3(sa));\n"
"    vec3 dp = clamp(dst.rgb, vec3(0.0), vec3(da));\n"
"    vec3 blendCommon = premultipliedCommon(sp, dp, sa, da);\n"
"    float overA = sa + da * (1.0 - sa);\n"
"    float sad = sa * da;\n"
"    vec3 outRgb = coveredSrc.rgb;\n"
"    float outA = coveredSrc.a;\n"
"\n"
"    switch (blendMode) {\n"
"    case BLEND_SRC:\n"
"        outRgb = sp;\n"
"        outA = sa;\n"
"        break;\n"
"    case BLEND_SRC_OVER:\n"
"        outRgb = sp + dp * (1.0 - sa);\n"
"        outA = sa + da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_DST_OVER:\n"
"        outRgb = sp * (1.0 - da) + dp;\n"
"        outA = sa * (1.0 - da) + da;\n"
"        break;\n"
"    case BLEND_SRC_IN:\n"
"        outRgb = sp * da;\n"
"        outA = sa * da;\n"
"        break;\n"
"    case BLEND_DST_IN:\n"
"        outRgb = dp * sa;\n"
"        outA = da * sa;\n"
"        break;\n"
"    case BLEND_CLEAR:\n"
"        outRgb = vec3(0.0);\n"
"        outA = 0.0;\n"
"        break;\n"
"    case BLEND_DST:\n"
"        outRgb = dp;\n"
"        outA = da;\n"
"        break;\n"
"    case BLEND_MULTIPLY:\n"
"        outRgb = blendCommon + sp * dp;\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_SCREEN:\n"
"        outRgb = sp + dp - sp * dp;\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_DARKEN:\n"
"        outRgb = min(sp + dp * (1.0 - sa), dp + sp * (1.0 - da));\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_LIGHTEN:\n"
"        outRgb = max(sp + dp * (1.0 - sa), dp + sp * (1.0 - da));\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_ADDITIVE:\n"
"        outRgb = min(sp + dp, vec3(1.0));\n"
"        outA = min(sa + da, 1.0);\n"
"        break;\n"
"    case BLEND_SRC_OUT:\n"
"    case BLEND_SRC_OUT_KHR:\n"
"        outRgb = sp * (1.0 - da);\n"
"        outA = sa * (1.0 - da);\n"
"        break;\n"
"    case BLEND_DST_OUT:\n"
"    case BLEND_DST_OUT_KHR:\n"
"        outRgb = dp * (1.0 - sa);\n"
"        outA = da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_SRC_ATOP:\n"
"    case BLEND_SRC_ATOP_KHR:\n"
"        outRgb = sp * da + dp * (1.0 - sa);\n"
"        outA = da;\n"
"        break;\n"
"    case BLEND_DST_ATOP:\n"
"    case BLEND_DST_ATOP_KHR:\n"
"        outRgb = dp * sa + sp * (1.0 - da);\n"
"        outA = sa;\n"
"        break;\n";

static const char* vgShaderFragmentBlendPipelineD =
"    case BLEND_XOR:\n"
"        outRgb = sp * (1.0 - da) + dp * (1.0 - sa);\n"
"        outA = sa * (1.0 - da) + da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_OVERLAY:\n"
"        outRgb = blendOverlay(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_HARDLIGHT:\n"
"        outRgb = blendHardlight(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_SOFTLIGHT_SVG:\n"
"        outRgb = blendSoftlightSvg(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_SOFTLIGHT:\n"
"        outRgb = blendSoftlight(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_COLORDODGE:\n"
"        outRgb = blendColorDodge(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_COLORBURN:\n"
"        outRgb = blendColorBurn(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_DIFFERENCE:\n"
"        outRgb = sp + dp - 2.0 * min(sp * da, dp * sa);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_SUBTRACT:\n"
"        outRgb = max(dp - sp, vec3(0.0));\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_INVERT:\n"
"        outRgb = (1.0 - sa) * dp + sa * (1.0 - dp);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_EXCLUSION:\n"
"        outRgb = sp * da + dp * sa - 2.0 * sp * dp + blendCommon;\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_LINEARDODGE:\n"
"        outRgb = mix(vec3(sad) + blendCommon, sp + dp,\n"
"                     1.0 - step(vec3(sad), sp * da + dp * sa));\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_LINEARBURN:\n"
"        outRgb = mix(blendCommon, sp + dp - sad,\n"
"                     step(vec3(sad), sp * da + dp * sa));\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_VIVIDLIGHT:\n"
"        outRgb = blendVividLight(sp, dp, sa, da);\n"
"        outA = overA;\n"
"        break;\n"
"    case BLEND_LINEARLIGHT:\n"
"        {\n"
"            vec3 value = 2.0 * sp * da + dp * sa;\n"
"            vec3 low = blendCommon;\n"
"            vec3 middle = value - sad + blendCommon;\n"
"            vec3 high = vec3(sad) + blendCommon;\n"
"            outRgb = mix(middle, high, step(vec3(2.0 * sad + 0.000001), value));\n"
"            outRgb = mix(low, outRgb, step(vec3(sad + 0.000001), value));\n"
"            outA = overA;\n"
"        }\n"
"        break;\n"
"    case BLEND_PINLIGHT:\n"
"        {\n"
"            vec3 value = 2.0 * sp * da;\n"
"            vec3 lowGroup = mix(dp * sa + blendCommon, value + blendCommon,\n"
"                                1.0 - step(dp * sa - 0.000001, value));\n"
"            vec3 highGroup = mix(value - sad + blendCommon, blendCommon,\n"
"                                 1.0 - step(vec3(sa - 0.000001), 2.0 * sp));\n"
"            outRgb = mix(lowGroup, highGroup,\n"
"                         step(vec3(sad + 0.000001), value - dp * sa));\n"
"            outA = overA;\n"
"        }\n"
"        break;\n"
"    case BLEND_HARDMIX:\n"
"        outRgb = mix(vec3(sad) + blendCommon, blendCommon,\n"
"                     1.0 - step(vec3(sad - 0.000001), sp * da + dp * sa));\n"
"        outA = overA;\n"
"        break;\n"
"    default:\n"
"        return coveredSrc;\n"
"    }\n"
"\n"
"    outA = clamp(outA, 0.0, 1.0);\n"
"    vec4 blended = vec4(clampPremultiplied(outRgb, outA), outA);\n"
"    return mix(dst, blended, clamp(coverage, 0.0, 1.0));\n"
"}\n";

static const char* vgShaderFragmentUserDefault =
"void shMain(){ fragColor = sh_Color; }\n";

static const char* vgShaderVertexColorRamp =
"#version 330\n"
"\n"
"in vec2 pos;\n"
"\n"
"void main()\n"
"{\n"
"    gl_Position = vec4(pos.xy, 0, 1);\n"
"}\n";

static const char* vgShaderFragmentColorRamp =
"#version 330\n"
"\n"
"uniform vec4 startColor;\n"
"uniform vec4 endColor;\n"
"uniform float startPixel;\n"
"uniform float pixelSpan;\n"
"out vec4 fragColor;\n"
"\n"
"void main()\n"
"{\n"
"    float k = (gl_FragCoord.x - 0.5 - startPixel) / pixelSpan;\n"
"    fragColor = mix(startColor, endColor, clamp(k, 0.0, 1.0));\n"
"}\n";

static const char* vgShaderVertexCoverage =
"#version 330\n"
"\n"
"in vec2 pos;\n"
"uniform vec2 targetSize;\n"
"\n"
"void main()\n"
"{\n"
"    vec2 clip = vec2(pos.x / targetSize.x * 2.0 - 1.0,\n"
"                     pos.y / targetSize.y * 2.0 - 1.0);\n"
"    gl_Position = vec4(clip, 0.0, 1.0);\n"
"}\n";

static const char* vgShaderFragmentCoverage =
"#version 330\n"
"\n"
"uniform sampler2D sourceSampler;\n"
"uniform int scale;\n"
"out vec4 fragColor;\n"
"\n"
"void main()\n"
"{\n"
"    ivec2 base = ivec2(gl_FragCoord.xy) * scale;\n"
"    float sum = 0.0;\n"
"    for (int y = 0; y < 4; ++y) {\n"
"        for (int x = 0; x < 4; ++x) {\n"
"            if (x < scale && y < scale)\n"
"                sum += texelFetch(sourceSampler, base + ivec2(x, y), 0).r;\n"
"        }\n"
"    }\n"
"    float coverage = sum / float(scale * scale);\n"
"    fragColor = vec4(coverage, coverage, coverage, coverage);\n"
"}\n";

static const char* vgShaderVertexImageFilter =
"#version 330\n"
"\n"
"in vec2 pos;\n"
"uniform vec2 targetSize;\n"
"\n"
"void main()\n"
"{\n"
"    vec2 clip = vec2(pos.x / targetSize.x * 2.0 - 1.0,\n"
"                     pos.y / targetSize.y * 2.0 - 1.0);\n"
"    gl_Position = vec4(clip, 0.0, 1.0);\n"
"}\n";

static const char* vgShaderFragmentImageFilterA =
"#version 330\n"
"\n"
"#define FILTER_COLOR_MATRIX 0\n"
"#define FILTER_CONVOLVE 1\n"
"#define FILTER_SEPARABLE_X 2\n"
"#define FILTER_SEPARABLE_Y 3\n"
"#define FILTER_GAUSSIAN_X 4\n"
"#define FILTER_GAUSSIAN_Y 5\n"
"#define FILTER_LOOKUP 6\n"
"#define FILTER_LOOKUP_SINGLE 7\n"
"#define FILTER_TRANSFER 8\n"
"#define FILTER_PARAMETRIC 9\n"
"\n"
"#define STORAGE_RGBA 0\n"
"#define STORAGE_ALPHA 1\n"
"#define STORAGE_LUMINANCE 2\n"
"#define STORAGE_FLOAT 3\n"
"\n"
"#define PAINT_NONE 0\n"
"#define PAINT_COLOR 1\n"
"#define PAINT_LINEAR_GRADIENT 2\n"
"\n"
"#define VG_PF_OBJECT_VISIBLE_FLAG_KHR 1\n"
"#define VG_PF_KNOCKOUT_FLAG_KHR 2\n"
"#define VG_PF_OUTER_FLAG_KHR 4\n"
"#define VG_PF_INNER_FLAG_KHR 8\n"
"\n"
"#define VG_TILE_FILL 0x1D00\n"
"#define VG_TILE_PAD 0x1D01\n"
"#define VG_TILE_REPEAT 0x1D02\n"
"#define VG_TILE_REFLECT 0x1D03\n"
"\n"
"uniform int mode;\n"
"uniform sampler2D sourceSampler;\n"
"uniform sampler2D auxSampler;\n"
"uniform ivec2 sourceSize;\n"
"uniform ivec2 sourceOrigin;\n"
"uniform ivec2 targetOrigin;\n"
"uniform int sourcePremultiplied;\n"
"uniform ivec2 kernelSize;\n"
"uniform ivec2 shift;\n"
"uniform float scale;\n"
"uniform float bias;\n"
"uniform int tilingMode;\n"
"uniform vec4 colorMatrix[4];\n"
"uniform vec4 colorBias;\n"
"uniform vec4 tileFillColor;\n"
"uniform int sourceLinear;\n"
"uniform int filterLinear;\n"
"uniform int outputLinear;\n"
"uniform int dstLinear;\n"
"uniform int premultiplyInput;\n"
"uniform int unpremultiplyOutput;\n"
"uniform int dstStorageMode;\n"
"uniform int lookupSourceChannel;\n"
"uniform ivec2 blurSize;\n"
"uniform vec2 parametricOffset;\n"
"uniform float parametricStrength;\n"
"uniform int parametricFlags;\n"
"uniform int highlightPaintMode;\n"
"uniform int shadowPaintMode;\n"
"uniform vec4 highlightColor;\n"
"uniform vec4 shadowColor;\n"
"uniform sampler2D highlightSampler;\n"
"uniform sampler2D shadowSampler;\n"
"out vec4 fragColor;\n"
"\n"
"float srgbToLinear(float value)\n"
"{\n"
"    if (value <= 0.0)\n"
"        return 0.0;\n"
"    if (value >= 1.0)\n"
"        return 1.0;\n"
"    if (value <= 0.04045)\n"
"        return value / 12.92;\n"
"    return pow((value + 0.055) / 1.055, 2.4);\n"
"}\n"
"\n"
"float linearToSrgb(float value)\n"
"{\n"
"    if (value <= 0.0)\n"
"        return 0.0;\n"
"    if (value >= 1.0)\n"
"        return 1.0;\n"
"    if (value <= 0.0031308)\n"
"        return value * 12.92;\n"
"    return 1.055 * pow(value, 1.0 / 2.4) - 0.055;\n"
"}\n"
"\n"
"vec3 convertColorSpace(vec3 color, int fromLinear, int toLinear)\n"
"{\n"
"    if (fromLinear == toLinear)\n"
"        return color;\n"
"    if (toLinear != 0)\n"
"        return vec3(srgbToLinear(color.r),\n"
"                    srgbToLinear(color.g),\n"
"                    srgbToLinear(color.b));\n"
"    return vec3(linearToSrgb(color.r),\n"
"                linearToSrgb(color.g),\n"
"                linearToSrgb(color.b));\n"
"}\n"
"\n"
"int intMod(int value, int modulus)\n"
"{\n"
"    int result = value % modulus;\n"
"    return result < 0 ? result + modulus : result;\n"
"}\n"
"\n"
"bool tileCoord(inout ivec2 coord)\n"
"{\n"
"    if (coord.x >= 0 && coord.x < sourceSize.x &&\n"
"        coord.y >= 0 && coord.y < sourceSize.y)\n"
"        return true;\n"
"\n"
"    if (tilingMode == VG_TILE_FILL)\n"
"        return false;\n"
"\n"
"    if (tilingMode == VG_TILE_PAD) {\n"
"        coord = clamp(coord, ivec2(0), sourceSize - ivec2(1));\n"
"        return true;\n"
"    }\n"
"\n"
"    if (tilingMode == VG_TILE_REPEAT) {\n"
"        coord.x = intMod(coord.x, sourceSize.x);\n"
"        coord.y = intMod(coord.y, sourceSize.y);\n"
"        return true;\n"
"    }\n"
"\n"
"    coord.x = intMod(coord.x, sourceSize.x * 2);\n"
"    coord.y = intMod(coord.y, sourceSize.y * 2);\n"
"    if (coord.x >= sourceSize.x)\n"
"        coord.x = sourceSize.x * 2 - 1 - coord.x;\n"
"    if (coord.y >= sourceSize.y)\n"
"        coord.y = sourceSize.y * 2 - 1 - coord.y;\n"
"    return true;\n"
"}\n"
"\n"
"vec4 loadSource(ivec2 coord)\n"
"{\n"
"    vec4 color = texelFetch(sourceSampler, coord, 0);\n"
"    color.rgb = convertColorSpace(color.rgb, sourceLinear, filterLinear);\n"
"    if (premultiplyInput != 0)\n"
"        color.rgb *= color.a;\n"
"    return color;\n"
"}\n"
"\n"
"vec4 sampleSource(ivec2 coord)\n"
"{\n"
"    if (!tileCoord(coord))\n"
"        return tileFillColor;\n"
"    return loadSource(coord);\n"
"}\n"
"\n"
"vec4 sampleRaw(ivec2 coord, vec4 edge)\n"
"{\n"
"    if (!tileCoord(coord))\n"
"        return edge;\n"
"    return texelFetch(sourceSampler, coord, 0);\n"
"}\n"
"\n";

static const char* vgShaderFragmentImageFilterB =
"\n"
"vec4 finishColor(vec4 color)\n"
"{\n"
"    color = clamp(color, 0.0, 1.0);\n"
"    if (unpremultiplyOutput != 0) {\n"
"        if (color.a <= 0.0)\n"
"            color = vec4(0.0);\n"
"        else {\n"
"            color.rgb = min(color.rgb, vec3(color.a));\n"
"            color.rgb /= color.a;\n"
"        }\n"
"    }\n"
"    return clamp(color, 0.0, 1.0);\n"
"}\n"
"\n"
"vec4 storeColor(vec4 color)\n"
"{\n"
"    if (dstStorageMode == STORAGE_FLOAT)\n"
"        return color;\n"
"\n"
"    color = finishColor(color);\n"
"    color.rgb = convertColorSpace(color.rgb, outputLinear, dstLinear);\n"
"\n"
"    if (dstStorageMode == STORAGE_ALPHA)\n"
"        return vec4(color.a, 0.0, 0.0, 1.0);\n"
"    if (dstStorageMode == STORAGE_LUMINANCE) {\n"
"        float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
"        return vec4(luma, 0.0, 0.0, 1.0);\n"
"    }\n"
"    return color;\n"
"}\n"
"\n"
"int colorToIndex(float value)\n"
"{\n"
"    return int(floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5));\n"
"}\n"
"\n"
"float channelForIndex(vec4 color, int channel)\n"
"{\n"
"    if (channel == 1)\n"
"        return color.g;\n"
"    if (channel == 2)\n"
"        return color.b;\n"
"    if (channel == 3)\n"
"        return color.a;\n"
"    return color.r;\n"
"}\n"
"\n"
"vec4 applyConvolve(ivec2 pixel)\n"
"{\n"
"    vec4 sum = vec4(0.0);\n"
"    for (int ky = 0; ky < kernelSize.y; ++ky) {\n"
"        for (int kx = 0; kx < kernelSize.x; ++kx) {\n"
"            ivec2 sampleCoord = pixel + ivec2(kx - shift.x,\n"
"                                              ky - shift.y);\n"
"            ivec2 kernelCoord = ivec2(kernelSize.x - kx - 1,\n"
"                                      kernelSize.y - ky - 1);\n"
"            float weight = texelFetch(auxSampler, kernelCoord, 0).r;\n"
"            sum += sampleSource(sampleCoord) * weight;\n"
"        }\n"
"    }\n"
"    return sum * scale + vec4(bias);\n"
"}\n"
"\n"
"vec4 applySeparableX(ivec2 pixel)\n"
"{\n"
"    vec4 sum = vec4(0.0);\n"
"    for (int k = 0; k < kernelSize.x; ++k) {\n"
"        ivec2 sampleCoord = pixel + ivec2(k - shift.x, 0);\n"
"        float weight = texelFetch(auxSampler,\n"
"                                  ivec2(kernelSize.x - k - 1, 0), 0).r;\n"
"        sum += sampleSource(sampleCoord) * weight;\n"
"    }\n"
"    return sum;\n"
"}\n"
"\n"
"vec4 applySeparableY(ivec2 pixel)\n"
"{\n"
"    vec4 sum = vec4(0.0);\n"
"    for (int k = 0; k < kernelSize.y; ++k) {\n"
"        ivec2 sampleCoord = pixel + ivec2(0, k - shift.y);\n"
"        float weight = texelFetch(auxSampler,\n"
"                                  ivec2(kernelSize.y - k - 1, 0), 0).r;\n"
"        sum += sampleRaw(sampleCoord, tileFillColor) * weight;\n"
"    }\n"
"    if (mode == FILTER_SEPARABLE_Y)\n"
"        sum = sum * scale + vec4(bias);\n"
"    return sum;\n"
"}\n"
"\n";

static const char* vgShaderFragmentImageFilterD =
"\n"
"bool parametricFlag(int flag)\n"
"{\n"
"    return (parametricFlags & flag) != 0;\n"
"}\n"
"\n"
"float sampleBlurAlpha(vec2 coord)\n"
"{\n"
"    if (coord.x < 0.0 || coord.y < 0.0 ||\n"
"        coord.x > float(blurSize.x - 1) ||\n"
"        coord.y > float(blurSize.y - 1))\n"
"        return 0.0;\n"
"    return texture(auxSampler, (coord + vec2(0.5)) / vec2(blurSize)).a;\n"
"}\n"
"\n"
"vec4 paintForAmount(int paintMode, vec4 paintColor,\n"
"                    sampler2D paintSampler, float amount)\n"
"{\n"
"    vec4 color;\n"
"\n"
"    if (paintMode == PAINT_NONE)\n"
"        return vec4(0.0);\n"
"\n"
"    if (paintMode == PAINT_COLOR) {\n"
"        color = paintColor;\n"
"        color.rgb = convertColorSpace(color.rgb, 0, filterLinear);\n"
"        color.rgb *= color.a;\n"
"        return color * amount;\n"
"    }\n"
"\n"
"    color = texture(paintSampler, vec2(amount, 0.5));\n"
"    color.rgb = convertColorSpace(color.rgb, 0, filterLinear);\n"
"    color.rgb *= color.a;\n"
"    return color;\n"
"}\n"
"\n"
"vec4 applyParametric(ivec2 pixel)\n"
"{\n"
"    vec4 source = loadSource(pixel);\n"
"    vec2 base = vec2(pixel);\n"
"    float hblur = sampleBlurAlpha(base + parametricOffset);\n"
"    float sblur = sampleBlurAlpha(base - parametricOffset);\n"
"    float highlightAlpha = max(hblur - sblur, 0.0);\n"
"    float shadowAlpha = max(sblur - hblur, 0.0);\n"
"    vec4 highlightPixel = paintForAmount(highlightPaintMode,\n"
"                                         highlightColor,\n"
"                                         highlightSampler,\n"
"                                         highlightAlpha * parametricStrength);\n"
"    vec4 shadowPixel = paintForAmount(shadowPaintMode,\n"
"                                      shadowColor,\n"
"                                      shadowSampler,\n"
"                                      shadowAlpha * parametricStrength);\n"
"    vec4 inverseShadowPixel = paintForAmount(shadowPaintMode,\n"
"                                             shadowColor,\n"
"                                             shadowSampler,\n"
"                                             (1.0 - shadowAlpha) *\n"
"                                             parametricStrength);\n"
"    vec4 outerEffect = highlightPixel + shadowPixel;\n"
"    vec4 innerEffect = highlightPaintMode == PAINT_NONE ?\n"
"                       inverseShadowPixel : outerEffect;\n"
"    bool inner = parametricFlag(VG_PF_INNER_FLAG_KHR);\n"
"    bool outer = parametricFlag(VG_PF_OUTER_FLAG_KHR);\n"
"    bool knockout = parametricFlag(VG_PF_KNOCKOUT_FLAG_KHR);\n"
"    bool objectVisible = parametricFlag(VG_PF_OBJECT_VISIBLE_FLAG_KHR);\n"
"    float innerAlpha = inner ? source.a : 0.0;\n"
"    float objectAlpha = 0.0;\n"
"    float outerAlpha = 0.0;\n"
"\n"
"    source.rgb = min(source.rgb, vec3(source.a));\n"
"\n"
"    if (!knockout && objectVisible)\n"
"        objectAlpha = inner ? (1.0 - innerEffect.a) : 1.0;\n"
"    if (outer)\n"
"        outerAlpha = (knockout || objectVisible) ?\n"
"                     (1.0 - source.a) : 1.0;\n"
"\n"
"    return innerAlpha * innerEffect +\n"
"           objectAlpha * source +\n"
"           outerAlpha * outerEffect;\n"
"}\n"
"\n";

static const char* vgShaderFragmentImageFilterC =
"\n"
"vec4 applyLookup(vec4 source)\n"
"{\n"
"    ivec4 index = ivec4(colorToIndex(source.r),\n"
"                        colorToIndex(source.g),\n"
"                        colorToIndex(source.b),\n"
"                        colorToIndex(source.a));\n"
"    return vec4(texelFetch(auxSampler, ivec2(index.r, 0), 0).r,\n"
"                texelFetch(auxSampler, ivec2(index.g, 0), 0).g,\n"
"                texelFetch(auxSampler, ivec2(index.b, 0), 0).b,\n"
"                texelFetch(auxSampler, ivec2(index.a, 0), 0).a);\n"
"}\n"
"\n"
"vec4 unpremultiplySource(vec4 color)\n"
"{\n"
"    if (sourcePremultiplied == 0)\n"
"        return color;\n"
"    color = clamp(color, 0.0, 1.0);\n"
"    if (color.a <= 0.0)\n"
"        return vec4(0.0);\n"
"    color.rgb = min(color.rgb, vec3(color.a)) / color.a;\n"
"    return color;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"    ivec2 pixel = ivec2(gl_FragCoord.xy);\n"
"    vec4 source;\n"
"    vec4 result;\n"
"\n"
"    if (mode == FILTER_TRANSFER) {\n"
"        result = unpremultiplySource(texelFetch(sourceSampler,\n"
"                                                sourceOrigin + pixel - targetOrigin,\n"
"                                                0));\n"
"    } else if (mode == FILTER_COLOR_MATRIX) {\n"
"        source = loadSource(pixel);\n"
"        result = vec4(dot(colorMatrix[0], source),\n"
"                      dot(colorMatrix[1], source),\n"
"                      dot(colorMatrix[2], source),\n"
"                      dot(colorMatrix[3], source)) + colorBias;\n"
"    } else if (mode == FILTER_CONVOLVE) {\n"
"        result = applyConvolve(pixel);\n"
"    } else if (mode == FILTER_SEPARABLE_X || mode == FILTER_GAUSSIAN_X) {\n"
"        result = applySeparableX(pixel);\n"
"    } else if (mode == FILTER_SEPARABLE_Y || mode == FILTER_GAUSSIAN_Y) {\n"
"        result = applySeparableY(pixel);\n"
"    } else if (mode == FILTER_LOOKUP) {\n"
"        result = applyLookup(loadSource(pixel));\n"
"    } else if (mode == FILTER_PARAMETRIC) {\n"
"        result = applyParametric(pixel);\n"
"    } else {\n"
"        source = loadSource(pixel);\n"
"        result = texelFetch(auxSampler,\n"
"                           ivec2(colorToIndex(channelForIndex(\n"
"                                 source, lookupSourceChannel)), 0), 0);\n"
"    }\n"
"\n"
"    fragColor = storeColor(result);\n"
"}\n";

void shInitPiplelineShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);
  const char* extendedStage;
  const char* buf[7];
  GLint size[7];

  context->vs = glCreateShader(GL_VERTEX_SHADER);
  if(context->userShaderVertex){
    extendedStage = (const char*)context->userShaderVertex;
  } else {
    extendedStage = vgShaderVertexUserDefault;
  }
  buf[0] = vgShaderVertexPipeline;
  buf[1] = extendedStage;
  size[0] = strlen(vgShaderVertexPipeline);
  size[1] = strlen(extendedStage);
  glShaderSource(context->vs, 2, buf, size);
  glCompileShader(context->vs);
  SH_CHECK_SHADER_COMPILE(context->vs, "pipeline vertex");
  GL_CHECK_ERROR;

  context->fs = glCreateShader(GL_FRAGMENT_SHADER);
  if(context->userShaderFragment){
    extendedStage = (const char*)context->userShaderFragment;
  } else {
    extendedStage = vgShaderFragmentUserDefault;
  }
  buf[0] = vgShaderFragmentPipelineA;
  buf[1] = vgShaderFragmentPipelineB;
  buf[2] = vgShaderFragmentBlendPipelineA;
  buf[3] = vgShaderFragmentBlendPipelineB;
  buf[4] = vgShaderFragmentBlendPipelineC;
  buf[5] = vgShaderFragmentBlendPipelineD;
  buf[6] = extendedStage;
  size[0] = strlen(vgShaderFragmentPipelineA);
  size[1] = strlen(vgShaderFragmentPipelineB);
  size[2] = strlen(vgShaderFragmentBlendPipelineA);
  size[3] = strlen(vgShaderFragmentBlendPipelineB);
  size[4] = strlen(vgShaderFragmentBlendPipelineC);
  size[5] = strlen(vgShaderFragmentBlendPipelineD);
  size[6] = strlen(extendedStage);
  glShaderSource(context->fs, 7, buf, size);
  glCompileShader(context->fs);
  SH_CHECK_SHADER_COMPILE(context->fs, "pipeline fragment");
  GL_CHECK_ERROR;

  context->progDraw = glCreateProgram();
  glAttachShader(context->progDraw, context->vs);
  glAttachShader(context->progDraw, context->fs);
  glLinkProgram(context->progDraw);
  GL_CHECK_ERROR;

  context->locationDraw.pos            = glGetAttribLocation(context->progDraw,  "pos");
  context->locationDraw.textureUV      = glGetAttribLocation(context->progDraw,  "textureUV");
  context->locationDraw.model          = glGetUniformLocation(context->progDraw, "sh_Model");
  context->locationDraw.projection     = glGetUniformLocation(context->progDraw, "sh_Ortho");
  context->locationDraw.paintInverted  = glGetUniformLocation(context->progDraw, "paintInverted");
  context->locationDraw.drawMode       = glGetUniformLocation(context->progDraw, "drawMode");
  context->locationDraw.imageSampler   = glGetUniformLocation(context->progDraw, "imageSampler");
  context->locationDraw.imagePremultiplied =
    glGetUniformLocation(context->progDraw, "imagePremultiplied");
  context->locationDraw.imageMode      = glGetUniformLocation(context->progDraw, "imageMode");
  context->locationDraw.paintType      = glGetUniformLocation(context->progDraw, "paintType");
  context->locationDraw.rampSampler    = glGetUniformLocation(context->progDraw, "rampSampler");
  context->locationDraw.patternSampler = glGetUniformLocation(context->progDraw, "patternSampler");
  context->locationDraw.paintParams    = glGetUniformLocation(context->progDraw, "paintParams");
  context->locationDraw.paintColor     = glGetUniformLocation(context->progDraw, "paintColor");
  context->locationDraw.scaleFactorBias= glGetUniformLocation(context->progDraw, "scaleFactorBias");
  context->locationDraw.maskEnabled    = glGetUniformLocation(context->progDraw, "maskEnabled");
  context->locationDraw.maskSampler    = glGetUniformLocation(context->progDraw, "maskSampler");
  context->locationDraw.maskSurfaceSize= glGetUniformLocation(context->progDraw, "maskSurfaceSize");
  context->locationDraw.blendMode      = glGetUniformLocation(context->progDraw, "blendMode");
  context->locationDraw.blendSampler   = glGetUniformLocation(context->progDraw, "blendSampler");
  context->locationDraw.blendSurfaceSize= glGetUniformLocation(context->progDraw, "blendSurfaceSize");
  context->locationDraw.coverageEnabled= glGetUniformLocation(context->progDraw, "coverageEnabled");
  context->locationDraw.coverageSampler= glGetUniformLocation(context->progDraw, "coverageSampler");
  context->locationDraw.coverageSurfaceSize= glGetUniformLocation(context->progDraw, "coverageSurfaceSize");
  context->locationDraw.coveragePass   = glGetUniformLocation(context->progDraw, "coveragePass");
  GL_CHECK_ERROR;

  glUseProgram(context->progDraw);
  shApplyColorTransform(context);
  glUniform1i(context->locationDraw.maskEnabled, 0);
  glUniform1i(context->locationDraw.maskSampler, SH_TEXTURE_MASK_INDEX);
  glUniform1i(context->locationDraw.blendMode, 0);
  glUniform1i(context->locationDraw.blendSampler, SH_TEXTURE_BLEND_INDEX);
  glUniform1i(context->locationDraw.coverageEnabled, 0);
  glUniform1i(context->locationDraw.coverageSampler, SH_TEXTURE_COVERAGE_INDEX);
  glUniform1i(context->locationDraw.coveragePass, 0);
  glUniform1i(context->locationDraw.imagePremultiplied, 0);
  GL_CHECK_ERROR;

  /* Initialize uniform variables */
  float mat[16];
  float volume = fmax(context->surfaceWidth, context->surfaceHeight) / 2;
  shCalcOrtho2D(mat, 0, context->surfaceWidth , 0, context->surfaceHeight, -volume, volume);
  glUniformMatrix4fv(context->locationDraw.projection, 1, GL_FALSE, mat);
  glUniform2f(context->locationDraw.maskSurfaceSize,
              (GLfloat)context->surfaceWidth,
              (GLfloat)context->surfaceHeight);
  glUniform2f(context->locationDraw.blendSurfaceSize,
              (GLfloat)context->surfaceWidth,
              (GLfloat)context->surfaceHeight);
  glUniform2f(context->locationDraw.coverageSurfaceSize,
              (GLfloat)context->surfaceWidth,
              (GLfloat)context->surfaceHeight);
  GL_CHECK_ERROR;
}

void shDeinitPiplelineShaders(void){

  VG_GETCONTEXT(VG_NO_RETVAL);
  glDeleteShader(context->vs);
  glDeleteShader(context->fs);
  glDeleteProgram(context->progDraw);
  GL_CHECK_ERROR;
}

void shInitRampShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vgShaderVertexColorRamp, NULL);
  glCompileShader(vs);
  SH_CHECK_SHADER_COMPILE(vs, "ramp vertex");
  GL_CHECK_ERROR;

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &vgShaderFragmentColorRamp, NULL);
  glCompileShader(fs);
  SH_CHECK_SHADER_COMPILE(fs, "ramp fragment");
  GL_CHECK_ERROR;

  context->progColorRamp = glCreateProgram();
  glAttachShader(context->progColorRamp, vs);
  glAttachShader(context->progColorRamp, fs);
  glLinkProgram(context->progColorRamp);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GL_CHECK_ERROR;

  context->locationColorRamp.pos = glGetAttribLocation(context->progColorRamp, "pos");
  context->locationColorRamp.startColor = glGetUniformLocation(context->progColorRamp, "startColor");
  context->locationColorRamp.endColor = glGetUniformLocation(context->progColorRamp, "endColor");
  context->locationColorRamp.startPixel = glGetUniformLocation(context->progColorRamp, "startPixel");
  context->locationColorRamp.pixelSpan = glGetUniformLocation(context->progColorRamp, "pixelSpan");
  GL_CHECK_ERROR;
}

void shDeinitRampShaders(void){
  VG_GETCONTEXT(VG_NO_RETVAL);
  glDeleteProgram(context->progColorRamp);
}

void shInitCoverageShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vgShaderVertexCoverage, NULL);
  glCompileShader(vs);
  SH_CHECK_SHADER_COMPILE(vs, "coverage vertex");
  GL_CHECK_ERROR;

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &vgShaderFragmentCoverage, NULL);
  glCompileShader(fs);
  SH_CHECK_SHADER_COMPILE(fs, "coverage fragment");
  GL_CHECK_ERROR;

  context->progCoverage = glCreateProgram();
  glAttachShader(context->progCoverage, vs);
  glAttachShader(context->progCoverage, fs);
  glLinkProgram(context->progCoverage);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GL_CHECK_ERROR;

  context->locationCoverage.pos =
    glGetAttribLocation(context->progCoverage, "pos");
  context->locationCoverage.targetSize =
    glGetUniformLocation(context->progCoverage, "targetSize");
  context->locationCoverage.sourceSampler =
    glGetUniformLocation(context->progCoverage, "sourceSampler");
  context->locationCoverage.scale =
    glGetUniformLocation(context->progCoverage, "scale");
  GL_CHECK_ERROR;

  glUseProgram(context->progCoverage);
  glUniform1i(context->locationCoverage.sourceSampler, 0);
  GL_CHECK_ERROR;
}

void shDeinitCoverageShaders(void){
  VG_GETCONTEXT(VG_NO_RETVAL);
  glDeleteProgram(context->progCoverage);
}

void shInitImageFilterShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);
  const char* fsSources[4];

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vgShaderVertexImageFilter, NULL);
  glCompileShader(vs);
  SH_CHECK_SHADER_COMPILE(vs, "image filter vertex");
  GL_CHECK_ERROR;

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  fsSources[0] = vgShaderFragmentImageFilterA;
  fsSources[1] = vgShaderFragmentImageFilterB;
  fsSources[2] = vgShaderFragmentImageFilterD;
  fsSources[3] = vgShaderFragmentImageFilterC;
  glShaderSource(fs, 4, fsSources, NULL);
  glCompileShader(fs);
  SH_CHECK_SHADER_COMPILE(fs, "image filter fragment");
  GL_CHECK_ERROR;

  context->progImageFilter = glCreateProgram();
  glAttachShader(context->progImageFilter, vs);
  glAttachShader(context->progImageFilter, fs);
  glLinkProgram(context->progImageFilter);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GL_CHECK_ERROR;

  context->locationImageFilter.pos =
    glGetAttribLocation(context->progImageFilter, "pos");
  context->locationImageFilter.mode =
    glGetUniformLocation(context->progImageFilter, "mode");
  context->locationImageFilter.sourceSampler =
    glGetUniformLocation(context->progImageFilter, "sourceSampler");
  context->locationImageFilter.auxSampler =
    glGetUniformLocation(context->progImageFilter, "auxSampler");
  context->locationImageFilter.targetSize =
    glGetUniformLocation(context->progImageFilter, "targetSize");
  context->locationImageFilter.sourceSize =
    glGetUniformLocation(context->progImageFilter, "sourceSize");
  context->locationImageFilter.sourceOrigin =
    glGetUniformLocation(context->progImageFilter, "sourceOrigin");
  context->locationImageFilter.targetOrigin =
    glGetUniformLocation(context->progImageFilter, "targetOrigin");
  context->locationImageFilter.sourcePremultiplied =
    glGetUniformLocation(context->progImageFilter, "sourcePremultiplied");
  context->locationImageFilter.kernelSize =
    glGetUniformLocation(context->progImageFilter, "kernelSize");
  context->locationImageFilter.shift =
    glGetUniformLocation(context->progImageFilter, "shift");
  context->locationImageFilter.scale =
    glGetUniformLocation(context->progImageFilter, "scale");
  context->locationImageFilter.bias =
    glGetUniformLocation(context->progImageFilter, "bias");
  context->locationImageFilter.tilingMode =
    glGetUniformLocation(context->progImageFilter, "tilingMode");
  context->locationImageFilter.colorMatrix =
    glGetUniformLocation(context->progImageFilter, "colorMatrix[0]");
  context->locationImageFilter.colorBias =
    glGetUniformLocation(context->progImageFilter, "colorBias");
  context->locationImageFilter.tileFillColor =
    glGetUniformLocation(context->progImageFilter, "tileFillColor");
  context->locationImageFilter.sourceLinear =
    glGetUniformLocation(context->progImageFilter, "sourceLinear");
  context->locationImageFilter.filterLinear =
    glGetUniformLocation(context->progImageFilter, "filterLinear");
  context->locationImageFilter.outputLinear =
    glGetUniformLocation(context->progImageFilter, "outputLinear");
  context->locationImageFilter.dstLinear =
    glGetUniformLocation(context->progImageFilter, "dstLinear");
  context->locationImageFilter.premultiplyInput =
    glGetUniformLocation(context->progImageFilter, "premultiplyInput");
  context->locationImageFilter.unpremultiplyOutput =
    glGetUniformLocation(context->progImageFilter, "unpremultiplyOutput");
  context->locationImageFilter.dstStorageMode =
    glGetUniformLocation(context->progImageFilter, "dstStorageMode");
  context->locationImageFilter.lookupSourceChannel =
    glGetUniformLocation(context->progImageFilter, "lookupSourceChannel");
  context->locationImageFilter.blurSize =
    glGetUniformLocation(context->progImageFilter, "blurSize");
  context->locationImageFilter.parametricOffset =
    glGetUniformLocation(context->progImageFilter, "parametricOffset");
  context->locationImageFilter.parametricStrength =
    glGetUniformLocation(context->progImageFilter, "parametricStrength");
  context->locationImageFilter.parametricFlags =
    glGetUniformLocation(context->progImageFilter, "parametricFlags");
  context->locationImageFilter.highlightPaintMode =
    glGetUniformLocation(context->progImageFilter, "highlightPaintMode");
  context->locationImageFilter.shadowPaintMode =
    glGetUniformLocation(context->progImageFilter, "shadowPaintMode");
  context->locationImageFilter.highlightColor =
    glGetUniformLocation(context->progImageFilter, "highlightColor");
  context->locationImageFilter.shadowColor =
    glGetUniformLocation(context->progImageFilter, "shadowColor");
  context->locationImageFilter.highlightSampler =
    glGetUniformLocation(context->progImageFilter, "highlightSampler");
  context->locationImageFilter.shadowSampler =
    glGetUniformLocation(context->progImageFilter, "shadowSampler");
  GL_CHECK_ERROR;

  glUseProgram(context->progImageFilter);
  glUniform1i(context->locationImageFilter.sourceSampler, 0);
  glUniform1i(context->locationImageFilter.auxSampler, 1);
  glUniform1i(context->locationImageFilter.highlightSampler, 2);
  glUniform1i(context->locationImageFilter.shadowSampler, 3);
  GL_CHECK_ERROR;
}

void shDeinitImageFilterShaders(void) {
  VG_GETCONTEXT(VG_NO_RETVAL);

  if (context->progImageFilter != 0) {
    glDeleteProgram(context->progImageFilter);
    context->progImageFilter = 0;
  }
}

VG_API_CALL void vgShaderSourceSH(VGuint shadertype, const VGbyte* string){
    VG_GETCONTEXT(VG_NO_RETVAL);

    switch(shadertype) {
		case VG_FRAGMENT_SHADER_SH:
			context->userShaderFragment = (const void*)string;
			break;
		case VG_VERTEX_SHADER_SH:
			context->userShaderVertex = (const void*)string;
			break;
		default:
			break;
    }
}

VG_API_CALL void vgCompileShaderSH(void){
    shDeinitPiplelineShaders();
    shInitPiplelineShaders();
}

VG_API_CALL void vgUniform1fSH(VGint location, VGfloat v0){
    glUniform1f(location, v0);                                                     
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform2fSH(VGint location, VGfloat v0, VGfloat v1){
    glUniform2f(location, v0, v1);                                         
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform3fSH(VGint location, VGfloat v0, VGfloat v1, VGfloat v2){
    glUniform3f(location, v0, v1, v2);                             
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform4fSH(VGint location, VGfloat v0, VGfloat v1, VGfloat v2, VGfloat v3){
    glUniform4f(location, v0, v1, v2, v3);                 
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform1fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform1fv(location, count, value);                           
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform2fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform2fv(location, count, value);                           
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform3fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform3fv(location, count, value);                           
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform4fvSH(VGint location, VGint count, const VGfloat *value){
    glUniform4fv(location, count, value);                           
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniformMatrix2fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix2fv(location, count, transpose, value);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniformMatrix3fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix3fv(location, count, transpose, value);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniformMatrix4fvSH(VGint location, VGint count, VGboolean transpose, const VGfloat *value){
    glUniformMatrix4fv(location, count, transpose, value);
    GL_CHECK_ERROR;
}

VG_API_CALL VGint vgGetUniformLocationSH(const VGbyte *name){
    VG_GETCONTEXT(-1);
    VGint retval = glGetUniformLocation(context->progDraw, name);
    GL_CHECK_ERROR;
    return retval;
}

VG_API_CALL void vgGetUniformfvSH(VGint location, VGfloat *params){
    VG_GETCONTEXT(VG_NO_RETVAL);
    glGetUniformfv(context->progDraw, location, params);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform1iSH (VGint location, VGint v0){
    glUniform1i (location, v0);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform2iSH (VGint location, VGint v0, VGint v1){
    glUniform2i (location, v0, v1);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform3iSH (VGint location, VGint v0, VGint v1, VGint v2){
    glUniform3i (location,  v0,  v1, v2);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform4iSH (VGint location, VGint v0, VGint v1, VGint v2, VGint v3){
    glUniform4i (location, v0, v1, v2, v3);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform1ivSH (VGint location, VGint count, const VGint *value){
    glUniform1iv (location, count, value);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform2ivSH (VGint location, VGint count, const VGint *value){
    glUniform2iv (location, count, value);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform3ivSH (VGint location, VGint count, const VGint *value){
    glUniform3iv (location, count, value);
    GL_CHECK_ERROR;
}

VG_API_CALL void vgUniform4ivSH (VGint location, VGint count, const VGint *value){
    glUniform4iv (location, count, value);
    GL_CHECK_ERROR;
}
