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

#include "openvg.h"
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

static const char* vgShaderFragmentPipeline =
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
"#define BLEND_MULTIPLY             0x2005\n"
"#define BLEND_SCREEN               0x2006\n"
"#define BLEND_DARKEN               0x2007\n"
"#define BLEND_LIGHTEN              0x2008\n"
"#define BLEND_ADDITIVE             0x2009\n"
"\n"
"in vec2 texImageCoord;\n"
"in vec2 paintCoord;\n"
"\n"
"uniform int drawMode;\n"
"uniform sampler2D imageSampler;\n"
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
"\n"
"out vec4 fragColor;\n"
"vec4 sh_Color;\n"
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
"vec4 applyBlendMode(vec4 src);\n"
"void shMain(void);\n"
"\n"
"void main()\n"
"{\n"
"    vec4 col;\n"
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
"        col = texture(imageSampler, texImageCoord)\n"
"            * (imageMode == DRAW_IMAGE_MULTIPLY ? col : vec4(1.0, 1.0, 1.0, 1.0));\n"
"    }\n"
"    sh_Color = col * scaleFactorBias[0] + scaleFactorBias[1];\n"
"    shMain();\n"
"    if(maskEnabled != 0) {\n"
"        float maskValue = texture(maskSampler, gl_FragCoord.xy / maskSurfaceSize).r;\n"
"        fragColor.a *= maskValue;\n"
"    }\n"
"    fragColor = applyBlendMode(fragColor);\n"
"}\n";

static const char* vgShaderFragmentBlendPipeline =
"vec4 applyBlendMode(vec4 src)\n"
"{\n"
"    if (blendMode == BLEND_MODE_NONE)\n"
"        return src;\n"
"\n"
"    vec4 dst = texture(blendSampler, gl_FragCoord.xy / blendSurfaceSize);\n"
"    vec3 sp = clamp(src.rgb * src.a, 0.0, 1.0);\n"
"    vec3 dp = clamp(dst.rgb, 0.0, 1.0);\n"
"    float sa = clamp(src.a, 0.0, 1.0);\n"
"    float da = clamp(dst.a, 0.0, 1.0);\n"
"    vec3 outRgb = sp;\n"
"    float outA = sa;\n"
"\n"
"    switch (blendMode) {\n"
"    case BLEND_MULTIPLY:\n"
"        outRgb = sp * (1.0 - da) + dp * (1.0 - sa) + sp * dp;\n"
"        outA = sa + da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_SCREEN:\n"
"        outRgb = sp + dp - sp * dp;\n"
"        outA = sa + da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_DARKEN:\n"
"        outRgb = min(sp + dp * (1.0 - sa), dp + sp * (1.0 - da));\n"
"        outA = sa + da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_LIGHTEN:\n"
"        outRgb = max(sp + dp * (1.0 - sa), dp + sp * (1.0 - da));\n"
"        outA = sa + da * (1.0 - sa);\n"
"        break;\n"
"    case BLEND_ADDITIVE:\n"
"        outRgb = min(sp + dp, vec3(1.0));\n"
"        outA = min(sa + da, 1.0);\n"
"        break;\n"
"    default:\n"
"        return src;\n"
"    }\n"
"\n"
"    return vec4(clamp(outRgb, 0.0, 1.0), clamp(outA, 0.0, 1.0));\n"
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
"\n"
"#define STORAGE_RGBA 0\n"
"#define STORAGE_ALPHA 1\n"
"#define STORAGE_LUMINANCE 2\n"
"#define STORAGE_FLOAT 3\n"
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
"void main()\n"
"{\n"
"    ivec2 pixel = ivec2(gl_FragCoord.xy);\n"
"    vec4 source;\n"
"    vec4 result;\n"
"\n"
"    if (mode == FILTER_COLOR_MATRIX) {\n"
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
  const char* buf[3];
  GLint size[3];

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
  buf[0] = vgShaderFragmentPipeline;
  buf[1] = vgShaderFragmentBlendPipeline;
  buf[2] = extendedStage;
  size[0] = strlen(vgShaderFragmentPipeline);
  size[1] = strlen(vgShaderFragmentBlendPipeline);
  size[2] = strlen(extendedStage);
  glShaderSource(context->fs, 3, buf, size);
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
  GL_CHECK_ERROR;

  // TODO: Support color transform to remove this from here
  glUseProgram(context->progDraw);
  GLfloat factor_bias[8] = {1.0,1.0,1.0,1.0,0.0,0.0,0.0,0.0};
  glUniform4fv(context->locationDraw.scaleFactorBias, 2, factor_bias);
  glUniform1i(context->locationDraw.maskEnabled, 0);
  glUniform1i(context->locationDraw.maskSampler, SH_TEXTURE_MASK_INDEX);
  glUniform1i(context->locationDraw.blendMode, 0);
  glUniform1i(context->locationDraw.blendSampler, SH_TEXTURE_BLEND_INDEX);
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

void shInitImageFilterShaders(void) {

  VG_GETCONTEXT(VG_NO_RETVAL);
  const char* fsSources[3];

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vgShaderVertexImageFilter, NULL);
  glCompileShader(vs);
  SH_CHECK_SHADER_COMPILE(vs, "image filter vertex");
  GL_CHECK_ERROR;

  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  fsSources[0] = vgShaderFragmentImageFilterA;
  fsSources[1] = vgShaderFragmentImageFilterB;
  fsSources[2] = vgShaderFragmentImageFilterC;
  glShaderSource(fs, 3, fsSources, NULL);
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
  GL_CHECK_ERROR;

  glUseProgram(context->progImageFilter);
  glUniform1i(context->locationImageFilter.sourceSampler, 0);
  glUniform1i(context->locationImageFilter.auxSampler, 1);
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
