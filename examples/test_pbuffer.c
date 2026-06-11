/*
 * Minimal EGL/OpenVG pbuffer smoke test for ShaderVG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#if defined(__APPLE__)
#  include <OpenGL/gl.h>
#else
#  include <GL/gl.h>
#endif

#include <EGL/egl.h>
#include <VG/openvg.h>
#include <VG/vgext.h>
#include <VG/vgu.h>

#define WARP_TEST_EPSILON 0.001f
#define BLEND_TEST_EPSILON 0.000001f
#define PATH_TEST_EPSILON 0.0001f

static int expect_rgba_at(const VGubyte *data,
                          VGint stride,
                          VGint x,
                          VGint y,
                          VGubyte r,
                          VGubyte g,
                          VGubyte b,
                          VGubyte a,
                          const char *message);

static int expect_channel_between(const VGubyte *data,
                                  VGint stride,
                                  VGint x,
                                  VGint y,
                                  int channel,
                                  VGubyte minValue,
                                  VGubyte maxValue,
                                  const char *message);

static int fail_egl(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
  return 1;
}

static int expect_egl_error(const char *message, EGLint expected)
{
  EGLint error = eglGetError();
  if (error == expected)
    return 0;

  fprintf(stderr, "%s (expected EGL error 0x%04x, got 0x%04x)\n",
          message, expected, error);
  return 1;
}

static int fail_vg(const char *message)
{
  fprintf(stderr, "%s (VG error 0x%04x)\n", message, vgGetError());
  return 1;
}

static int expect_vg_error(const char *message, VGErrorCode expected)
{
  VGErrorCode error = vgGetError();
  if (error == expected)
    return 0;

  fprintf(stderr, "%s (expected VG error 0x%04x, got 0x%04x)\n",
          message, expected, error);
  return 1;
}

static int expect_no_vg_error(const char *message)
{
  VGErrorCode error = vgGetError();
  if (error == VG_NO_ERROR)
    return 0;

  fprintf(stderr, "%s (VG error 0x%04x)\n", message, error);
  return 1;
}

static void *misaligned_pointer(void *storage, size_t storageSize,
                                size_t alignment)
{
  uintptr_t value = (uintptr_t)storage;
  uintptr_t end = value + storageSize;

  if (alignment <= 1)
    return storage;

  while (value < end) {
    if (value % alignment != 0)
      return (void*)value;
    ++value;
  }

  return NULL;
}

static VGPath create_glyph_square(void)
{
  VGPath path;
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat coords[] = {
    0.0f, 0.0f,
    32.0f, 0.0f,
    32.0f, 32.0f,
    0.0f, 32.0f
  };

  path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
                      1.0f, 0.0f, 5, 8, VG_PATH_CAPABILITY_ALL);
  if (path != VG_INVALID_HANDLE)
    vgAppendPathData(path, 5, segments, coords);

  return path;
}

static void draw_retained_path_glyph(void)
{
  VGFont font;
  VGPath path;
  VGPaint paint;
  VGfloat glyphOrigin[] = {0.0f, 0.0f};
  VGfloat escapement[] = {34.0f, 0.0f};
  VGfloat paintColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
  VGfloat drawOrigin[] = {16.0f, 16.0f};

  font = vgCreateFont(1);
  path = create_glyph_square();
  paint = vgCreatePaint();

  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, paintColor);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSetGlyphToPath(font, 1, path, VG_FALSE, glyphOrigin, escapement);
  vgDestroyPath(path);

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetfv(VG_GLYPH_ORIGIN, 2, drawOrigin);
  vgDrawGlyph(font, 1, VG_FILL_PATH, VG_FALSE);

  vgDestroyFont(font);
  vgDestroyPaint(paint);
}

static VGPath create_rect_path(VGfloat width, VGfloat height)
{
  VGPath path;
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat coords[] = {
    0.0f, 0.0f,
    width, 0.0f,
    width, height,
    0.0f, height
  };

  path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
                      1.0f, 0.0f, 5, 8, VG_PATH_CAPABILITY_ALL);
  if (path != VG_INVALID_HANDLE)
    vgAppendPathData(path, 5, segments, coords);

  return path;
}

static void draw_masked_rect(VGPath rect, VGPaint paint,
                             const VGfloat *clearColor,
                             unsigned char *pixels,
                             EGLint width, EGLint height)
{
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
}

static int expect_green_visibility(const unsigned char *pixels,
                                   EGLint width,
                                   EGLint x,
                                   EGLint y,
                                   int visible,
                                   const char *message)
{
  size_t sample = ((size_t)y * (size_t)width + (size_t)x) * 4u;

  if (visible && pixels[sample + 1] >= 128)
    return 0;
  if (!visible && pixels[sample + 1] <= 32)
    return 0;

  fprintf(stderr, "%s\n", message);
  return 1;
}

static int expect_dominant_channel(const unsigned char *pixels,
                                   EGLint width,
                                   EGLint x,
                                   EGLint y,
                                   int channel,
                                   const char *message)
{
  size_t sample = ((size_t)y * (size_t)width + (size_t)x) * 4u;
  unsigned char value = pixels[sample + channel];
  unsigned char other1 = pixels[sample + ((channel + 1) % 3)];
  unsigned char other2 = pixels[sample + ((channel + 2) % 3)];

  if (value >= 160 && other1 <= 128 && other2 <= 128 &&
      pixels[sample + 3] != 0)
    return 0;

  fprintf(stderr, "%s (got %u,%u,%u,%u)\n",
          message,
          pixels[sample + 0], pixels[sample + 1],
          pixels[sample + 2], pixels[sample + 3]);
  return 1;
}

static int run_gradient_ramp_test(unsigned char *pixels,
                                  EGLint width, EGLint height)
{
  VGPaint paint = VG_INVALID_HANDLE;
  VGPath rect = VG_INVALID_HANDLE;
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat linearGradient[] = {
    0.5f, 0.0f,
    (VGfloat)width - 0.5f, 0.0f
  };
  VGfloat rampStops[] = {
    0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    0.5f, 0.0f, 1.0f, 0.0f, 1.0f,
    1.0f, 0.0f, 0.0f, 1.0f, 1.0f
  };
  int result = 0;

  paint = vgCreatePaint();
  rect = create_rect_path((VGfloat)width, (VGfloat)height);
  if (paint == VG_INVALID_HANDLE || rect == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG gradient ramp test setup failed");
    goto cleanup;
  }

  vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_LINEAR_GRADIENT);
  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_SPREAD_MODE,
                  VG_COLOR_RAMP_SPREAD_PAD);
  vgSetParameterfv(paint, VG_PAINT_LINEAR_GRADIENT, 4, linearGradient);
  vgSetParameterfv(paint, VG_PAINT_COLOR_RAMP_STOPS, 15, rampStops);
  vgSetPaint(paint, VG_FILL_PATH);

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (expect_no_vg_error("OpenVG gradient ramp rendering failed") ||
      expect_dominant_channel(pixels, width, 2, height / 2, 0,
                              "OpenVG gradient ramp lost the red stop") ||
      expect_dominant_channel(pixels, width, width / 2, height / 2, 1,
                              "OpenVG gradient ramp lost the green stop") ||
      expect_dominant_channel(pixels, width, width - 3, height / 2, 2,
                              "OpenVG gradient ramp lost the blue stop"))
    result = 1;

cleanup:
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  return result;
}

static int run_image_draw_test(unsigned char *pixels,
                               EGLint width, EGLint height)
{
  VGImage image;
  VGImage patternImage;
  VGPaint paint;
  VGubyte imageData[4 * 4 * 4];
  VGubyte patternData[2 * 2 * 4];
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat paintColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  VGfloat linearGradient[] = {0.0f, 0.0f, 4.0f, 0.0f};
  VGfloat rampStops[] = {
    0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f
  };
  size_t sample = ((size_t)2 * (size_t)width + 2u) * 4u;
  int i;

  for (i=0; i<16; ++i) {
    imageData[i * 4 + 0] = 255;
    imageData[i * 4 + 1] = 0;
    imageData[i * 4 + 2] = 0;
    imageData[i * 4 + 3] = 255;
  }
  for (i=0; i<4; ++i) {
    patternData[i * 4 + 0] = 255;
    patternData[i * 4 + 1] = 255;
    patternData[i * 4 + 2] = 255;
    patternData[i * 4 + 3] = 255;
  }

  image = vgCreateImage(VG_lABGR_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  patternImage = vgCreateImage(VG_lABGR_8888, 2, 2, VG_IMAGE_QUALITY_BETTER);
  paint = vgCreatePaint();
  if (image == VG_INVALID_HANDLE ||
      patternImage == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE)
    return fail_vg("OpenVG image draw test setup failed");

  vgImageSubData(image, imageData, 4 * 4, VG_lABGR_8888, 0, 0, 4, 4);
  vgImageSubData(patternImage, patternData, 2 * 4,
                 VG_lABGR_8888, 0, 0, 2, 2);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, paintColor);
  vgSetPaint(paint, VG_FILL_PATH);

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (pixels[sample] < 128 || pixels[sample + 1] > 32 ||
      pixels[sample + 2] > 32 || pixels[sample + 3] == 0) {
    fprintf(stderr, "OpenVG image drawing did not sample uploaded pixels\n");
    vgDestroyPaint(paint);
    vgDestroyImage(patternImage);
    vgDestroyImage(image);
    return 1;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_MULTIPLY);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (pixels[sample] < 128 || pixels[sample + 1] > 32 ||
      pixels[sample + 2] > 32 || pixels[sample + 3] == 0) {
    fprintf(stderr, "OpenVG multiplied image drawing suppressed uploaded pixels\n");
    vgDestroyPaint(paint);
    vgDestroyImage(patternImage);
    vgDestroyImage(image);
    return 1;
  }

  vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_LINEAR_GRADIENT);
  vgSetParameterfv(paint, VG_PAINT_LINEAR_GRADIENT, 4, linearGradient);
  vgSetParameterfv(paint, VG_PAINT_COLOR_RAMP_STOPS, 10, rampStops);

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (pixels[sample] < 128 || pixels[sample + 1] > 32 ||
      pixels[sample + 2] > 32 || pixels[sample + 3] == 0) {
    fprintf(stderr, "OpenVG gradient-multiplied image drawing suppressed uploaded pixels\n");
    vgDestroyPaint(paint);
    vgDestroyImage(patternImage);
    vgDestroyImage(image);
    return 1;
  }

  vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_PATTERN);
  vgSetParameteri(paint, VG_PAINT_PATTERN_TILING_MODE, VG_TILE_REPEAT);
  vgPaintPattern(paint, patternImage);

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (pixels[sample] < 128 || pixels[sample + 1] > 32 ||
      pixels[sample + 2] > 32 || pixels[sample + 3] == 0) {
    fprintf(stderr, "OpenVG pattern-multiplied image drawing suppressed uploaded pixels\n");
    vgDestroyPaint(paint);
    vgDestroyImage(patternImage);
    vgDestroyImage(image);
    return 1;
  }

  vgDestroyPaint(paint);
  vgDestroyImage(patternImage);
  vgDestroyImage(image);
  return 0;
}

static int run_src_over_alpha_test(unsigned char *pixels,
                                   EGLint width, EGLint height)
{
  VGPath rect = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
  VGfloat paintColor[] = {1.0f, 0.0f, 0.0f, 0.25f};
  size_t sample = ((size_t)(height / 2) * (size_t)width +
                   (size_t)(width / 2)) * 4u;
  int i;
  int result = 0;

  rect = create_rect_path((VGfloat)width, (VGfloat)height);
  paint = vgCreatePaint();
  if (rect == VG_INVALID_HANDLE || paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG source-over alpha test setup failed");
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, paintColor);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();

  for (i=0; i<3; ++i)
    vgDrawPath(rect, VG_FILL_PATH);

  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (expect_no_vg_error("OpenVG source-over alpha test failed")) {
    result = 1;
    goto cleanup;
  }

  if (expect_rgba_at(pixels, width * 4, width / 2, height / 2,
                     255, 0, 0, 147,
                     "OpenVG source-over alpha did not read back straight color")) {
    result = 1;
  } else if (pixels[sample + 3] < 100) {
    fprintf(stderr,
            "OpenVG source-over alpha stayed too low: %u\n",
            pixels[sample + 3]);
    result = 1;
  }

cleanup:
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  return result;
}

static int expect_pixel(const unsigned char *pixels,
                        EGLint width,
                        EGLint x,
                        EGLint y,
                        unsigned char minRed,
                        unsigned char maxGreen,
                        unsigned char maxBlue,
                        const char *message)
{
  size_t sample = ((size_t)y * (size_t)width + (size_t)x) * 4u;

  if (pixels[sample] >= minRed &&
      pixels[sample + 1] <= maxGreen &&
      pixels[sample + 2] <= maxBlue &&
      pixels[sample + 3] != 0)
    return 0;

  fprintf(stderr, "%s\n", message);
  return 1;
}

static void set_rgba(VGubyte *data, VGint stride,
                     VGint x, VGint y,
                     VGubyte r, VGubyte g, VGubyte b, VGubyte a)
{
  size_t offset = (size_t)y * (size_t)stride + (size_t)x * 4u;

  data[offset + 0] = r;
  data[offset + 1] = g;
  data[offset + 2] = b;
  data[offset + 3] = a;
}

static int channel_near(VGubyte actual, VGubyte expected)
{
  int delta = (int)actual - (int)expected;

  if (delta < 0)
    delta = -delta;
  return delta <= 8;
}

static int expect_rgba_at(const VGubyte *data,
                          VGint stride,
                          VGint x,
                          VGint y,
                          VGubyte r,
                          VGubyte g,
                          VGubyte b,
                          VGubyte a,
                          const char *message)
{
  size_t offset = (size_t)y * (size_t)stride + (size_t)x * 4u;

  if (channel_near(data[offset + 0], r) &&
      channel_near(data[offset + 1], g) &&
      channel_near(data[offset + 2], b) &&
      channel_near(data[offset + 3], a))
    return 0;

  fprintf(stderr, "%s (got %u,%u,%u,%u)\n",
          message,
          data[offset + 0], data[offset + 1],
          data[offset + 2], data[offset + 3]);
  return 1;
}

static int run_child_image_filter_test(void)
{
  VGImage sourceParent = VG_INVALID_HANDLE;
  VGImage sourceChild = VG_INVALID_HANDLE;
  VGImage destParent = VG_INVALID_HANDLE;
  VGImage destChild = VG_INVALID_HANDLE;
  VGImage destSmall = VG_INVALID_HANDLE;
  VGImage paramSource = VG_INVALID_HANDLE;
  VGImage blurParent = VG_INVALID_HANDLE;
  VGImage blurChild = VG_INVALID_HANDLE;
  VGImage sharedParent = VG_INVALID_HANDLE;
  VGImage sharedSource = VG_INVALID_HANDLE;
  VGImage sharedOverlap = VG_INVALID_HANDLE;
  VGImage sharedDisjoint = VG_INVALID_HANDLE;
  VGPaint highlightPaint = VG_INVALID_HANDLE;
  VGubyte sourceParentData[5 * 4 * 4];
  VGubyte destParentData[5 * 4 * 4];
  VGubyte smallData[2 * 2 * 4];
  VGubyte readParent[5 * 4 * 4];
  VGubyte readSmall[2 * 2 * 4];
  VGubyte blurData[4 * 3];
  VGubyte sharedData[5 * 2 * 4];
  VGubyte sharedRead[5 * 2 * 4];
  VGubyte redLut[256];
  VGubyte greenLut[256];
  VGubyte blueLut[256];
  VGubyte alphaLut[256];
  VGuint singleLut[256];
  VGfloat identityMatrix[20] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 0.0f
  };
  VGshort childEdgeKernel[] = {0, 0, 1};
  VGshort childIdentityKernel[] = {1};
  VGfloat highlightColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
  int x, y, i;
  int result = 0;

  memset(sourceParentData, 0, sizeof(sourceParentData));
  memset(destParentData, 0, sizeof(destParentData));
  memset(smallData, 0, sizeof(smallData));
  memset(readParent, 0, sizeof(readParent));
  memset(readSmall, 0, sizeof(readSmall));
  memset(blurData, 0, sizeof(blurData));
  memset(sharedData, 0, sizeof(sharedData));
  memset(sharedRead, 0, sizeof(sharedRead));

  for (y=0; y<4; ++y) {
    for (x=0; x<5; ++x) {
      set_rgba(sourceParentData, 5 * 4, x, y, 240, 0, 240, 255);
      set_rgba(destParentData, 5 * 4, x, y, 7, 8, 9, 200);
    }
  }
  set_rgba(sourceParentData, 5 * 4, 1, 1, 10, 20, 30, 255);
  set_rgba(sourceParentData, 5 * 4, 2, 1, 40, 50, 60, 255);
  set_rgba(sourceParentData, 5 * 4, 1, 2, 70, 80, 90, 255);
  set_rgba(sourceParentData, 5 * 4, 2, 2, 100, 110, 120, 255);

  for (y=0; y<2; ++y)
    for (x=0; x<2; ++x)
      set_rgba(smallData, 2 * 4, x, y, 0, 0, 0, 0);

  for (i=0; i<256; ++i) {
    redLut[i] = (VGubyte)(255 - i);
    greenLut[i] = (VGubyte)i;
    blueLut[i] = 0;
    alphaLut[i] = 255;
    singleLut[i] = ((VGuint)i << 24) |
                   ((VGuint)(255 - i) << 16) |
                   ((VGuint)51 << 8) |
                   (VGuint)255;
  }

  sourceParent = vgCreateImage(VG_lABGR_8888, 5, 4,
                               VG_IMAGE_QUALITY_BETTER);
  destParent = vgCreateImage(VG_lABGR_8888, 5, 4,
                             VG_IMAGE_QUALITY_BETTER);
  destSmall = vgCreateImage(VG_lABGR_8888, 2, 2,
                            VG_IMAGE_QUALITY_BETTER);
  paramSource = vgCreateImage(VG_lABGR_8888, 2, 2,
                              VG_IMAGE_QUALITY_BETTER);
  blurParent = vgCreateImage(VG_A_8, 4, 3, VG_IMAGE_QUALITY_BETTER);
  sharedParent = vgCreateImage(VG_lABGR_8888, 5, 2,
                               VG_IMAGE_QUALITY_BETTER);
  highlightPaint = vgCreatePaint();
  if (sourceParent == VG_INVALID_HANDLE ||
      destParent == VG_INVALID_HANDLE ||
      destSmall == VG_INVALID_HANDLE ||
      paramSource == VG_INVALID_HANDLE ||
      blurParent == VG_INVALID_HANDLE ||
      sharedParent == VG_INVALID_HANDLE ||
      highlightPaint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG child image filter test setup failed");
    goto cleanup;
  }

  sourceChild = vgChildImage(sourceParent, 1, 1, 2, 2);
  destChild = vgChildImage(destParent, 2, 1, 2, 2);
  blurChild = vgChildImage(blurParent, 1, 1, 2, 2);
  sharedSource = vgChildImage(sharedParent, 0, 0, 2, 2);
  sharedOverlap = vgChildImage(sharedParent, 1, 0, 2, 2);
  sharedDisjoint = vgChildImage(sharedParent, 3, 0, 2, 2);
  if (sourceChild == VG_INVALID_HANDLE ||
      destChild == VG_INVALID_HANDLE ||
      blurChild == VG_INVALID_HANDLE ||
      sharedSource == VG_INVALID_HANDLE ||
      sharedOverlap == VG_INVALID_HANDLE ||
      sharedDisjoint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG child image filter child setup failed");
    goto cleanup;
  }

  vgSetParameterfv(highlightPaint, VG_PAINT_COLOR, 4, highlightColor);
  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_TRUE);
  vgSeti(VG_FILTER_CHANNEL_MASK,
         VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);

  vgImageSubData(sourceParent, sourceParentData, 5 * 4,
                 VG_lABGR_8888, 0, 0, 5, 4);
  vgImageSubData(destParent, destParentData, 5 * 4,
                 VG_lABGR_8888, 0, 0, 5, 4);
  vgColorMatrix(destChild, sourceChild, identityMatrix);
  vgGetImageSubData(destParent, readParent, 5 * 4,
                    VG_lABGR_8888, 0, 0, 5, 4);
  if (expect_no_vg_error("OpenVG child color matrix filter failed") ||
      expect_rgba_at(readParent, 5 * 4, 2, 1, 10, 20, 30, 255,
                     "OpenVG child filter did not write the child destination origin") ||
      expect_rgba_at(readParent, 5 * 4, 3, 2, 100, 110, 120, 255,
                     "OpenVG child filter did not align child image corners") ||
      expect_rgba_at(readParent, 5 * 4, 1, 1, 7, 8, 9, 200,
                     "OpenVG child filter wrote outside the destination child") ||
      expect_rgba_at(readParent, 5 * 4, 4, 3, 7, 8, 9, 200,
                     "OpenVG child filter modified unrelated parent storage")) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(destSmall, smallData, 2 * 4,
                 VG_lABGR_8888, 0, 0, 2, 2);
  vgLookup(destSmall, sourceChild, redLut, greenLut, blueLut, alphaLut,
           VG_TRUE, VG_FALSE);
  vgGetImageSubData(destSmall, readSmall, 2 * 4,
                    VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG child lookup filter failed") ||
      expect_rgba_at(readSmall, 2 * 4, 0, 0, 245, 20, 0, 255,
                     "OpenVG child lookup sampled outside the child source")) {
    result = 1;
    goto cleanup;
  }

  vgLookupSingle(destSmall, sourceChild, singleLut,
                 VG_GREEN, VG_TRUE, VG_FALSE);
  vgGetImageSubData(destSmall, readSmall, 2 * 4,
                    VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG child lookup single filter failed") ||
      expect_rgba_at(readSmall, 2 * 4, 0, 0, 20, 235, 51, 255,
                     "OpenVG child lookup single used the wrong child pixel")) {
    result = 1;
    goto cleanup;
  }

  vgConvolve(destSmall, sourceChild, 3, 1, 1, 0,
             childEdgeKernel, 1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(destSmall, readSmall, 2 * 4,
                    VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG child convolve filter failed") ||
      expect_rgba_at(readSmall, 2 * 4, 0, 0, 10, 20, 30, 255,
                     "OpenVG child convolve did not pad from the child edge")) {
    result = 1;
    goto cleanup;
  }

  vgSeparableConvolve(destSmall, sourceChild, 3, 1, 1, 0,
                      childEdgeKernel, childIdentityKernel,
                      1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(destSmall, readSmall, 2 * 4,
                    VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG child separable filter failed") ||
      expect_rgba_at(readSmall, 2 * 4, 0, 0, 10, 20, 30, 255,
                     "OpenVG child separable filter did not pad from the child edge")) {
    result = 1;
    goto cleanup;
  }

  blurData[1 * 4 + 2] = 255;
  vgImageSubData(paramSource, smallData, 2 * 4,
                 VG_lABGR_8888, 0, 0, 2, 2);
  vgImageSubData(destSmall, smallData, 2 * 4,
                 VG_lABGR_8888, 0, 0, 2, 2);
  vgImageSubData(blurParent, blurData, 4, VG_A_8, 0, 0, 4, 3);
  vgParametricFilterKHR(destSmall, paramSource, blurChild,
                        1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        highlightPaint, VG_INVALID_HANDLE);
  vgGetImageSubData(destSmall, readSmall, 2 * 4,
                    VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG child parametric filter failed") ||
      expect_rgba_at(readSmall, 2 * 4, 0, 0, 255, 0, 0, 255,
                     "OpenVG child parametric filter ignored the blur child origin")) {
    result = 1;
    goto cleanup;
  }

  for (y=0; y<2; ++y)
    for (x=0; x<5; ++x)
      set_rgba(sharedData, 5 * 4, x, y, 3, 4, 5, 255);
  set_rgba(sharedData, 5 * 4, 0, 0, 11, 22, 33, 255);
  set_rgba(sharedData, 5 * 4, 1, 1, 44, 55, 66, 255);
  vgImageSubData(sharedParent, sharedData, 5 * 4,
                 VG_lABGR_8888, 0, 0, 5, 2);

  vgColorMatrix(sharedOverlap, sharedSource, identityMatrix);
  if (expect_vg_error("OpenVG accepted overlapping child filter operands",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgColorMatrix(sharedDisjoint, sharedSource, identityMatrix);
  vgGetImageSubData(sharedParent, sharedRead, 5 * 4,
                    VG_lABGR_8888, 0, 0, 5, 2);
  if (expect_no_vg_error("OpenVG rejected disjoint child filter operands") ||
      expect_rgba_at(sharedRead, 5 * 4, 3, 0, 11, 22, 33, 255,
                     "OpenVG disjoint child filter did not copy the source") ||
      expect_rgba_at(sharedRead, 5 * 4, 2, 0, 3, 4, 5, 255,
                     "OpenVG disjoint child filter wrote outside the child destination")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);
  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_FALSE);
  if (highlightPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(highlightPaint);
  if (sharedDisjoint != VG_INVALID_HANDLE)
    vgDestroyImage(sharedDisjoint);
  if (sharedOverlap != VG_INVALID_HANDLE)
    vgDestroyImage(sharedOverlap);
  if (sharedSource != VG_INVALID_HANDLE)
    vgDestroyImage(sharedSource);
  if (sharedParent != VG_INVALID_HANDLE)
    vgDestroyImage(sharedParent);
  if (blurChild != VG_INVALID_HANDLE)
    vgDestroyImage(blurChild);
  if (blurParent != VG_INVALID_HANDLE)
    vgDestroyImage(blurParent);
  if (paramSource != VG_INVALID_HANDLE)
    vgDestroyImage(paramSource);
  if (destSmall != VG_INVALID_HANDLE)
    vgDestroyImage(destSmall);
  if (destChild != VG_INVALID_HANDLE)
    vgDestroyImage(destChild);
  if (destParent != VG_INVALID_HANDLE)
    vgDestroyImage(destParent);
  if (sourceChild != VG_INVALID_HANDLE)
    vgDestroyImage(sourceChild);
  if (sourceParent != VG_INVALID_HANDLE)
    vgDestroyImage(sourceParent);
  return result;
}

static int expect_gl_pack_state(GLint alignment,
                                GLint rowLength,
                                GLint skipPixels,
                                GLint skipRows,
                                const char *message)
{
  GLint actualAlignment;
  GLint actualRowLength;
  GLint actualSkipPixels;
  GLint actualSkipRows;

  glGetIntegerv(GL_PACK_ALIGNMENT, &actualAlignment);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &actualRowLength);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &actualSkipPixels);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &actualSkipRows);

  if (actualAlignment == alignment &&
      actualRowLength == rowLength &&
      actualSkipPixels == skipPixels &&
      actualSkipRows == skipRows)
    return 0;

  fprintf(stderr,
          "%s (got alignment=%d rowLength=%d skipPixels=%d skipRows=%d)\n",
          message,
          actualAlignment,
          actualRowLength,
          actualSkipPixels,
          actualSkipRows);
  return 1;
}

static VGPath create_test_path(const VGubyte *segments,
                               VGint segmentCount,
                               const VGfloat *coords)
{
  VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD,
                             VG_PATH_DATATYPE_F,
                             1.0f,
                             0.0f,
                             segmentCount,
                             8,
                             VG_PATH_CAPABILITY_ALL);
  if (path != VG_INVALID_HANDLE)
    vgAppendPathData(path, segmentCount, segments, coords);
  return path;
}

static int expect_float_close(const char *message,
                              VGfloat actual,
                              VGfloat expected)
{
  VGfloat diff = actual - expected;
  if (diff < 0.0f)
    diff = -diff;

  if (diff <= PATH_TEST_EPSILON)
    return 0;

  fprintf(stderr, "%s (expected %.4f, got %.4f)\n",
          message, expected, actual);
  return 1;
}

static int expect_float_between(const char *message,
                                VGfloat actual,
                                VGfloat minValue,
                                VGfloat maxValue)
{
  if (actual >= minValue && actual <= maxValue)
    return 0;

  fprintf(stderr, "%s (expected %.4f..%.4f, got %.4f)\n",
          message, minValue, maxValue, actual);
  return 1;
}

static int expect_path_point(const char *message,
                             VGfloat actualX,
                             VGfloat actualY,
                             VGfloat expectedX,
                             VGfloat expectedY)
{
  if (!expect_float_close(message, actualX, expectedX) &&
      !expect_float_close(message, actualY, expectedY))
    return 0;

  fprintf(stderr, "%s point mismatch\n", message);
  return 1;
}

static int run_path_measurement_test(void)
{
  VGPath line = VG_INVALID_HANDLE;
  VGPath multi = VG_INVALID_HANDLE;
  VGPath rect = VG_INVALID_HANDLE;
  VGPath curve = VG_INVALID_HANDLE;
  VGPath capPath = VG_INVALID_HANDLE;
  VGubyte lineSegments[] = {VG_MOVE_TO_ABS, VG_LINE_TO_ABS};
  VGfloat lineCoords[] = {0.0f, 0.0f, 3.0f, 4.0f};
  VGubyte multiSegments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS
  };
  VGfloat multiCoords[] = {
    0.0f, 0.0f,
    4.0f, 0.0f,
    4.0f, 3.0f
  };
  VGubyte rectSegments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat rectCoords[] = {
    0.0f, 0.0f,
    4.0f, 0.0f,
    4.0f, 3.0f,
    0.0f, 3.0f
  };
  VGubyte curveSegments[] = {VG_MOVE_TO_ABS, VG_QUAD_TO_ABS};
  VGfloat curveCoords[] = {
    0.0f, 0.0f,
    5.0f, 5.0f,
    10.0f, 0.0f
  };
  VGfloat length;
  VGfloat x = -100.0f;
  VGfloat y = -100.0f;
  VGfloat tx = -100.0f;
  VGfloat ty = -100.0f;
  unsigned char misalignedStorage[sizeof(VGfloat) * 3u];
  VGfloat *misaligned = NULL;
  int misalignOffset;
  int result = 0;

  line = create_test_path(lineSegments, 2, lineCoords);
  multi = create_test_path(multiSegments, 3, multiCoords);
  rect = create_test_path(rectSegments, 5, rectCoords);
  curve = create_test_path(curveSegments, 2, curveCoords);
  if (line == VG_INVALID_HANDLE ||
      multi == VG_INVALID_HANDLE ||
      rect == VG_INVALID_HANDLE ||
      curve == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG path measurement setup failed");
    goto cleanup;
  }

  length = vgPathLength(line, 1, 1);
  if (expect_no_vg_error("OpenVG line path length failed") ||
      expect_float_close("OpenVG measured the wrong line length",
                         length, 5.0f)) {
    result = 1;
    goto cleanup;
  }

  vgPointAlongPath(line, 1, 1, 2.5f, &x, &y, &tx, &ty);
  if (expect_no_vg_error("OpenVG line point-along-path failed") ||
      expect_path_point("OpenVG returned the wrong line midpoint",
                        x, y, 1.5f, 2.0f) ||
      expect_path_point("OpenVG returned the wrong line tangent",
                        tx, ty, 0.6f, 0.8f)) {
    result = 1;
    goto cleanup;
  }

  length = vgPathLength(multi, 2, 1);
  vgPointAlongPath(multi, 2, 1, 1.5f, &x, &y, &tx, &ty);
  if (expect_no_vg_error("OpenVG ranged path measurement failed") ||
      expect_float_close("OpenVG measured the wrong ranged segment length",
                         length, 3.0f) ||
      expect_path_point("OpenVG returned the wrong ranged point",
                        x, y, 4.0f, 1.5f) ||
      expect_path_point("OpenVG returned the wrong ranged tangent",
                        tx, ty, 0.0f, 1.0f)) {
    result = 1;
    goto cleanup;
  }

  length = vgPathLength(rect, 4, 1);
  vgPointAlongPath(rect, 4, 1, 1.0f, &x, &y, &tx, &ty);
  if (expect_no_vg_error("OpenVG close-path measurement failed") ||
      expect_float_close("OpenVG measured the wrong close-path length",
                         length, 3.0f) ||
      expect_path_point("OpenVG returned the wrong close-path point",
                        x, y, 0.0f, 2.0f) ||
      expect_path_point("OpenVG returned the wrong close-path tangent",
                        tx, ty, 0.0f, -1.0f)) {
    result = 1;
    goto cleanup;
  }

  length = vgPathLength(curve, 1, 1);
  if (expect_no_vg_error("OpenVG curve path length failed") ||
      expect_float_between("OpenVG returned an implausible curve length",
                           length, 10.0f, 13.0f)) {
    result = 1;
    goto cleanup;
  }

  vgPointAlongPath(line, 1, 1, -2.0f, &x, &y, &tx, &ty);
  if (expect_no_vg_error("OpenVG negative distance clamp failed") ||
      expect_path_point("OpenVG did not clamp negative distance to start",
                        x, y, 0.0f, 0.0f) ||
      expect_path_point("OpenVG returned the wrong start tangent",
                        tx, ty, 0.6f, 0.8f)) {
    result = 1;
    goto cleanup;
  }

  vgPointAlongPath(line, 1, 1, 20.0f, &x, &y, &tx, &ty);
  if (expect_no_vg_error("OpenVG oversized distance clamp failed") ||
      expect_path_point("OpenVG did not clamp oversized distance to end",
                        x, y, 3.0f, 4.0f) ||
      expect_path_point("OpenVG returned the wrong end tangent",
                        tx, ty, 0.6f, 0.8f)) {
    result = 1;
    goto cleanup;
  }

  vgPointAlongPath(line, 1, 1, 1.0f, NULL, NULL, &tx, &ty);
  if (expect_no_vg_error("OpenVG tangent-only point-along-path failed") ||
      expect_path_point("OpenVG returned the wrong tangent-only result",
                        tx, ty, 0.6f, 0.8f)) {
    result = 1;
    goto cleanup;
  }

  vgPointAlongPath(line, 1, 1, 1.0f, NULL, NULL, NULL, NULL);
  if (expect_no_vg_error("OpenVG all-null point-along-path outputs failed")) {
    result = 1;
    goto cleanup;
  }

  length = vgPathLength(VG_INVALID_HANDLE, 0, 1);
  if (length != -1.0f ||
      expect_vg_error("OpenVG accepted an invalid path length handle",
                      VG_BAD_HANDLE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgPathLength(line, -1, 1);
  if (expect_vg_error("OpenVG accepted a negative path length start segment",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgPathLength(line, 1, 0);
  if (expect_vg_error("OpenVG accepted a zero path length segment count",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgPathLength(line, 2, 1);
  if (expect_vg_error("OpenVG accepted an out-of-range path length start segment",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgPointAlongPath(line, 1, 2, 0.0f, &x, &y, NULL, NULL);
  if (expect_vg_error("OpenVG accepted an out-of-range point-along-path segment count",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

#ifdef NAN
  vgPointAlongPath(line, 1, 1, (VGfloat)NAN, &x, &y, NULL, NULL);
  if (expect_vg_error("OpenVG accepted a NaN point-along-path distance",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }
#endif

  for (misalignOffset=1; misalignOffset<(int)sizeof(VGfloat); ++misalignOffset) {
    misaligned = (VGfloat*)(void*)(misalignedStorage + misalignOffset);
    if (((uintptr_t)misaligned % sizeof(VGfloat)) != 0)
      break;
    misaligned = NULL;
  }
  if (misaligned) {
    vgPointAlongPath(line, 1, 1, 0.0f, misaligned, &y, NULL, NULL);
    if (expect_vg_error("OpenVG accepted a misaligned point-along-path output",
                        VG_ILLEGAL_ARGUMENT_ERROR)) {
      result = 1;
      goto cleanup;
    }
  }

  capPath = create_test_path(lineSegments, 2, lineCoords);
  if (capPath == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG path capability test setup failed");
    goto cleanup;
  }
  vgRemovePathCapabilities(capPath, VG_PATH_CAPABILITY_PATH_LENGTH);
  length = vgPathLength(capPath, 1, 1);
  if (length != -1.0f ||
      expect_vg_error("OpenVG ignored missing path length capability",
                      VG_PATH_CAPABILITY_ERROR)) {
    result = 1;
    goto cleanup;
  }
  vgDestroyPath(capPath);
  capPath = create_test_path(lineSegments, 2, lineCoords);
  if (capPath == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG point capability test setup failed");
    goto cleanup;
  }
  vgRemovePathCapabilities(capPath,
                           VG_PATH_CAPABILITY_POINT_ALONG_PATH);
  vgPointAlongPath(capPath, 1, 1, 1.0f, &x, &y, NULL, NULL);
  if (expect_vg_error("OpenVG ignored missing point-along-path capability",
                      VG_PATH_CAPABILITY_ERROR)) {
    result = 1;
    goto cleanup;
  }
  vgPointAlongPath(capPath, 1, 1, 1.0f, NULL, NULL, &tx, &ty);
  if (expect_no_vg_error("OpenVG rejected tangent-only path measurement")) {
    result = 1;
    goto cleanup;
  }
  vgDestroyPath(capPath);
  capPath = create_test_path(lineSegments, 2, lineCoords);
  if (capPath == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG tangent capability test setup failed");
    goto cleanup;
  }
  vgRemovePathCapabilities(capPath,
                           VG_PATH_CAPABILITY_TANGENT_ALONG_PATH);
  vgPointAlongPath(capPath, 1, 1, 1.0f, NULL, NULL, &tx, &ty);
  if (expect_vg_error("OpenVG ignored missing tangent-along-path capability",
                      VG_PATH_CAPABILITY_ERROR)) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if (capPath != VG_INVALID_HANDLE)
    vgDestroyPath(capPath);
  if (curve != VG_INVALID_HANDLE)
    vgDestroyPath(curve);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  if (multi != VG_INVALID_HANDLE)
    vgDestroyPath(multi);
  if (line != VG_INVALID_HANDLE)
    vgDestroyPath(line);
  return result;
}

static VGPath create_fill_rule_test_path(VGboolean innerSameDirection)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH,
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat sameDirectionCoords[] = {
    4.0f, 4.0f,
    36.0f, 4.0f,
    36.0f, 36.0f,
    4.0f, 36.0f,
    14.0f, 14.0f,
    26.0f, 14.0f,
    26.0f, 26.0f,
    14.0f, 26.0f
  };
  VGfloat oppositeDirectionCoords[] = {
    4.0f, 4.0f,
    36.0f, 4.0f,
    36.0f, 36.0f,
    4.0f, 36.0f,
    14.0f, 14.0f,
    14.0f, 26.0f,
    26.0f, 26.0f,
    26.0f, 14.0f
  };

  return create_test_path(segments, 10,
                          innerSameDirection ? sameDirectionCoords :
                                               oppositeDirectionCoords);
}

static int expect_fill_rule_result(const unsigned char *pixels,
                                   EGLint width,
                                   VGboolean centerFilled,
                                   const char *outerMessage,
                                   const char *centerMessage)
{
  if (expect_rgba_at(pixels, width * 4, 8, 8, 0, 255, 0, 255,
                     outerMessage))
    return 1;

  if (centerFilled == VG_TRUE)
    return expect_rgba_at(pixels, width * 4, 20, 20, 0, 255, 0, 255,
                          centerMessage);

  return expect_rgba_at(pixels, width * 4, 20, 20, 0, 0, 0, 255,
                        centerMessage);
}

static int draw_fill_rule_path(VGPath path,
                               VGPaint paint,
                               VGFillRule fillRule,
                               VGboolean antialias,
                               unsigned char *pixels,
                               EGLint width,
                               EGLint height)
{
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_FILL_RULE, fillRule);
  vgSeti(VG_RENDERING_QUALITY,
         antialias ? VG_RENDERING_QUALITY_BETTER :
                     VG_RENDERING_QUALITY_NONANTIALIASED);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgDrawPath(path, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  return expect_no_vg_error("OpenVG fill-rule path draw failed");
}

static int draw_fill_rule_mask(VGPath path,
                               VGPath fullRect,
                               VGPaint paint,
                               VGFillRule fillRule,
                               unsigned char *pixels,
                               EGLint width,
                               EGLint height)
{
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};

  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_TRUE);
  vgSeti(VG_FILL_RULE, fillRule);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK, 0, 0, width, height);
  vgRenderToMask(path, VG_FILL_PATH, VG_SET_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);

  return expect_no_vg_error("OpenVG fill-rule render-to-mask draw failed");
}

static int run_fill_rule_test(unsigned char *pixels,
                              EGLint width,
                              EGLint height)
{
  VGPath sameDirection = VG_INVALID_HANDLE;
  VGPath oppositeDirection = VG_INVALID_HANDLE;
  VGPath fullRect = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGint oldFillRule = vgGeti(VG_FILL_RULE);
  VGint oldRenderingQuality = vgGeti(VG_RENDERING_QUALITY);
  VGint oldMasking = vgGeti(VG_MASKING);
  VGint oldScissoring = vgGeti(VG_SCISSORING);
  int result = 0;

  sameDirection = create_fill_rule_test_path(VG_TRUE);
  oppositeDirection = create_fill_rule_test_path(VG_FALSE);
  fullRect = create_rect_path((VGfloat)width, (VGfloat)height);
  paint = vgCreatePaint();
  if (sameDirection == VG_INVALID_HANDLE ||
      oppositeDirection == VG_INVALID_HANDLE ||
      fullRect == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG fill-rule test setup failed");
    goto cleanup;
  }

  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, green);

  if (draw_fill_rule_path(sameDirection, paint, VG_EVEN_ODD,
                          VG_FALSE, pixels, width, height) ||
      expect_fill_rule_result(pixels, width, VG_FALSE,
                              "OpenVG even-odd fill missed outer coverage",
                              "OpenVG even-odd fill did not cancel the hole")) {
    result = 1;
    goto cleanup;
  }

  if (draw_fill_rule_path(sameDirection, paint, VG_NON_ZERO,
                          VG_FALSE, pixels, width, height) ||
      expect_fill_rule_result(pixels, width, VG_TRUE,
                              "OpenVG nonzero fill missed outer coverage",
                              "OpenVG nonzero fill ignored same-direction winding")) {
    result = 1;
    goto cleanup;
  }

  if (draw_fill_rule_path(oppositeDirection, paint, VG_NON_ZERO,
                          VG_FALSE, pixels, width, height) ||
      expect_fill_rule_result(pixels, width, VG_FALSE,
                              "OpenVG nonzero opposite fill missed outer coverage",
                              "OpenVG nonzero fill ignored opposite winding")) {
    result = 1;
    goto cleanup;
  }

  if (draw_fill_rule_path(sameDirection, paint, VG_NON_ZERO,
                          VG_TRUE, pixels, width, height) ||
      expect_fill_rule_result(pixels, width, VG_TRUE,
                              "OpenVG AA nonzero fill missed outer coverage",
                              "OpenVG AA nonzero fill ignored same winding")) {
    result = 1;
    goto cleanup;
  }

  if (draw_fill_rule_mask(sameDirection, fullRect, paint, VG_NON_ZERO,
                          pixels, width, height) ||
      expect_fill_rule_result(pixels, width, VG_TRUE,
                              "OpenVG nonzero mask missed outer coverage",
                              "OpenVG nonzero mask ignored same winding")) {
    result = 1;
    goto cleanup;
  }

  if (draw_fill_rule_mask(oppositeDirection, fullRect, paint, VG_NON_ZERO,
                          pixels, width, height) ||
      expect_fill_rule_result(pixels, width, VG_FALSE,
                              "OpenVG opposite nonzero mask missed outer coverage",
                              "OpenVG nonzero mask ignored opposite winding")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  vgSeti(VG_MASKING, oldMasking);
  vgSeti(VG_SCISSORING, oldScissoring);
  vgSeti(VG_RENDERING_QUALITY, oldRenderingQuality);
  vgSeti(VG_FILL_RULE, oldFillRule);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (fullRect != VG_INVALID_HANDLE)
    vgDestroyPath(fullRect);
  if (oppositeDirection != VG_INVALID_HANDLE)
    vgDestroyPath(oppositeDirection);
  if (sameDirection != VG_INVALID_HANDLE)
    vgDestroyPath(sameDirection);
  return result;
}

static int run_review_regression_test(void)
{
  VGPaint paint = VG_INVALID_HANDLE;
  VGImage image = VG_INVALID_HANDLE;
  VGImage hugeImage;
  VGubyte imageRead[2 * 2 * 4];
  VGPath dst = VG_INVALID_HANDLE;
  VGPath start = VG_INVALID_HANDLE;
  VGPath end = VG_INVALID_HANDLE;
  VGPath badEnd = VG_INVALID_HANDLE;
  VGPath badCountEnd = VG_INVALID_HANDLE;
  VGubyte dstSegments[] = {VG_MOVE_TO_ABS, VG_LINE_TO_ABS};
  VGfloat dstCoords[] = {100.0f, 100.0f, 120.0f, 100.0f};
  VGubyte startSegments[] = {VG_MOVE_TO_ABS, VG_LINE_TO_ABS};
  VGfloat startCoords[] = {0.0f, 0.0f, 10.0f, 0.0f};
  VGfloat endCoords[] = {0.0f, 0.0f, 20.0f, 0.0f};
  VGubyte badEndSegments[] = {VG_MOVE_TO_ABS, VG_MOVE_TO_ABS};
  VGfloat badEndCoords[] = {0.0f, 0.0f, 20.0f, 0.0f};
  VGubyte badCountEndSegments[] = {VG_MOVE_TO_ABS, VG_CUBIC_TO_ABS};
  VGfloat badCountEndCoords[] = {
    0.0f, 0.0f,
    5.0f, 0.0f, 15.0f, 0.0f, 20.0f, 0.0f
  };
  VGfloat dashPattern[] = {3.0f, 1.0f, 2.0f, 1.0f};
  VGfloat dashRead[4];
  VGfloat *oversizedDashPattern = NULL;
  VGint scissorRects[] = {2, 2, 3, 3, 10, 2, 3, 3};
  VGint scissorRead[8];
  VGint *oversizedScissorRects = NULL;
  VGubyte scissorPixel[4];
  VGfloat opaqueBlack[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat opaqueRed[] = {1.0f, 0.0f, 0.0f, 1.0f};
  VGfloat opaqueGreen[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGfloat minX = -1.0f;
  VGfloat minY = -1.0f;
  VGfloat width = -1.0f;
  VGfloat height = -1.0f;
  VGint segmentCount;
  VGint screenLayout;
  VGint maxDashCount;
  VGint maxScissorRects;
  VGboolean interpolated;
  int i;
  int result = 0;

  paint = vgCreatePaint();
  if (paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG review regression paint setup failed");
    goto cleanup;
  }

  if (vgGetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED) != VG_TRUE ||
      expect_no_vg_error("OpenVG paint premultiplied default query failed")) {
    fprintf(stderr, "OpenVG paint color ramp premultiplied default was not VG_TRUE\n");
    result = 1;
    goto cleanup;
  }

  screenLayout = vgGeti(VG_SCREEN_LAYOUT);
  if (expect_no_vg_error("OpenVG screen layout query failed") ||
      screenLayout < VG_PIXEL_LAYOUT_UNKNOWN ||
      screenLayout > VG_PIXEL_LAYOUT_BGR_HORIZONTAL) {
    fprintf(stderr, "OpenVG screen layout query returned an invalid value\n");
    result = 1;
    goto cleanup;
  }

  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED, VG_FALSE);
  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_SPREAD_MODE,
                  VG_COLOR_RAMP_SPREAD_REFLECT);
  if (expect_no_vg_error("OpenVG paint premultiplied state setup failed") ||
      vgGetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED) != VG_FALSE) {
    fprintf(stderr,
            "OpenVG paint color ramp premultiplied getter returned the wrong state\n");
    result = 1;
    goto cleanup;
  }

  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED, 2);
  if (expect_vg_error("OpenVG accepted an invalid paint premultiplied flag",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }
  if (vgGetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED) != VG_FALSE ||
      expect_no_vg_error("OpenVG paint premultiplied state changed after invalid set")) {
    fprintf(stderr,
            "OpenVG invalid paint premultiplied set changed the stored state\n");
    result = 1;
    goto cleanup;
  }

  maxDashCount = vgGeti(VG_MAX_DASH_COUNT);
  if (expect_no_vg_error("OpenVG dash count limit query failed") ||
      maxDashCount < 16) {
    fprintf(stderr, "OpenVG VG_MAX_DASH_COUNT was below the required minimum\n");
    result = 1;
    goto cleanup;
  }

  oversizedDashPattern =
    (VGfloat*)malloc((size_t)(maxDashCount + 1) * sizeof(VGfloat));
  if (!oversizedDashPattern) {
    result = 1;
    goto cleanup;
  }

  for (i=0; i<=maxDashCount; ++i)
    oversizedDashPattern[i] = 1.0f;

  vgSetfv(VG_STROKE_DASH_PATTERN, 4, dashPattern);
  if (expect_no_vg_error("OpenVG dash pattern setup failed")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_STROKE_DASH_PATTERN,
          maxDashCount + 1,
          oversizedDashPattern);
  if (expect_vg_error("OpenVG accepted too many dash pattern entries",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }
  if (vgGetVectorSize(VG_STROKE_DASH_PATTERN) != 4 ||
      expect_no_vg_error("OpenVG dash pattern size changed after invalid set")) {
    fprintf(stderr,
            "OpenVG invalid dash pattern set changed the stored dash count\n");
    result = 1;
    goto cleanup;
  }
  vgGetfv(VG_STROKE_DASH_PATTERN, 4, dashRead);
  if (expect_no_vg_error("OpenVG dash pattern readback failed") ||
      dashRead[0] != dashPattern[0] ||
      dashRead[1] != dashPattern[1] ||
      dashRead[2] != dashPattern[2] ||
      dashRead[3] != dashPattern[3]) {
    fprintf(stderr,
            "OpenVG invalid dash pattern set changed the stored dash pattern\n");
    result = 1;
    goto cleanup;
  }
  vgSetfv(VG_STROKE_DASH_PATTERN, 0, NULL);
  if (expect_no_vg_error("OpenVG dash pattern reset failed")) {
    result = 1;
    goto cleanup;
  }

  maxScissorRects = vgGeti(VG_MAX_SCISSOR_RECTS);
  if (expect_no_vg_error("OpenVG scissor rect limit query failed") ||
      maxScissorRects < 32) {
    fprintf(stderr,
            "OpenVG VG_MAX_SCISSOR_RECTS was below the required minimum\n");
    result = 1;
    goto cleanup;
  }

  oversizedScissorRects =
    (VGint*)malloc((size_t)(maxScissorRects + 1) * 4u * sizeof(VGint));
  if (!oversizedScissorRects) {
    result = 1;
    goto cleanup;
  }

  for (i=0; i<(maxScissorRects + 1) * 4; ++i)
    oversizedScissorRects[i] = (i % 4 == 2 || i % 4 == 3) ? 1 : 0;

  vgSetiv(VG_SCISSOR_RECTS, 8, scissorRects);
  if (expect_no_vg_error("OpenVG scissor rect setup failed")) {
    result = 1;
    goto cleanup;
  }

  vgSetiv(VG_SCISSOR_RECTS,
          (maxScissorRects + 1) * 4,
          oversizedScissorRects);
  if (expect_vg_error("OpenVG accepted too many scissor rectangles",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }
  if (vgGetVectorSize(VG_SCISSOR_RECTS) != 8 ||
      expect_no_vg_error("OpenVG scissor rect size changed after invalid set")) {
    fprintf(stderr,
            "OpenVG invalid scissor rect set changed the stored count\n");
    result = 1;
    goto cleanup;
  }

  vgGetiv(VG_SCISSOR_RECTS, 8, scissorRead);
  if (expect_no_vg_error("OpenVG scissor rect readback failed") ||
      memcmp(scissorRead, scissorRects, sizeof(scissorRects)) != 0) {
    fprintf(stderr,
            "OpenVG invalid scissor rect set changed the stored rectangles\n");
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, opaqueBlack);
  vgClear(0, 0, 64, 64);
  vgSeti(VG_SCISSORING, VG_TRUE);
  vgSetfv(VG_CLEAR_COLOR, 4, opaqueRed);
  vgClear(0, 0, 64, 64);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgFinish();
  vgReadPixels(scissorPixel, 4, VG_sRGBA_8888, 2, 2, 1, 1);
  if (expect_no_vg_error("OpenVG scissored clear first rect readback failed") ||
      expect_rgba_at(scissorPixel, 4, 0, 0, 255, 0, 0, 255,
                     "OpenVG scissored clear missed the first rectangle")) {
    result = 1;
    goto cleanup;
  }
  vgReadPixels(scissorPixel, 4, VG_sRGBA_8888, 10, 2, 1, 1);
  if (expect_no_vg_error("OpenVG scissored clear second rect readback failed") ||
      expect_rgba_at(scissorPixel, 4, 0, 0, 255, 0, 0, 255,
                     "OpenVG scissored clear missed the second rectangle")) {
    result = 1;
    goto cleanup;
  }
  vgReadPixels(scissorPixel, 4, VG_sRGBA_8888, 7, 2, 1, 1);
  if (expect_no_vg_error("OpenVG scissored clear gap readback failed") ||
      expect_rgba_at(scissorPixel, 4, 0, 0, 0, 0, 0, 255,
                     "OpenVG scissored clear wrote between rectangles")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_SCISSOR_RECTS, 0, NULL);
  vgSeti(VG_SCISSORING, VG_TRUE);
  vgSetfv(VG_CLEAR_COLOR, 4, opaqueGreen);
  vgClear(20, 2, 1, 1);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgFinish();
  vgReadPixels(scissorPixel, 4, VG_sRGBA_8888, 20, 2, 1, 1);
  if (expect_no_vg_error("OpenVG empty scissored clear readback failed") ||
      expect_rgba_at(scissorPixel, 4, 0, 0, 0, 0, 0, 255,
                     "OpenVG empty scissor rectangles did not make clear a no-op")) {
    result = 1;
    goto cleanup;
  }
  vgSetfv(VG_SCISSOR_RECTS, 0, NULL);
  if (expect_no_vg_error("OpenVG scissor rect reset failed")) {
    result = 1;
    goto cleanup;
  }

  vgSetParameteri(paint, VG_PAINT_PATTERN_TILING_MODE, 0x1234);
  if (expect_vg_error("OpenVG accepted an invalid pattern tiling mode",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  image = vgCreateImage(VG_lABGR_8888, 2, 2, VG_IMAGE_QUALITY_BETTER);
  if (image == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG review regression image setup failed");
    goto cleanup;
  }

  memset(imageRead, 0xff, sizeof(imageRead));
  vgGetImageSubData(image, imageRead, 2 * 4, VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG new image zero-readback failed") ||
      expect_rgba_at(imageRead, 2 * 4, 0, 0, 0, 0, 0, 0,
                     "OpenVG did not initialize new images to zero")) {
    result = 1;
    goto cleanup;
  }

  hugeImage = vgCreateImage(VG_lABGR_8888, 65536, 65536,
                            VG_IMAGE_QUALITY_BETTER);
  if (hugeImage != VG_INVALID_HANDLE) {
    vgDestroyImage(hugeImage);
    fprintf(stderr, "OpenVG created an oversized image\n");
    result = 1;
    goto cleanup;
  }
  if (expect_vg_error("OpenVG reported the wrong oversized image error",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  dst = create_test_path(dstSegments, 2, dstCoords);
  start = create_test_path(startSegments, 2, startCoords);
  end = create_test_path(startSegments, 2, endCoords);
  badEnd = create_test_path(badEndSegments, 2, badEndCoords);
  badCountEnd = create_test_path(badCountEndSegments, 2, badCountEndCoords);
  if (dst == VG_INVALID_HANDLE ||
      start == VG_INVALID_HANDLE ||
      end == VG_INVALID_HANDLE ||
      badEnd == VG_INVALID_HANDLE ||
      badCountEnd == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG review regression path setup failed");
    goto cleanup;
  }

  interpolated = vgInterpolatePath(dst, start, end, 0.5f);
  segmentCount = vgGetParameteri(dst, VG_PATH_NUM_SEGMENTS);
  vgPathBounds(dst, &minX, &minY, &width, &height);
  if (expect_no_vg_error("OpenVG path interpolation failed") ||
      !interpolated ||
      segmentCount != 4 ||
      !channel_near((VGubyte)minX, 0) ||
      !channel_near((VGubyte)minY, 0) ||
      !channel_near((VGubyte)width, 120) ||
      !channel_near((VGubyte)height, 100)) {
    fprintf(stderr,
            "OpenVG vgInterpolatePath did not append without corrupting the destination\n");
    result = 1;
    goto cleanup;
  }

  interpolated = vgInterpolatePath(dst, start, badEnd, 0.5f);
  if (expect_no_vg_error("OpenVG incompatible path interpolation raised an error") ||
      interpolated ||
      vgGetParameteri(dst, VG_PATH_NUM_SEGMENTS) != segmentCount) {
    fprintf(stderr,
            "OpenVG vgInterpolatePath changed the destination on incompatible input\n");
    result = 1;
    goto cleanup;
  }

  interpolated = vgInterpolatePath(dst, start, badCountEnd, 0.5f);
  if (expect_no_vg_error("OpenVG count-mismatched path interpolation raised an error") ||
      interpolated ||
      vgGetParameteri(dst, VG_PATH_NUM_SEGMENTS) != segmentCount) {
    fprintf(stderr,
            "OpenVG vgInterpolatePath changed the destination on count-mismatched input\n");
    result = 1;
    goto cleanup;
  }

cleanup:
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSetfv(VG_SCISSOR_RECTS, 0, NULL);
  vgSetfv(VG_STROKE_DASH_PATTERN, 0, NULL);
  vgGetError();
  free(oversizedScissorRects);
  free(oversizedDashPattern);
  if (badCountEnd != VG_INVALID_HANDLE)
    vgDestroyPath(badCountEnd);
  if (badEnd != VG_INVALID_HANDLE)
    vgDestroyPath(badEnd);
  if (end != VG_INVALID_HANDLE)
    vgDestroyPath(end);
  if (start != VG_INVALID_HANDLE)
    vgDestroyPath(start);
  if (dst != VG_INVALID_HANDLE)
    vgDestroyPath(dst);
  if (image != VG_INVALID_HANDLE)
    vgDestroyImage(image);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  return result;
}

static int run_image_filter_test(void)
{
  VGImage source = VG_INVALID_HANDLE;
  VGImage dest = VG_INVALID_HANDLE;
  VGImage blur = VG_INVALID_HANDLE;
  VGImage alphaDest = VG_INVALID_HANDLE;
  VGImage lumaDest = VG_INVALID_HANDLE;
  VGPaint highlightPaint = VG_INVALID_HANDLE;
  VGPaint shadowPaint = VG_INVALID_HANDLE;
  VGPaint gradientPaint = VG_INVALID_HANDLE;
  VGPaint invalidFilterPaint = VG_INVALID_HANDLE;
  VGubyte sourceData[4 * 4 * 4];
  VGubyte destData[4 * 4 * 4];
  VGubyte readData[4 * 4 * 4];
  VGubyte blurData[4 * 4];
  VGubyte alphaData[2 * 2];
  VGubyte alphaRead[2 * 2];
  VGubyte lumaData[2 * 2];
  VGubyte lumaRead[2 * 2];
  VGubyte redLut[256];
  VGubyte greenLut[256];
  VGubyte blueLut[256];
  VGubyte alphaLut[256];
  VGuint singleLut[256];
  VGfloat matrix[20] = {
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 0.0f
  };
  VGfloat identityMatrix[20] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 0.0f
  };
  VGshort convolveKernel[] = {1, 0, 0, 0};
  VGshort horizontalKernel[] = {0, 1, 0};
  VGshort verticalKernel[] = {0, 1, 0};
  VGshort asymmetricKernel[] = {0, 0, 1, 0, 0, 0};
  VGshort separableKernelX[] = {1, 0};
  VGshort separableKernelY[] = {1};
  VGfloat tileFill[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat redPaintColor[] = {1.0f, 0.0f, 0.0f, 1.0f};
  VGfloat badGradientStops[] = {
    0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 0.0f, 0.0f, 1.0f
  };
  VGfloat goodGradientStops[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f, 0.0f, 1.0f
  };
  const char *extensions;
  VGint maxKernelSize;
  VGint maxAverageDimension;
  VGint averageDimensionResolution;
  VGint maxAverageIterations;
  size_t center;
  size_t adjacent;
  int i;
  int result = 0;

  memset(sourceData, 0, sizeof(sourceData));
  memset(destData, 0, sizeof(destData));
  memset(readData, 0, sizeof(readData));
  memset(blurData, 0, sizeof(blurData));
  memset(alphaData, 64, sizeof(alphaData));
  memset(alphaRead, 0, sizeof(alphaRead));
  memset(lumaData, 0, sizeof(lumaData));
  memset(lumaRead, 0, sizeof(lumaRead));

  for (i=0; i<16; ++i) {
    set_rgba(sourceData, 4 * 4, i % 4, i / 4, 0, 0, 0, 255);
    set_rgba(destData, 4 * 4, i % 4, i / 4, 1, 2, 3, 200);
  }
  set_rgba(sourceData, 4 * 4, 0, 0, 10, 20, 30, 255);
  set_rgba(sourceData, 4 * 4, 1, 0, 100, 0, 0, 255);
  set_rgba(sourceData, 4 * 4, 0, 1, 0, 80, 0, 255);
  set_rgba(sourceData, 4 * 4, 1, 1, 255, 0, 0, 255);

  for (i=0; i<256; ++i) {
    redLut[i] = (VGubyte)(255 - i);
    greenLut[i] = (VGubyte)i;
    blueLut[i] = 0;
    alphaLut[i] = 255;
    singleLut[i] = ((VGuint)i << 24) |
                   ((VGuint)(255 - i) << 16) |
                   ((VGuint)51 << 8) |
                   (VGuint)255;
  }

  source = vgCreateImage(VG_lABGR_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  dest = vgCreateImage(VG_lABGR_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  blur = vgCreateImage(VG_A_8, 4, 4, VG_IMAGE_QUALITY_BETTER);
  alphaDest = vgCreateImage(VG_A_8, 2, 2, VG_IMAGE_QUALITY_BETTER);
  lumaDest = vgCreateImage(VG_lL_8, 2, 2, VG_IMAGE_QUALITY_BETTER);
  highlightPaint = vgCreatePaint();
  shadowPaint = vgCreatePaint();
  gradientPaint = vgCreatePaint();
  invalidFilterPaint = vgCreatePaint();
  if (source == VG_INVALID_HANDLE ||
      dest == VG_INVALID_HANDLE ||
      blur == VG_INVALID_HANDLE ||
      alphaDest == VG_INVALID_HANDLE ||
      lumaDest == VG_INVALID_HANDLE ||
      highlightPaint == VG_INVALID_HANDLE ||
      shadowPaint == VG_INVALID_HANDLE ||
      gradientPaint == VG_INVALID_HANDLE ||
      invalidFilterPaint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG image filter test setup failed");
    goto cleanup;
  }

  vgSetColor(highlightPaint, 0xff0000ff);
  vgSetColor(shadowPaint, 0x00ff00ff);
  if (vgGetColor(highlightPaint) != 0xff0000ff ||
      expect_no_vg_error("OpenVG paint color helper test failed")) {
    fprintf(stderr, "OpenVG vgSetColor/vgGetColor round trip failed\n");
    result = 1;
    goto cleanup;
  }
  vgSetParameteri(gradientPaint, VG_PAINT_TYPE,
                  VG_PAINT_TYPE_LINEAR_GRADIENT);
  vgSetParameterfv(gradientPaint, VG_PAINT_COLOR_RAMP_STOPS,
                   10, goodGradientStops);
  vgSetParameteri(invalidFilterPaint, VG_PAINT_TYPE,
                  VG_PAINT_TYPE_RADIAL_GRADIENT);

  maxKernelSize = vgGeti(VG_MAX_KERNEL_SIZE);
  if (expect_no_vg_error("OpenVG image filter limit query failed") ||
      maxKernelSize < 2) {
    fprintf(stderr, "OpenVG reported an unusable convolution kernel limit\n");
    result = 1;
    goto cleanup;
  }

  extensions = (const char*)vgGetString(VG_EXTENSIONS);
  if (!extensions ||
      !strstr(extensions, "VG_KHR_iterative_average_blur") ||
      !strstr(extensions, "VG_KHR_parametric_filter")) {
    fprintf(stderr,
            "OpenVG did not advertise expected image filter extensions\n");
    result = 1;
    goto cleanup;
  }

  maxAverageDimension = vgGeti(VG_MAX_AVERAGE_BLUR_DIMENSION_KHR);
  averageDimensionResolution =
    vgGeti(VG_AVERAGE_BLUR_DIMENSION_RESOLUTION_KHR);
  maxAverageIterations = vgGeti(VG_MAX_AVERAGE_BLUR_ITERATIONS_KHR);
  if (expect_no_vg_error("OpenVG iterative average blur limit query failed") ||
      maxAverageDimension < 128 ||
      averageDimensionResolution != -1 ||
      maxAverageIterations < 3) {
    fprintf(stderr,
            "OpenVG reported unusable iterative average blur limits\n");
    result = 1;
    goto cleanup;
  }

  if (vgGetVectorSize(VG_MAX_AVERAGE_BLUR_DIMENSION_KHR) != 1 ||
      vgGetVectorSize(VG_AVERAGE_BLUR_DIMENSION_RESOLUTION_KHR) != 1 ||
      vgGetVectorSize(VG_MAX_AVERAGE_BLUR_ITERATIONS_KHR) != 1 ||
      expect_no_vg_error("OpenVG iterative average blur vector size query failed")) {
    fprintf(stderr,
            "OpenVG reported the wrong iterative average blur vector size\n");
    result = 1;
    goto cleanup;
  }

  if (run_child_image_filter_test()) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(source, sourceData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgImageSubData(dest, destData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgImageSubData(alphaDest, alphaData, 2, VG_A_8, 0, 0, 2, 2);
  vgImageSubData(lumaDest, lumaData, 2, VG_lL_8, 0, 0, 2, 2);
  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_TRUE);
  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_BLUE);
  vgColorMatrix(dest, source, matrix);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgColorMatrix failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 30, 2, 10, 200,
                     "OpenVG vgColorMatrix did not honor channel masking")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);
  vgLookup(dest, source, redLut, greenLut, blueLut, alphaLut,
           VG_TRUE, VG_FALSE);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgLookup failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 245, 20, 0, 255,
                     "OpenVG vgLookup produced the wrong mapped pixel")) {
    result = 1;
    goto cleanup;
  }

  vgLookupSingle(dest, source, singleLut, VG_GREEN, VG_TRUE, VG_FALSE);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgLookupSingle failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 20, 235, 51, 255,
                     "OpenVG vgLookupSingle used the wrong source channel")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_TILE_FILL_COLOR, 4, tileFill);
  vgConvolve(dest, source, 2, 2, 0, 0,
             convolveKernel, 1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgConvolve failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 255, 0, 0, 255,
                     "OpenVG vgConvolve did not apply the flipped column-major kernel")) {
    result = 1;
    goto cleanup;
  }

  vgConvolve(dest, source, 3, 1, 0, 0,
             horizontalKernel, 1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG horizontal vgConvolve failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 100, 0, 0, 255,
                     "OpenVG vgConvolve mishandled a 3x1 kernel")) {
    result = 1;
    goto cleanup;
  }

  vgConvolve(dest, source, 1, 3, 0, 0,
             verticalKernel, 1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vertical vgConvolve failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 0, 80, 0, 255,
                     "OpenVG vgConvolve mishandled a 1x3 kernel")) {
    result = 1;
    goto cleanup;
  }

  vgConvolve(dest, source, 2, 3, 0, 0,
             asymmetricKernel, 1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG asymmetric vgConvolve failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 100, 0, 0, 255,
                     "OpenVG vgConvolve did not use column-major kernel storage")) {
    result = 1;
    goto cleanup;
  }

  vgSeparableConvolve(dest, source, 2, 1, 0, 0,
                      separableKernelX, separableKernelY,
                      1.0f, 0.0f, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgSeparableConvolve failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 0, 100, 0, 0, 255,
                     "OpenVG vgSeparableConvolve did not apply the flipped kernel")) {
    result = 1;
    goto cleanup;
  }

  vgGaussianBlur(dest, source, 1.0f, 1.0f, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgGaussianBlur failed")) {
    result = 1;
    goto cleanup;
  }

  center = ((size_t)1 * 4u + 1u) * 4u;
  adjacent = ((size_t)1 * 4u + 2u) * 4u;
  if (readData[center] <= readData[adjacent] ||
      readData[adjacent] == 0 ||
      readData[center + 3] == 0) {
    fprintf(stderr,
            "OpenVG vgGaussianBlur did not spread the source impulse as expected\n");
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE);
  vgColorMatrix(alphaDest, source, identityMatrix);
  vgGetImageSubData(alphaDest, alphaRead, 2, VG_A_8, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG A_8 channel mask filter failed") ||
      !channel_near(alphaRead[0], 64)) {
    fprintf(stderr,
            "OpenVG image filter modified A_8 without VG_ALPHA in the mask\n");
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_FILTER_CHANNEL_MASK, VG_ALPHA);
  vgColorMatrix(alphaDest, source, identityMatrix);
  vgGetImageSubData(alphaDest, alphaRead, 2, VG_A_8, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG A_8 alpha filter failed") ||
      !channel_near(alphaRead[0], 255)) {
    fprintf(stderr,
            "OpenVG image filter did not update A_8 through VG_ALPHA\n");
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_FILTER_CHANNEL_MASK, 0);
  vgColorMatrix(lumaDest, source, identityMatrix);
  vgGetImageSubData(lumaDest, lumaRead, 2, VG_lL_8, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG luminance filter failed") ||
      !channel_near(lumaRead[0], 19)) {
    fprintf(stderr,
            "OpenVG image filter did not update luminance destinations\n");
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);

  memset(sourceData, 0, sizeof(sourceData));
  for (i=0; i<16; ++i)
    set_rgba(sourceData, 4 * 4, i % 4, i / 4, 0, 0, 0, 255);
  set_rgba(sourceData, 4 * 4, 1, 1, 255, 0, 0, 255);
  vgImageSubData(source, sourceData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);

  vgIterativeAverageBlurKHR(dest, source, 3.0f, 1.0f, 1, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgIterativeAverageBlurKHR horizontal blur failed") ||
      expect_rgba_at(readData, 4 * 4, 0, 1, 85, 0, 0, 255,
                     "OpenVG iterative average blur did not spread left") ||
      expect_rgba_at(readData, 4 * 4, 1, 1, 85, 0, 0, 255,
                     "OpenVG iterative average blur produced the wrong center") ||
      expect_rgba_at(readData, 4 * 4, 2, 1, 85, 0, 0, 255,
                     "OpenVG iterative average blur did not spread right") ||
      expect_rgba_at(readData, 4 * 4, 3, 1, 0, 0, 0, 255,
                     "OpenVG iterative average blur spread too far")) {
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(dest, source, 1.0f, 3.0f, 1, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgIterativeAverageBlurKHR vertical blur failed") ||
      expect_rgba_at(readData, 4 * 4, 1, 0, 85, 0, 0, 255,
                     "OpenVG iterative average blur did not spread down") ||
      expect_rgba_at(readData, 4 * 4, 1, 2, 85, 0, 0, 255,
                     "OpenVG iterative average blur did not spread up")) {
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(dest, source, 1.0f, 1.0f, 0, VG_TILE_PAD);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgIterativeAverageBlurKHR identity blur failed") ||
      expect_rgba_at(readData, 4 * 4, 1, 1, 255, 0, 0, 255,
                     "OpenVG iterative average blur did not preserve identity") ||
      expect_rgba_at(readData, 4 * 4, 0, 1, 0, 0, 0, 255,
                     "OpenVG iterative average identity blur modified neighbors")) {
    result = 1;
    goto cleanup;
  }

  memset(sourceData, 0, sizeof(sourceData));
  memset(destData, 0, sizeof(destData));
  memset(blurData, 0, sizeof(blurData));
  for (i=0; i<16; ++i)
    set_rgba(destData, 4 * 4, i % 4, i / 4, 5, 6, 7, 8);
  blurData[1 * 4 + 2] = 255;
  vgImageSubData(source, sourceData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgImageSubData(dest, destData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgImageSubData(blur, blurData, 4, VG_A_8, 0, 0, 4, 4);

  vgSeti(VG_FILTER_CHANNEL_MASK,
         VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);
  vgParametricFilterKHR(dest, source, blur, 1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        highlightPaint, VG_INVALID_HANDLE);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgParametricFilterKHR color paint failed") ||
      expect_rgba_at(readData, 4 * 4, 1, 1, 255, 0, 0, 255,
                     "OpenVG parametric filter did not apply a color highlight")) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(dest, destData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgParametricFilterKHR(dest, source, blur, 1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        gradientPaint, VG_INVALID_HANDLE);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG vgParametricFilterKHR gradient paint failed") ||
      expect_rgba_at(readData, 4 * 4, 1, 1, 255, 0, 0, 255,
                     "OpenVG parametric filter did not sample a gradient paint")) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(dest, destData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgSeti(VG_FILTER_CHANNEL_MASK, VG_ALPHA);
  vgParametricFilterKHR(dest, source, blur, 1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        highlightPaint, VG_INVALID_HANDLE);
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG parametric channel mask failed") ||
      expect_rgba_at(readData, 4 * 4, 1, 1, 5, 6, 7, 255,
                     "OpenVG parametric filter ignored the channel mask")) {
    result = 1;
    goto cleanup;
  }
  vgSeti(VG_FILTER_CHANNEL_MASK,
         VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);

  vgSetParameterfv(gradientPaint, VG_PAINT_COLOR_RAMP_STOPS,
                   10, badGradientStops);
  vgParametricFilterKHR(dest, source, blur, 1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        gradientPaint, VG_INVALID_HANDLE);
  if (expect_vg_error("OpenVG accepted a parametric gradient with opaque first stop",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }
  vgSetParameterfv(gradientPaint, VG_PAINT_COLOR_RAMP_STOPS,
                   10, goodGradientStops);

  vgParametricFilterKHR(dest, source, blur, 1.0f, 1.0f, 0.0f,
                        0x10u, highlightPaint, VG_INVALID_HANDLE);
  if (expect_vg_error("OpenVG accepted invalid parametric filter flags",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgParametricFilterKHR(dest, source, blur, 1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        invalidFilterPaint, VG_INVALID_HANDLE);
  if (expect_vg_error("OpenVG accepted an invalid parametric paint type",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgParametricFilterKHR(source, source, blur, 1.0f, 1.0f, 0.0f,
                        VG_PF_OUTER_FLAG_KHR,
                        highlightPaint, VG_INVALID_HANDLE);
  if (expect_vg_error("OpenVG accepted overlapping parametric operands",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  memset(sourceData, 0, sizeof(sourceData));
  set_rgba(sourceData, 4 * 4, 1, 1, 255, 255, 255, 255);
  vgImageSubData(source, sourceData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  vgImageSubData(dest, destData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 4);
  if (vguDropShadowKHR(dest, source, 3.0f, 1.0f, 1, 1.0f,
                       1.0f, 0.0f, VG_PF_OUTER_FLAG_KHR,
                       VG_IMAGE_QUALITY_BETTER,
                       0x00ff00ff) != VGU_NO_ERROR) {
    fprintf(stderr, "OpenVG VGU drop shadow helper failed\n");
    result = 1;
    goto cleanup;
  }
  vgGetImageSubData(dest, readData, 4 * 4,
                    VG_lABGR_8888, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG VGU drop shadow readback failed") ||
      readData[((size_t)1 * 4u + 2u) * 4u + 1] == 0 ||
      readData[((size_t)1 * 4u + 2u) * 4u + 3] == 0) {
    fprintf(stderr, "OpenVG VGU drop shadow did not produce a visible shadow\n");
    result = 1;
    goto cleanup;
  }

  if (vguDropShadowKHR(dest, VG_INVALID_HANDLE, 3.0f, 1.0f, 1, 1.0f,
                       1.0f, 0.0f, VG_PF_OUTER_FLAG_KHR,
                       VG_IMAGE_QUALITY_BETTER,
                       0x00ff00ff) != VGU_BAD_HANDLE_ERROR) {
    fprintf(stderr, "OpenVG VGU drop shadow did not map bad handles\n");
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(dest, source, -1.0f, 1.0f, 1, VG_TILE_PAD);
  if (expect_vg_error("OpenVG accepted a negative iterative average blur dimension",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(dest, source,
                            (VGfloat)maxAverageDimension + 1.0f,
                            1.0f, 1, VG_TILE_PAD);
  if (expect_vg_error("OpenVG accepted an oversized iterative average blur dimension",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(dest, source, 1.0f, 1.0f,
                            (VGuint)maxAverageIterations + 1u,
                            VG_TILE_PAD);
  if (expect_vg_error("OpenVG accepted too many iterative average blur iterations",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(dest, source, 1.0f, 1.0f, 1,
                            (VGTilingMode)0x1234);
  if (expect_vg_error("OpenVG accepted an invalid iterative average blur tiling mode",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgIterativeAverageBlurKHR(source, source, 1.0f, 1.0f, 1,
                            VG_TILE_PAD);
  if (expect_vg_error("OpenVG accepted overlapping iterative average blur operands",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgGaussianBlur(dest, source, 0.0f, 1.0f, VG_TILE_PAD);
  if (expect_vg_error("OpenVG accepted zero Gaussian blur deviation",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgColorMatrix(source, source, matrix);
  if (expect_vg_error("OpenVG accepted overlapping image filter operands",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgConvolve(dest, source, 0, 1, 0, 0,
             convolveKernel, 1.0f, 0.0f, VG_TILE_PAD);
  if (expect_vg_error("OpenVG accepted an invalid convolution kernel size",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgLookup(dest, source, NULL, greenLut, blueLut, alphaLut,
           VG_TRUE, VG_FALSE);
  if (expect_vg_error("OpenVG accepted a null lookup table",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

cleanup:
  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);
  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_FALSE);
  if (invalidFilterPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(invalidFilterPaint);
  if (gradientPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(gradientPaint);
  if (shadowPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(shadowPaint);
  if (highlightPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(highlightPaint);
  if (lumaDest != VG_INVALID_HANDLE)
    vgDestroyImage(lumaDest);
  if (alphaDest != VG_INVALID_HANDLE)
    vgDestroyImage(alphaDest);
  if (blur != VG_INVALID_HANDLE)
    vgDestroyImage(blur);
  if (dest != VG_INVALID_HANDLE)
    vgDestroyImage(dest);
  if (source != VG_INVALID_HANDLE)
    vgDestroyImage(source);
  return result;
}

static VGfloat clamp_unit(VGfloat value)
{
  if (value < 0.0f)
    return 0.0f;
  if (value > 1.0f)
    return 1.0f;
  return value;
}

static VGfloat min_float(VGfloat a, VGfloat b)
{
  return (a < b) ? a : b;
}

static VGfloat max_float(VGfloat a, VGfloat b)
{
  return (a > b) ? a : b;
}

static VGubyte unit_to_byte(VGfloat value)
{
  return (VGubyte)(clamp_unit(value) * 255.0f + 0.5f);
}

static void premultiply_color(const VGfloat in[4], VGfloat out[4])
{
  out[0] = in[0] * in[3];
  out[1] = in[1] * in[3];
  out[2] = in[2] * in[3];
  out[3] = in[3];
}

static void store_straight_rgba(const VGfloat color[4], VGubyte out[4])
{
  if (color[3] <= 0.0f) {
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
  } else {
    out[0] = unit_to_byte(color[0] / color[3]);
    out[1] = unit_to_byte(color[1] / color[3]);
    out[2] = unit_to_byte(color[2] / color[3]);
  }
  out[3] = unit_to_byte(color[3]);
}

static VGfloat safe_divide_float(VGfloat numerator, VGfloat denominator)
{
  if (denominator == 0.0f)
    return 0.0f;
  return numerator / denominator;
}

static int blend_definitely_less(VGfloat a, VGfloat b)
{
  return a < b - BLEND_TEST_EPSILON;
}

static int blend_definitely_greater(VGfloat a, VGfloat b)
{
  return a > b + BLEND_TEST_EPSILON;
}

static VGfloat premultiplied_common_channel(VGfloat src,
                                            VGfloat dst,
                                            VGfloat srcAlpha,
                                            VGfloat dstAlpha)
{
  return src * (1.0f - dstAlpha) + dst * (1.0f - srcAlpha);
}

static VGfloat blend_overlay_channel(VGfloat src,
                                     VGfloat dst,
                                     VGfloat srcAlpha,
                                     VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);

  if (blend_definitely_less(2.0f * dst, dstAlpha))
    return 2.0f * src * dst + common;

  return srcAlpha * dstAlpha -
         2.0f * (dstAlpha - dst) * (srcAlpha - src) + common;
}

static VGfloat blend_hardlight_channel(VGfloat src,
                                       VGfloat dst,
                                       VGfloat srcAlpha,
                                       VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);

  if (blend_definitely_less(2.0f * src, srcAlpha))
    return 2.0f * src * dst + common;

  return srcAlpha * dstAlpha -
         2.0f * (dstAlpha - dst) * (srcAlpha - src) + common;
}

static VGfloat blend_softlight_svg_channel(VGfloat src,
                                           VGfloat dst,
                                           VGfloat srcAlpha,
                                           VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat dstRatio = safe_divide_float(dst, dstAlpha);

  if (blend_definitely_less(2.0f * src, srcAlpha)) {
    return dst * (srcAlpha -
                  (1.0f - dstRatio) * (2.0f * src - srcAlpha)) + common;
  }

  if (blend_definitely_less(8.0f * dst, dstAlpha)) {
    return dst * (srcAlpha -
                  (1.0f - dstRatio) * (2.0f * src - srcAlpha) *
                  (3.0f - 8.0f * dstRatio)) + common;
  }

  return dst * srcAlpha +
         (sqrtf(dstRatio) * dstAlpha - dst) *
         (2.0f * src - srcAlpha) + common;
}

static VGfloat blend_softlight_channel(VGfloat src,
                                       VGfloat dst,
                                       VGfloat srcAlpha,
                                       VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat dstRatio = safe_divide_float(dst, dstAlpha);

  if (blend_definitely_less(2.0f * src, srcAlpha)) {
    return dst * srcAlpha +
           dst * (1.0f - dstRatio) * (2.0f * src - srcAlpha) + common;
  }

  if (blend_definitely_less(4.0f * dst, dstAlpha)) {
    return dst * srcAlpha +
           dst * ((16.0f * dstRatio - 12.0f) * dstRatio + 3.0f) *
           (2.0f * src - srcAlpha) + common;
  }

  return dst * srcAlpha +
         (sqrtf(dstRatio) * dstAlpha - dst) *
         (2.0f * src - srcAlpha) + common;
}

static VGfloat blend_color_dodge_channel(VGfloat src,
                                         VGfloat dst,
                                         VGfloat srcAlpha,
                                         VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat srcRatio = safe_divide_float(src, srcAlpha);
  VGfloat srcDstAlpha = srcAlpha * dstAlpha;

  if (!blend_definitely_less(src, srcAlpha))
    return srcDstAlpha + common;

  return min_float(srcDstAlpha,
                   safe_divide_float(dst * srcAlpha,
                                     1.0f - srcRatio)) + common;
}

static VGfloat blend_color_burn_channel(VGfloat src,
                                        VGfloat dst,
                                        VGfloat srcAlpha,
                                        VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat srcDstAlpha = srcAlpha * dstAlpha;

  if (!blend_definitely_greater(src, 0.0f))
    return common;

  return srcDstAlpha -
         min_float(srcDstAlpha,
                   safe_divide_float(srcAlpha * srcAlpha *
                                     (dstAlpha - dst), src)) + common;
}

static VGfloat blend_vivid_light_channel(VGfloat src,
                                         VGfloat dst,
                                         VGfloat srcAlpha,
                                         VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat srcDstAlpha = srcAlpha * dstAlpha;
  VGfloat srcRatio = safe_divide_float(src, srcAlpha);

  if (blend_definitely_less(2.0f * src, srcAlpha)) {
    if (!blend_definitely_greater(src, 0.0f))
      return common;
    return srcDstAlpha -
           min_float(srcDstAlpha,
                     safe_divide_float(srcAlpha * srcAlpha *
                                       (dstAlpha - dst),
                                       2.0f * src)) + common;
  }

  if (!blend_definitely_less(src, srcAlpha))
    return srcDstAlpha + common;

  return min_float(srcDstAlpha,
                   safe_divide_float(dst * srcAlpha,
                                     2.0f * (1.0f - srcRatio))) + common;
}

static VGfloat blend_linear_light_channel(VGfloat src,
                                          VGfloat dst,
                                          VGfloat srcAlpha,
                                          VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat srcDstAlpha = srcAlpha * dstAlpha;
  VGfloat value = 2.0f * src * dstAlpha + dst * srcAlpha;

  if (blend_definitely_greater(value, 2.0f * srcDstAlpha))
    return srcDstAlpha + common;
  if (blend_definitely_greater(value, srcDstAlpha))
    return value - srcDstAlpha + common;
  return common;
}

static VGfloat blend_pin_light_channel(VGfloat src,
                                       VGfloat dst,
                                       VGfloat srcAlpha,
                                       VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat srcDstAlpha = srcAlpha * dstAlpha;
  VGfloat scaledSrc = 2.0f * src * dstAlpha;
  VGfloat scaledDst = dst * srcAlpha;

  if (blend_definitely_greater(scaledSrc - scaledDst, srcDstAlpha)) {
    if (blend_definitely_less(2.0f * src, srcAlpha))
      return common;
    return scaledSrc - srcDstAlpha + common;
  }

  if (blend_definitely_less(scaledSrc, scaledDst))
    return scaledSrc + common;

  return scaledDst + common;
}

static VGfloat blend_hardmix_channel(VGfloat src,
                                     VGfloat dst,
                                     VGfloat srcAlpha,
                                     VGfloat dstAlpha)
{
  VGfloat common = premultiplied_common_channel(src, dst,
                                                srcAlpha, dstAlpha);
  VGfloat srcDstAlpha = srcAlpha * dstAlpha;

  if (blend_definitely_less(src * dstAlpha + dst * srcAlpha, srcDstAlpha))
    return common;

  return srcDstAlpha + common;
}

static void clamp_premultiplied_color(VGfloat color[4])
{
  int channel;

  color[3] = clamp_unit(color[3]);
  for (channel=0; channel<3; ++channel) {
    if (color[channel] < 0.0f)
      color[channel] = 0.0f;
    if (color[channel] > color[3])
      color[channel] = color[3];
  }
}

static void expected_blend_premultiplied(VGBlendMode mode,
                                         const VGfloat srcStraight[4],
                                         const VGfloat dstStraight[4],
                                         VGfloat out[4])
{
  VGfloat src[4];
  VGfloat dst[4];
  VGfloat common;
  VGfloat srcDstAlpha;
  VGfloat value;
  int channel;

  premultiply_color(srcStraight, src);
  premultiply_color(dstStraight, dst);

  switch (mode) {
  case VG_BLEND_SRC:
    out[0] = src[0];
    out[1] = src[1];
    out[2] = src[2];
    out[3] = src[3];
    break;

  case VG_BLEND_DST_OVER:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] * (1.0f - dst[3]) + dst[channel];
    out[3] = src[3] * (1.0f - dst[3]) + dst[3];
    break;

  case VG_BLEND_SRC_IN:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] * dst[3];
    out[3] = src[3] * dst[3];
    break;

  case VG_BLEND_DST_IN:
    for (channel=0; channel<3; ++channel)
      out[channel] = dst[channel] * src[3];
    out[3] = dst[3] * src[3];
    break;

  case VG_BLEND_CLEAR_KHR:
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;
    break;

  case VG_BLEND_DST_KHR:
    out[0] = dst[0];
    out[1] = dst[1];
    out[2] = dst[2];
    out[3] = dst[3];
    break;

  case VG_BLEND_SRC_OUT_KHR:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] * (1.0f - dst[3]);
    out[3] = src[3] * (1.0f - dst[3]);
    break;

  case VG_BLEND_DST_OUT_KHR:
    for (channel=0; channel<3; ++channel)
      out[channel] = dst[channel] * (1.0f - src[3]);
    out[3] = dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_SRC_ATOP_KHR:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] * dst[3] +
                     dst[channel] * (1.0f - src[3]);
    out[3] = dst[3];
    break;

  case VG_BLEND_DST_ATOP_KHR:
    for (channel=0; channel<3; ++channel)
      out[channel] = dst[channel] * src[3] +
                     src[channel] * (1.0f - dst[3]);
    out[3] = src[3];
    break;

  case VG_BLEND_XOR_KHR:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] * (1.0f - dst[3]) +
                     dst[channel] * (1.0f - src[3]);
    out[3] = src[3] * (1.0f - dst[3]) +
             dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_MULTIPLY:
    for (channel=0; channel<3; ++channel) {
      out[channel] = src[channel] * (1.0f - dst[3]) +
                     dst[channel] * (1.0f - src[3]) +
                     src[channel] * dst[channel];
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_SCREEN:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] + dst[channel] -
                     src[channel] * dst[channel];
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_DARKEN:
    for (channel=0; channel<3; ++channel) {
      out[channel] = min_float(src[channel] +
                               dst[channel] * (1.0f - src[3]),
                               dst[channel] +
                               src[channel] * (1.0f - dst[3]));
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_LIGHTEN:
    for (channel=0; channel<3; ++channel) {
      out[channel] = max_float(src[channel] +
                               dst[channel] * (1.0f - src[3]),
                               dst[channel] +
                               src[channel] * (1.0f - dst[3]));
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_ADDITIVE:
    for (channel=0; channel<3; ++channel)
      out[channel] = min_float(src[channel] + dst[channel], 1.0f);
    out[3] = min_float(src[3] + dst[3], 1.0f);
    break;

  case VG_BLEND_OVERLAY_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_overlay_channel(src[channel], dst[channel],
                                           src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_HARDLIGHT_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_hardlight_channel(src[channel], dst[channel],
                                             src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_SOFTLIGHT_SVG_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_softlight_svg_channel(src[channel], dst[channel],
                                                 src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_SOFTLIGHT_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_softlight_channel(src[channel], dst[channel],
                                             src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_COLORDODGE_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_color_dodge_channel(src[channel], dst[channel],
                                               src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_COLORBURN_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_color_burn_channel(src[channel], dst[channel],
                                              src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_DIFFERENCE_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = src[channel] + dst[channel] -
                     2.0f * min_float(src[channel] * dst[3],
                                      dst[channel] * src[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_SUBTRACT_KHR:
    for (channel=0; channel<3; ++channel)
      out[channel] = max_float(dst[channel] - src[channel], 0.0f);
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_INVERT_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = (1.0f - src[3]) * dst[channel] +
                     src[3] * (1.0f - dst[channel]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_EXCLUSION_KHR:
    for (channel=0; channel<3; ++channel) {
      common = premultiplied_common_channel(src[channel], dst[channel],
                                            src[3], dst[3]);
      out[channel] = src[channel] * dst[3] +
                     dst[channel] * src[3] -
                     2.0f * src[channel] * dst[channel] + common;
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_LINEARDODGE_KHR:
    srcDstAlpha = src[3] * dst[3];
    for (channel=0; channel<3; ++channel) {
      common = premultiplied_common_channel(src[channel], dst[channel],
                                            src[3], dst[3]);
      value = src[channel] * dst[3] + dst[channel] * src[3];
      if (value <= srcDstAlpha)
        out[channel] = src[channel] + dst[channel];
      else
        out[channel] = srcDstAlpha + common;
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_LINEARBURN_KHR:
    srcDstAlpha = src[3] * dst[3];
    for (channel=0; channel<3; ++channel) {
      common = premultiplied_common_channel(src[channel], dst[channel],
                                            src[3], dst[3]);
      value = src[channel] * dst[3] + dst[channel] * src[3];
      if (value > srcDstAlpha)
        out[channel] = src[channel] + dst[channel] - srcDstAlpha;
      else
        out[channel] = common;
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_VIVIDLIGHT_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_vivid_light_channel(src[channel], dst[channel],
                                               src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_LINEARLIGHT_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_linear_light_channel(src[channel], dst[channel],
                                                src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_PINLIGHT_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_pin_light_channel(src[channel], dst[channel],
                                             src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_HARDMIX_KHR:
    for (channel=0; channel<3; ++channel) {
      out[channel] = blend_hardmix_channel(src[channel], dst[channel],
                                           src[3], dst[3]);
    }
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;

  case VG_BLEND_SRC_OVER:
  default:
    for (channel=0; channel<3; ++channel)
      out[channel] = src[channel] + dst[channel] * (1.0f - src[3]);
    out[3] = src[3] + dst[3] * (1.0f - src[3]);
    break;
  }

  clamp_premultiplied_color(out);
}

static void expected_blend_pixel(VGBlendMode mode,
                                 const VGfloat srcStraight[4],
                                 const VGfloat dstStraight[4],
                                 VGubyte expected[4])
{
  VGfloat out[4];

  expected_blend_premultiplied(mode, srcStraight, dstStraight, out);
  store_straight_rgba(out, expected);
}

static void expected_blend_covered_pixel(VGBlendMode mode,
                                         const VGfloat srcStraight[4],
                                         const VGfloat dstStraight[4],
                                         VGfloat coverage,
                                         VGubyte expected[4])
{
  VGfloat blended[4];
  VGfloat dst[4];
  VGfloat out[4];
  int channel;

  expected_blend_premultiplied(mode, srcStraight, dstStraight, blended);
  premultiply_color(dstStraight, dst);
  for (channel=0; channel<4; ++channel)
    out[channel] = dst[channel] * (1.0f - coverage) +
                   blended[channel] * coverage;

  clamp_premultiplied_color(out);
  store_straight_rgba(out, expected);
}

static const char *blend_mode_name(VGBlendMode mode)
{
  switch (mode) {
  case VG_BLEND_SRC: return "VG_BLEND_SRC";
  case VG_BLEND_SRC_OVER: return "VG_BLEND_SRC_OVER";
  case VG_BLEND_DST_OVER: return "VG_BLEND_DST_OVER";
  case VG_BLEND_SRC_IN: return "VG_BLEND_SRC_IN";
  case VG_BLEND_DST_IN: return "VG_BLEND_DST_IN";
  case VG_BLEND_MULTIPLY: return "VG_BLEND_MULTIPLY";
  case VG_BLEND_SCREEN: return "VG_BLEND_SCREEN";
  case VG_BLEND_DARKEN: return "VG_BLEND_DARKEN";
  case VG_BLEND_LIGHTEN: return "VG_BLEND_LIGHTEN";
  case VG_BLEND_ADDITIVE: return "VG_BLEND_ADDITIVE";
  case VG_BLEND_OVERLAY_KHR: return "VG_BLEND_OVERLAY_KHR";
  case VG_BLEND_HARDLIGHT_KHR: return "VG_BLEND_HARDLIGHT_KHR";
  case VG_BLEND_SOFTLIGHT_SVG_KHR: return "VG_BLEND_SOFTLIGHT_SVG_KHR";
  case VG_BLEND_SOFTLIGHT_KHR: return "VG_BLEND_SOFTLIGHT_KHR";
  case VG_BLEND_COLORDODGE_KHR: return "VG_BLEND_COLORDODGE_KHR";
  case VG_BLEND_COLORBURN_KHR: return "VG_BLEND_COLORBURN_KHR";
  case VG_BLEND_DIFFERENCE_KHR: return "VG_BLEND_DIFFERENCE_KHR";
  case VG_BLEND_SUBTRACT_KHR: return "VG_BLEND_SUBTRACT_KHR";
  case VG_BLEND_INVERT_KHR: return "VG_BLEND_INVERT_KHR";
  case VG_BLEND_EXCLUSION_KHR: return "VG_BLEND_EXCLUSION_KHR";
  case VG_BLEND_LINEARDODGE_KHR: return "VG_BLEND_LINEARDODGE_KHR";
  case VG_BLEND_LINEARBURN_KHR: return "VG_BLEND_LINEARBURN_KHR";
  case VG_BLEND_VIVIDLIGHT_KHR: return "VG_BLEND_VIVIDLIGHT_KHR";
  case VG_BLEND_LINEARLIGHT_KHR: return "VG_BLEND_LINEARLIGHT_KHR";
  case VG_BLEND_PINLIGHT_KHR: return "VG_BLEND_PINLIGHT_KHR";
  case VG_BLEND_HARDMIX_KHR: return "VG_BLEND_HARDMIX_KHR";
  case VG_BLEND_CLEAR_KHR: return "VG_BLEND_CLEAR_KHR";
  case VG_BLEND_DST_KHR: return "VG_BLEND_DST_KHR";
  case VG_BLEND_SRC_OUT_KHR: return "VG_BLEND_SRC_OUT_KHR";
  case VG_BLEND_DST_OUT_KHR: return "VG_BLEND_DST_OUT_KHR";
  case VG_BLEND_SRC_ATOP_KHR: return "VG_BLEND_SRC_ATOP_KHR";
  case VG_BLEND_DST_ATOP_KHR: return "VG_BLEND_DST_ATOP_KHR";
  case VG_BLEND_XOR_KHR: return "VG_BLEND_XOR_KHR";
  default: return "unknown blend mode";
  }
}

static int run_core_blend_mode_test(unsigned char *pixels,
                                    EGLint width,
                                    EGLint height)
{
  struct {
    VGBlendMode mode;
  } cases[] = {
    { VG_BLEND_SRC },
    { VG_BLEND_SRC_OVER },
    { VG_BLEND_DST_OVER },
    { VG_BLEND_SRC_IN },
    { VG_BLEND_DST_IN },
    { VG_BLEND_MULTIPLY },
    { VG_BLEND_SCREEN },
    { VG_BLEND_DARKEN },
    { VG_BLEND_LIGHTEN },
    { VG_BLEND_ADDITIVE }
  };
  VGPath rect = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGfloat dstColor[] = {0.25f, 0.70f, 0.40f, 0.625f};
  VGfloat srcColor[] = {0.80f, 0.20f, 0.60f, 0.50f};
  VGubyte expected[4];
  int i;
  int result = 0;

  rect = create_rect_path((VGfloat)width, (VGfloat)height);
  paint = vgCreatePaint();
  if (rect == VG_INVALID_HANDLE || paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG blend mode test setup failed");
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetPaint(paint, VG_FILL_PATH);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, srcColor);

  for (i=0; i<(int)(sizeof(cases) / sizeof(cases[0])); ++i) {
    expected_blend_pixel(cases[i].mode, srcColor, dstColor, expected);
    vgSetfv(VG_CLEAR_COLOR, 4, dstColor);
    vgClear(0, 0, width, height);
    vgSeti(VG_BLEND_MODE, cases[i].mode);
    vgDrawPath(rect, VG_FILL_PATH);
    vgFinish();
    vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
                 0, 0, width, height);

    if (expect_no_vg_error("OpenVG core blend mode test failed")) {
      result = 1;
      goto cleanup;
    }

    if (expect_rgba_at(pixels, width * 4, width / 2, height / 2,
                       expected[0], expected[1], expected[2], expected[3],
                       "OpenVG core blend mode produced an unexpected pixel")) {
      fprintf(stderr, "%s expected %u,%u,%u,%u\n",
              blend_mode_name(cases[i].mode),
              expected[0], expected[1], expected[2], expected[3]);
      result = 1;
      goto cleanup;
    }
  }

cleanup:
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  return result;
}

static int run_advanced_blend_mode_test(unsigned char *pixels,
                                        EGLint width,
                                        EGLint height)
{
  struct {
    VGBlendMode mode;
  } cases[] = {
    { VG_BLEND_OVERLAY_KHR },
    { VG_BLEND_HARDLIGHT_KHR },
    { VG_BLEND_SOFTLIGHT_SVG_KHR },
    { VG_BLEND_SOFTLIGHT_KHR },
    { VG_BLEND_COLORDODGE_KHR },
    { VG_BLEND_COLORBURN_KHR },
    { VG_BLEND_DIFFERENCE_KHR },
    { VG_BLEND_SUBTRACT_KHR },
    { VG_BLEND_INVERT_KHR },
    { VG_BLEND_EXCLUSION_KHR },
    { VG_BLEND_LINEARDODGE_KHR },
    { VG_BLEND_LINEARBURN_KHR },
    { VG_BLEND_VIVIDLIGHT_KHR },
    { VG_BLEND_LINEARLIGHT_KHR },
    { VG_BLEND_PINLIGHT_KHR },
    { VG_BLEND_HARDMIX_KHR },
    { VG_BLEND_CLEAR_KHR },
    { VG_BLEND_DST_KHR },
    { VG_BLEND_SRC_OUT_KHR },
    { VG_BLEND_DST_OUT_KHR },
    { VG_BLEND_SRC_ATOP_KHR },
    { VG_BLEND_DST_ATOP_KHR },
    { VG_BLEND_XOR_KHR }
  };
  struct {
    VGfloat src[4];
    VGfloat dst[4];
  } samples[] = {
    {
      { 0.80f, 0.20f, 0.60f, 0.50f },
      { 0.25f, 0.70f, 0.40f, 0.625f }
    },
    {
      { 0.20f, 0.85f, 0.45f, 0.80f },
      { 0.90f, 0.15f, 0.52f, 0.70f }
    },
    {
      { 0.07f, 0.88f, 0.52f, 0.65f },
      { 0.86f, 0.14f, 0.58f, 0.40f }
    },
    {
      { 0.30f, 0.70f, 0.10f, 0.00f },
      { 0.40f, 0.20f, 0.90f, 0.60f }
    }
  };
  VGPath rect = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  const char *extensions;
  VGubyte expected[4];
  int i;
  int sample;
  int result = 0;

  extensions = (const char*)vgGetString(VG_EXTENSIONS);
  if (!extensions || !strstr(extensions, "VG_KHR_advanced_blending")) {
    fprintf(stderr, "OpenVG did not advertise VG_KHR_advanced_blending\n");
    return 1;
  }

  rect = create_rect_path((VGfloat)width, (VGfloat)height);
  paint = vgCreatePaint();
  if (rect == VG_INVALID_HANDLE || paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG advanced blend mode test setup failed");
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetPaint(paint, VG_FILL_PATH);

  for (i=0; i<(int)(sizeof(cases) / sizeof(cases[0])); ++i) {
    for (sample=0; sample<(int)(sizeof(samples) / sizeof(samples[0]));
         ++sample) {
      expected_blend_pixel(cases[i].mode, samples[sample].src,
                           samples[sample].dst, expected);
      vgSetfv(VG_CLEAR_COLOR, 4, samples[sample].dst);
      vgClear(0, 0, width, height);
      vgSetParameterfv(paint, VG_PAINT_COLOR, 4, samples[sample].src);
      vgSeti(VG_BLEND_MODE, cases[i].mode);
      vgDrawPath(rect, VG_FILL_PATH);
      vgFinish();
      vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
                   0, 0, width, height);

      if (expect_no_vg_error("OpenVG advanced blend mode test failed")) {
        result = 1;
        goto cleanup;
      }

      if (expect_rgba_at(pixels, width * 4, width / 2, height / 2,
                         expected[0], expected[1],
                         expected[2], expected[3],
                         "OpenVG advanced blend mode produced an unexpected pixel")) {
        fprintf(stderr, "%s sample %d expected %u,%u,%u,%u\n",
                blend_mode_name(cases[i].mode), sample,
                expected[0], expected[1], expected[2], expected[3]);
        result = 1;
        goto cleanup;
      }
    }
  }

cleanup:
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  return result;
}

static int run_advanced_blend_mask_test(unsigned char *pixels,
                                        EGLint width,
                                        EGLint height)
{
  struct {
    VGBlendMode mode;
  } cases[] = {
    { VG_BLEND_OVERLAY_KHR },
    { VG_BLEND_CLEAR_KHR }
  };
  VGfloat dstColor[] = { 0.25f, 0.70f, 0.40f, 0.625f };
  VGfloat srcColor[] = { 0.80f, 0.20f, 0.60f, 0.50f };
  VGPath rect = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGImage mask = VG_INVALID_HANDLE;
  VGubyte *maskData = NULL;
  VGubyte expected[4];
  VGfloat coverage = 128.0f / 255.0f;
  int i;
  int result = 0;

  rect = create_rect_path((VGfloat)width, (VGfloat)height);
  paint = vgCreatePaint();
  mask = vgCreateImage(VG_A_8, width, height, VG_IMAGE_QUALITY_BETTER);
  maskData = malloc((size_t)width * (size_t)height);
  if (rect == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      mask == VG_INVALID_HANDLE ||
      !maskData) {
    result = fail_vg("OpenVG advanced blend mask test setup failed");
    goto cleanup;
  }

  memset(maskData, 128, (size_t)width * (size_t)height);
  vgImageSubData(mask, maskData, width, VG_A_8, 0, 0, width, height);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, srcColor);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();

  for (i=0; i<(int)(sizeof(cases) / sizeof(cases[0])); ++i) {
    expected_blend_covered_pixel(cases[i].mode, srcColor, dstColor,
                                 coverage, expected);
    vgSeti(VG_MASKING, VG_FALSE);
    vgSetfv(VG_CLEAR_COLOR, 4, dstColor);
    vgClear(0, 0, width, height);
    vgMask(mask, VG_SET_MASK, 0, 0, width, height);
    vgSeti(VG_MASKING, VG_TRUE);
    vgSeti(VG_BLEND_MODE, cases[i].mode);
    vgDrawPath(rect, VG_FILL_PATH);
    vgFinish();
    vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
                 0, 0, width, height);

    if (expect_no_vg_error("OpenVG advanced blend mask test failed")) {
      result = 1;
      goto cleanup;
    }

    if (expect_rgba_at(pixels, width * 4, width / 2, height / 2,
                       expected[0], expected[1], expected[2], expected[3],
                       "OpenVG advanced blend mask coverage produced an unexpected pixel")) {
      fprintf(stderr, "%s masked expected %u,%u,%u,%u\n",
              blend_mode_name(cases[i].mode),
              expected[0], expected[1], expected[2], expected[3]);
      result = 1;
      goto cleanup;
    }
  }

cleanup:
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  if (width > 0 && height > 0)
    vgMask(VG_INVALID_HANDLE, VG_FILL_MASK, 0, 0, width, height);
  free(maskData);
  if (mask != VG_INVALID_HANDLE)
    vgDestroyImage(mask);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  return result;
}

static int run_pixel_transfer_test(unsigned char *pixels,
                                   EGLint width,
                                   EGLint height)
{
  VGImage image = VG_INVALID_HANDLE;
  VGImage dstImage = VG_INVALID_HANDLE;
  VGImage alphaImage = VG_INVALID_HANDLE;
  VGImage lumaImage = VG_INVALID_HANDLE;
  VGImage rgbxImage = VG_INVALID_HANDLE;
  VGubyte writeData[8 * 5 * 4];
  VGubyte imageData[5 * 5 * 4];
  VGubyte stridedImageData[7 * 4 * 4];
  VGubyte imageRead[6 * 6 * 4];
  VGubyte alphaRead[4 * 4];
  VGubyte lumaRead[4 * 4];
  VGubyte lumaWrite[3 * 3];
  VGubyte alphaWrite[3 * 3];
  VGuint translucentPixelStorage[1];
  VGubyte *translucentPixel = (VGubyte*)translucentPixelStorage;
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat blue[] = {0.0f, 0.0f, 1.0f, 1.0f};
  VGfloat translucentRed[] = {1.0f, 0.0f, 0.0f, 0.25f};
  VGfloat transparent[] = {0.0f, 0.0f, 0.0f, 0.0f};
  VGfloat opaqueAlpha[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGint scissor[] = {12, 9, 2, 2};
  int i;
  int result = 0;

  memset(writeData, 0, sizeof(writeData));
  memset(imageData, 0, sizeof(imageData));
  memset(stridedImageData, 0, sizeof(stridedImageData));
  memset(imageRead, 0, sizeof(imageRead));
  memset(alphaRead, 0, sizeof(alphaRead));
  memset(lumaRead, 0, sizeof(lumaRead));
  memset(lumaWrite, 0, sizeof(lumaWrite));
  memset(alphaWrite, 0, sizeof(alphaWrite));
  memset(translucentPixelStorage, 0, sizeof(translucentPixelStorage));
  lumaWrite[1 * 3 + 1] = 96;
  alphaWrite[1 * 3 + 1] = 128;
  set_rgba(translucentPixel, 4, 0, 0, 255, 0, 0, 128);

  for (i=0; i<6 * 5; ++i) {
    VGint x = i % 6;
    VGint y = i / 6;
    set_rgba(writeData, 8 * 4, x, y, 255, 0, 0, 255);
  }
  set_rgba(writeData, 8 * 4, 2, 2, 0, 255, 0, 255);
  set_rgba(writeData, 8 * 4, 3, 2, 255, 255, 0, 255);
  set_rgba(writeData, 8 * 4, 0, 0, 0, 255, 0, 255);
  set_rgba(writeData, 8 * 4, 1, 1, 0, 255, 0, 255);

  for (i=0; i<5 * 5; ++i)
    set_rgba(imageData, 5 * 4, i % 5, i / 5, 255, 0, 0, 255);
  for (i=0; i<4 * 4; ++i)
    set_rgba(stridedImageData, 7 * 4, i % 4, i / 4, 255, 0, 0, 255);
  set_rgba(stridedImageData, 7 * 4, 1, 1, 0, 255, 0, 255);

  image = vgCreateImage(VG_lABGR_8888, 5, 5, VG_IMAGE_QUALITY_BETTER);
  dstImage = vgCreateImage(VG_lABGR_8888, 5, 5, VG_IMAGE_QUALITY_BETTER);
  alphaImage = vgCreateImage(VG_A_8, 4, 4, VG_IMAGE_QUALITY_BETTER);
  lumaImage = vgCreateImage(VG_sL_8, 4, 4, VG_IMAGE_QUALITY_BETTER);
  rgbxImage = vgCreateImage(VG_sRGBX_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  if (image == VG_INVALID_HANDLE ||
      dstImage == VG_INVALID_HANDLE ||
      alphaImage == VG_INVALID_HANDLE ||
      lumaImage == VG_INVALID_HANDLE ||
      rgbxImage == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG pixel transfer test setup failed");
    goto cleanup;
  }

  vgDestroyImage(VG_INVALID_HANDLE);
  if (expect_vg_error("OpenVG destroy image reported the wrong invalid handle error",
                      VG_BAD_HANDLE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(image, imageData, 5 * 4, VG_lABGR_8888, 0, 0, 5, 5);
  vgImageSubData(image, stridedImageData, 7 * 4,
                 VG_lABGR_8888, -1, -1, 4, 4);
  vgGetImageSubData(image, imageRead, 6 * 4,
                    VG_lABGR_8888, 0, 0, 1, 1);
  if (expect_no_vg_error("OpenVG strided image upload/readback failed") ||
      expect_rgba_at(imageRead, 6 * 4, 0, 0, 0, 255, 0, 255,
                     "OpenVG vgImageSubData did not honor clipped strided source pixels")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, blue);
  vgClearImage(image, 2, 1, 2, 3);
  vgGetImageSubData(image, imageRead, 6 * 4,
                    VG_lABGR_8888, 2, 4, 1, 1);
  if (expect_no_vg_error("OpenVG image clear/readback setup failed") ||
      expect_rgba_at(imageRead, 6 * 4, 0, 0, 255, 0, 0, 255,
                     "OpenVG vgGetImageSubData used the wrong source y coordinate")) {
    result = 1;
    goto cleanup;
  }

  vgGetImageSubData(image, imageRead, 6 * 4,
                    VG_lABGR_8888, 2, 2, 1, 1);
  if (expect_rgba_at(imageRead, 6 * 4, 0, 0, 0, 0, 255, 255,
                     "OpenVG vgClearImage did not update the requested image rectangle")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, transparent);
  vgClearImage(alphaImage, 0, 0, 4, 4);
  vgSetfv(VG_CLEAR_COLOR, 4, opaqueAlpha);
  vgClearImage(alphaImage, 1, 1, 2, 2);
  vgGetImageSubData(alphaImage, alphaRead, 4, VG_A_8, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG alpha image clear/readback failed") ||
      alphaRead[1 * 4 + 1] < 250 ||
      alphaRead[0] > 5) {
    fprintf(stderr, "OpenVG vgClearImage did not clear VG_A_8 alpha coverage\n");
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, blue);
  vgClearImage(lumaImage, 0, 0, 4, 4);
  vgGetImageSubData(lumaImage, lumaRead, 4, VG_sL_8, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG luminance image clear/readback failed") ||
      lumaRead[0] < 15 ||
      lumaRead[0] > 22) {
    fprintf(stderr, "OpenVG luminance clear used the wrong conversion value (%u)\n",
            lumaRead[0]);
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, translucentRed);
  vgClearImage(rgbxImage, 0, 0, 4, 4);
  vgGetImageSubData(rgbxImage, imageRead, 6 * 4,
                    VG_lABGR_8888, 0, 0, 1, 1);
  if (expect_no_vg_error("OpenVG RGBX image clear/readback failed") ||
      expect_rgba_at(imageRead, 6 * 4, 0, 0, 255, 0, 0, 255,
                     "OpenVG RGBX image did not read back with opaque alpha")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSetPixels(52, 8, rgbxImage, 0, 0, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG RGBX vgSetPixels failed") ||
      expect_rgba_at(pixels, width * 4, 52, 8, 255, 0, 0, 255,
                     "OpenVG RGBX vgSetPixels did not sample alpha as opaque")) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(lumaImage, imageData, 5 * 4, VG_lABGR_8888, 1, 1, 1, 1);
  vgGetImageSubData(lumaImage, lumaRead, 4, VG_sL_8, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG converted luminance image upload failed") ||
      lumaRead[1 * 4 + 1] < 50 ||
      lumaRead[1 * 4 + 1] > 58) {
    fprintf(stderr, "OpenVG converted vgImageSubData used the wrong luma value (%u)\n",
            lumaRead[1 * 4 + 1]);
    result = 1;
    goto cleanup;
  }

  memset(imageRead, 0, sizeof(imageRead));
  vgGetImageSubData(lumaImage, imageRead, 6 * 4,
                    VG_lABGR_8888, 1, 1, 1, 1);
  if (expect_no_vg_error("OpenVG converted luminance image readback failed") ||
      expect_rgba_at(imageRead, 6 * 4, 0, 0, 54, 54, 54, 255,
                     "OpenVG converted vgGetImageSubData did not expand luma")) {
    result = 1;
    goto cleanup;
  }

  memset(imageRead, 0, sizeof(imageRead));
  vgImageSubData(dstImage, imageRead, 5 * 4, VG_lABGR_8888, 0, 0, 5, 5);
  vgCopyImage(dstImage, 1, 1, image, 2, 1, 2, 2, VG_FALSE);
  vgGetImageSubData(dstImage, imageRead, 5 * 4,
                    VG_lABGR_8888, 0, 0, 5, 5);
  if (expect_rgba_at(imageRead, 5 * 4, 1, 1, 0, 0, 255, 255,
                     "OpenVG vgCopyImage did not copy the source subrectangle")) {
    result = 1;
    goto cleanup;
  }

  vgCopyImage(lumaImage, 2, 2, image, 2, 2, 1, 1, VG_FALSE);
  vgGetImageSubData(lumaImage, lumaRead, 4, VG_sL_8, 0, 0, 4, 4);
  if (expect_no_vg_error("OpenVG converted vgCopyImage failed") ||
      lumaRead[2 * 4 + 2] < 15 ||
      lumaRead[2 * 4 + 2] > 22) {
    fprintf(stderr, "OpenVG converted vgCopyImage used the wrong luma value (%u)\n",
            lumaRead[2 * 4 + 2]);
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK, 0, 0, width, height);
  vgSeti(VG_MASKING, VG_TRUE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_DST_IN);
  vgSetiv(VG_SCISSOR_RECTS, 4, scissor);
  vgSeti(VG_SCISSORING, VG_TRUE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(100.0f, 100.0f);
  vgWritePixels(writeData, 8 * 4, VG_lABGR_8888, 10, 8, 6, 5);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG vgWritePixels state isolation failed") ||
      expect_rgba_at(pixels, width * 4, 12, 10, 0, 255, 0, 255,
                     "OpenVG vgWritePixels did not write inside the scissor") ||
      expect_rgba_at(pixels, width * 4, 10, 8, 0, 0, 0, 255,
                     "OpenVG vgWritePixels ignored scissoring")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgWritePixels(writeData, 8 * 4, VG_lABGR_8888, -1, -1, 3, 3);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG clipped vgWritePixels failed") ||
      expect_rgba_at(pixels, width * 4, 0, 0, 0, 255, 0, 255,
                     "OpenVG clipped vgWritePixels used the wrong source offset") ||
      expect_rgba_at(pixels, width * 4, 2, 2, 0, 0, 0, 255,
                     "OpenVG clipped vgWritePixels wrote outside the clipped rectangle")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgWritePixels(lumaWrite, 3, VG_sL_8, 50, 8, 3, 3);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG luminance vgWritePixels failed") ||
      expect_rgba_at(pixels, width * 4, 51, 9, 96, 96, 96, 255,
                     "OpenVG luminance vgWritePixels did not expand to grayscale")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgWritePixels(alphaWrite, 3, VG_A_8, 54, 8, 3, 3);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG alpha vgWritePixels failed") ||
      expect_rgba_at(pixels, width * 4, 55, 9, 255, 255, 255, 128,
                     "OpenVG alpha vgWritePixels did not expand coverage")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, transparent);
  vgClear(0, 0, width, height);
  vgWritePixels(translucentPixel, 4, VG_lABGR_8888, 56, 8, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG translucent vgWritePixels failed") ||
      expect_rgba_at(pixels, width * 4, 56, 8, 255, 0, 0, 128,
                     "OpenVG vgReadPixels exposed premultiplied surface bytes")) {
    result = 1;
    goto cleanup;
  }

  memset(imageRead, 0, sizeof(imageRead));
  vgGetPixels(dstImage, 0, 0, 56, 8, 1, 1);
  vgGetImageSubData(dstImage, imageRead, 5 * 4,
                    VG_lABGR_8888, 0, 0, 1, 1);
  if (expect_no_vg_error("OpenVG translucent vgGetPixels failed") ||
      expect_rgba_at(imageRead, 5 * 4, 0, 0, 255, 0, 0, 128,
                     "OpenVG vgGetPixels stored premultiplied surface bytes")) {
    result = 1;
    goto cleanup;
  }

  vgCopyPixels(57, 8, 56, 8, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG translucent vgCopyPixels failed") ||
      expect_rgba_at(pixels, width * 4, 57, 8, 255, 0, 0, 128,
                     "OpenVG vgCopyPixels double-premultiplied surface pixels")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSetPixels(20, 8, image, 2, 1, 2, 2);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG vgSetPixels failed") ||
      expect_rgba_at(pixels, width * 4, 20, 8, 0, 0, 255, 255,
                     "OpenVG vgSetPixels did not copy image pixels to the surface")) {
    result = 1;
    goto cleanup;
  }

  for (i=0; i<5 * 5; ++i)
    set_rgba(imageData, 5 * 4, i % 5, i / 5, 255, 0, 0, 255);
  set_rgba(imageData, 5 * 4, 1, 1, 0, 255, 0, 255);
  set_rgba(imageData, 5 * 4, 2, 1, 255, 255, 0, 255);
  vgImageSubData(image, imageData, 5 * 4, VG_lABGR_8888, 0, 0, 5, 5);
  vgCopyImage(image, 2, 1, image, 1, 1, 2, 1, VG_FALSE);
  vgGetImageSubData(image, imageRead, 5 * 4,
                    VG_lABGR_8888, 0, 0, 5, 5);
  if (expect_no_vg_error("OpenVG overlapping image copy failed") ||
      expect_rgba_at(imageRead, 5 * 4, 2, 1, 0, 255, 0, 255,
                     "OpenVG overlapping vgCopyImage did not preserve the first source pixel") ||
      expect_rgba_at(imageRead, 5 * 4, 3, 1, 255, 255, 0, 255,
                     "OpenVG overlapping vgCopyImage did not use a temporary source copy")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgWritePixels(writeData, 8 * 4, VG_lABGR_8888, 30, 8, 6, 5);
  memset(imageRead, 0, sizeof(imageRead));
  vgImageSubData(dstImage, imageRead, 5 * 4, VG_lABGR_8888, 0, 0, 5, 5);
  vgGetPixels(dstImage, 1, 2, 32, 10, 2, 2);
  vgGetImageSubData(dstImage, imageRead, 5 * 4,
                    VG_lABGR_8888, 0, 0, 5, 5);
  if (expect_no_vg_error("OpenVG vgGetPixels failed") ||
      expect_rgba_at(imageRead, 5 * 4, 1, 2, 0, 255, 0, 255,
                     "OpenVG vgGetPixels did not copy surface pixels into the image")) {
    result = 1;
    goto cleanup;
  }

  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, 1);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  vgGetPixels(alphaImage, 0, 0, 32, 10, 1, 1);
  vgGetImageSubData(alphaImage, alphaRead, 4, VG_A_8, 0, 0, 1, 1);
  if (expect_no_vg_error("OpenVG alpha vgGetPixels failed") ||
      alphaRead[0] < 250 ||
      expect_gl_pack_state(8, 1, 0, 0,
                           "OpenVG alpha vgGetPixels leaked GL pack state")) {
    fprintf(stderr, "OpenVG alpha vgGetPixels did not copy alpha coverage\n");
    result = 1;
    goto cleanup;
  }
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);

  vgGetPixels(lumaImage, 0, 0, 32, 10, 1, 1);
  vgGetImageSubData(lumaImage, lumaRead, 4, VG_sL_8, 0, 0, 1, 1);
  if (expect_no_vg_error("OpenVG luminance vgGetPixels failed") ||
      lumaRead[0] < 178 ||
      lumaRead[0] > 186) {
    fprintf(stderr, "OpenVG luminance vgGetPixels used the wrong luma value (%u)\n",
            lumaRead[0]);
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSetPixels(40, 8, dstImage, 1, 2, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG dirty image sync after vgGetPixels failed") ||
      expect_rgba_at(pixels, width * 4, 40, 8, 0, 255, 0, 255,
                     "OpenVG did not sync a GPU-updated image before vgSetPixels")) {
    result = 1;
    goto cleanup;
  }

  memset(imageRead, 0, sizeof(imageRead));
  vgReadPixels(imageRead, 5 * 4, VG_sRGBA_8888, 40, 8, 1, 1);
  if (expect_no_vg_error("OpenVG vgReadPixels failed") ||
      expect_rgba_at(imageRead, 5 * 4, 0, 0, 0, 255, 0, 255,
                     "OpenVG vgReadPixels did not copy the requested surface pixel")) {
    result = 1;
    goto cleanup;
  }

  glPixelStorei(GL_PACK_ALIGNMENT, 8);
  glPixelStorei(GL_PACK_ROW_LENGTH, 1);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  memset(imageRead, 0, sizeof(imageRead));
  vgReadPixels(imageRead, 5 * 4, VG_lRGBA_8888, 40, 8, 1, 1);
  if (expect_no_vg_error("OpenVG fallback vgReadPixels failed") ||
      expect_rgba_at(imageRead, 5 * 4, 0, 0, 0, 255, 0, 255,
                     "OpenVG fallback vgReadPixels did not copy the requested surface pixel") ||
      expect_gl_pack_state(8, 1, 0, 0,
                           "OpenVG fallback vgReadPixels leaked GL pack state")) {
    result = 1;
    goto cleanup;
  }
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgWritePixels(writeData, 8 * 4, VG_lABGR_8888, 30, 20, 6, 5);
  vgCopyPixels(32, 21, 30, 20, 4, 3);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG vgCopyPixels failed") ||
      expect_rgba_at(pixels, width * 4, 32, 21, 0, 255, 0, 255,
                     "OpenVG vgCopyPixels did not copy the source rectangle") ||
      expect_rgba_at(pixels, width * 4, 35, 23, 255, 255, 0, 255,
                     "OpenVG vgCopyPixels did not preserve overlapping source pixels")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_PACK_SKIP_ROWS, 0);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  if (lumaImage != VG_INVALID_HANDLE)
    vgDestroyImage(lumaImage);
  if (rgbxImage != VG_INVALID_HANDLE)
    vgDestroyImage(rgbxImage);
  if (alphaImage != VG_INVALID_HANDLE)
    vgDestroyImage(alphaImage);
  if (dstImage != VG_INVALID_HANDLE)
    vgDestroyImage(dstImage);
  if (image != VG_INVALID_HANDLE)
    vgDestroyImage(image);
  return result;
}

static int run_core_image_format_test(unsigned char *pixels,
                                      EGLint width,
                                      EGLint height)
{
  static const VGImageFormat formats[] = {
    VG_sRGBX_8888,
    VG_sRGBA_8888,
    VG_sRGBA_8888_PRE,
    VG_sRGB_565,
    VG_sRGBA_5551,
    VG_sRGBA_4444,
    VG_sL_8,
    VG_lRGBX_8888,
    VG_lRGBA_8888,
    VG_lRGBA_8888_PRE,
    VG_lL_8,
    VG_A_8,
    VG_BW_1,
    VG_A_1,
    VG_A_4,
    VG_sXRGB_8888,
    VG_sARGB_8888,
    VG_sARGB_8888_PRE,
    VG_sARGB_1555,
    VG_sARGB_4444,
    VG_lXRGB_8888,
    VG_lARGB_8888,
    VG_lARGB_8888_PRE,
    VG_sBGRX_8888,
    VG_sBGRA_8888,
    VG_sBGRA_8888_PRE,
    VG_sBGR_565,
    VG_sBGRA_5551,
    VG_sBGRA_4444,
    VG_lBGRX_8888,
    VG_lBGRA_8888,
    VG_lBGRA_8888_PRE,
    VG_sXBGR_8888,
    VG_sABGR_8888,
    VG_sABGR_8888_PRE,
    VG_sABGR_1555,
    VG_sABGR_4444,
    VG_lXBGR_8888,
    VG_lABGR_8888,
    VG_lABGR_8888_PRE
  };
  VGImage created[sizeof(formats) / sizeof(formats[0])];
  VGImage preImage = VG_INVALID_HANDLE;
  VGImage a1Image = VG_INVALID_HANDLE;
  VGImage a4Image = VG_INVALID_HANDLE;
  VGImage bwImage = VG_INVALID_HANDLE;
  VGImage bgr565Image = VG_INVALID_HANDLE;
  VGubyte preData[2 * 2 * 4];
  VGubyte straightRead[2 * 2 * 4];
  VGubyte preRead[2 * 2 * 4];
  VGubyte a1Data[] = {0x80};
  VGubyte a4Data[] = {0xf0};
  VGubyte aRead[8];
  VGubyte bwData[] = {0xa5, 0x3c};
  VGubyte bwRead[2];
  VGubyte bwExpanded[8 * 2 * 4];
  VGubyte bwWrite[] = {0x40};
  unsigned short bgr565Red = 0x001fu;
  VGint bgr565X = width - 10;
  VGint bwWriteX = width - 8;
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat white[] = {1.0f, 1.0f, 1.0f, 1.0f};
  size_t i;
  int result = 0;

  for (i=0; i<sizeof(created) / sizeof(created[0]); ++i)
    created[i] = VG_INVALID_HANDLE;

  for (i=0; i<sizeof(formats) / sizeof(formats[0]); ++i) {
    created[i] = vgCreateImage(formats[i], 2, 2,
                               VG_IMAGE_QUALITY_BETTER);
    if (created[i] == VG_INVALID_HANDLE ||
        vgGetParameteri(created[i], VG_IMAGE_FORMAT) != formats[i]) {
      fprintf(stderr, "OpenVG core image format 0x%x failed\n",
              (unsigned int)formats[i]);
      result = fail_vg("OpenVG core image format creation failed");
      goto cleanup;
    }
  }
  if (expect_no_vg_error("OpenVG core image format query failed")) {
    result = 1;
    goto cleanup;
  }

  preImage = vgCreateImage(VG_lABGR_8888_PRE, 2, 2,
                           VG_IMAGE_QUALITY_BETTER);
  a1Image = vgCreateImage(VG_A_1, 8, 1, VG_IMAGE_QUALITY_BETTER);
  a4Image = vgCreateImage(VG_A_4, 2, 1, VG_IMAGE_QUALITY_BETTER);
  bwImage = vgCreateImage(VG_BW_1, 8, 2, VG_IMAGE_QUALITY_BETTER);
  bgr565Image = vgCreateImage(VG_sBGR_565, 1, 1,
                              VG_IMAGE_QUALITY_BETTER);
  if (preImage == VG_INVALID_HANDLE ||
      a1Image == VG_INVALID_HANDLE ||
      a4Image == VG_INVALID_HANDLE ||
      bwImage == VG_INVALID_HANDLE ||
      bgr565Image == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG core image format test setup failed");
    goto cleanup;
  }

  memset(preData, 0, sizeof(preData));
  memset(straightRead, 0, sizeof(straightRead));
  memset(preRead, 0, sizeof(preRead));
  set_rgba(preData, 2 * 4, 0, 0, 128, 0, 0, 128);
  vgImageSubData(preImage, preData, 2 * 4,
                 VG_lABGR_8888_PRE, 0, 0, 2, 2);
  vgGetImageSubData(preImage, straightRead, 2 * 4,
                    VG_lABGR_8888, 0, 0, 2, 2);
  vgGetImageSubData(preImage, preRead, 2 * 4,
                    VG_lABGR_8888_PRE, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG premultiplied image roundtrip failed") ||
      expect_rgba_at(straightRead, 2 * 4, 0, 0, 255, 0, 0, 128,
                     "OpenVG PRE image did not unpremultiply on readback") ||
      expect_rgba_at(preRead, 2 * 4, 0, 0, 128, 0, 0, 128,
                     "OpenVG PRE image did not premultiply on readback")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSetPixels(62, 8, preImage, 0, 0, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG PRE image vgSetPixels failed") ||
      expect_rgba_at(pixels, width * 4, 62, 8, 255, 0, 0, 128,
                     "OpenVG PRE image draw used associated color as straight color")) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(bgr565Image, &bgr565Red, sizeof(bgr565Red),
                 VG_sBGR_565, 0, 0, 1, 1);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSetPixels(bgr565X, 8, bgr565Image, 0, 0, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG BGR_565 image upload failed") ||
      expect_rgba_at(pixels, width * 4, bgr565X, 8, 255, 0, 0, 255,
                     "OpenVG BGR_565 image did not preserve channel order")) {
    result = 1;
    goto cleanup;
  }

  memset(aRead, 0, sizeof(aRead));
  vgImageSubData(a1Image, a1Data, 1, VG_A_1, 0, 0, 8, 1);
  vgGetImageSubData(a1Image, aRead, 8, VG_A_8, 0, 0, 8, 1);
  if (expect_no_vg_error("OpenVG A_1 image conversion failed") ||
      aRead[0] < 250 ||
      aRead[1] > 5) {
    fprintf(stderr, "OpenVG A_1 image did not expand packed alpha\n");
    result = 1;
    goto cleanup;
  }

  memset(aRead, 0, sizeof(aRead));
  vgImageSubData(a4Image, a4Data, 1, VG_A_4, 0, 0, 2, 1);
  vgGetImageSubData(a4Image, aRead, 2, VG_A_8, 0, 0, 2, 1);
  if (expect_no_vg_error("OpenVG A_4 image conversion failed") ||
      aRead[0] < 250 ||
      aRead[1] > 5) {
    fprintf(stderr, "OpenVG A_4 image did not expand packed alpha\n");
    result = 1;
    goto cleanup;
  }

  memset(bwRead, 0, sizeof(bwRead));
  memset(bwExpanded, 0, sizeof(bwExpanded));
  vgImageSubData(bwImage, bwData, 1, VG_BW_1, 0, 0, 8, 2);
  vgGetImageSubData(bwImage, bwRead, 1, VG_BW_1, 0, 0, 8, 2);
  vgGetImageSubData(bwImage, bwExpanded, 8 * 4,
                    VG_lABGR_8888, 0, 0, 8, 2);
  if (expect_no_vg_error("OpenVG BW_1 image roundtrip failed") ||
      bwRead[0] != 0xa5 ||
      bwRead[1] != 0x3c ||
      expect_rgba_at(bwExpanded, 8 * 4, 0, 0, 255, 255, 255, 255,
                     "OpenVG BW_1 image did not expand one bits to white") ||
      expect_rgba_at(bwExpanded, 8 * 4, 1, 0, 0, 0, 0, 255,
                     "OpenVG BW_1 image did not expand zero bits to black")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, white);
  vgClearImage(bwImage, 1, 1, 1, 1);
  memset(bwExpanded, 0, sizeof(bwExpanded));
  vgGetImageSubData(bwImage, bwExpanded, 8 * 4,
                    VG_lABGR_8888, 0, 0, 8, 2);
  if (expect_no_vg_error("OpenVG BW_1 image clear failed") ||
      expect_rgba_at(bwExpanded, 8 * 4, 1, 1, 255, 255, 255, 255,
                     "OpenVG BW_1 clear did not threshold to white")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgWritePixels(bwWrite, 1, VG_BW_1, bwWriteX, 8, 8, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG BW_1 vgWritePixels failed") ||
      expect_rgba_at(pixels, width * 4, bwWriteX, 8, 0, 0, 0, 255,
                     "OpenVG BW_1 vgWritePixels wrote the wrong zero bit") ||
      expect_rgba_at(pixels, width * 4, bwWriteX + 1, 8,
                     255, 255, 255, 255,
                     "OpenVG BW_1 vgWritePixels did not write the one bit")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if (bgr565Image != VG_INVALID_HANDLE)
    vgDestroyImage(bgr565Image);
  if (bwImage != VG_INVALID_HANDLE)
    vgDestroyImage(bwImage);
  if (a4Image != VG_INVALID_HANDLE)
    vgDestroyImage(a4Image);
  if (a1Image != VG_INVALID_HANDLE)
    vgDestroyImage(a1Image);
  if (preImage != VG_INVALID_HANDLE)
    vgDestroyImage(preImage);
  for (i=0; i<sizeof(created) / sizeof(created[0]); ++i)
    if (created[i] != VG_INVALID_HANDLE)
      vgDestroyImage(created[i]);
  return result;
}

static int run_child_image_test(unsigned char *pixels,
                                EGLint width,
                                EGLint height)
{
  VGImage parent = VG_INVALID_HANDLE;
  VGImage child = VG_INVALID_HANDLE;
  VGImage grandchild = VG_INVALID_HANDLE;
  VGImage dest = VG_INVALID_HANDLE;
  VGImage destChild = VG_INVALID_HANDLE;
  VGImage patternParent = VG_INVALID_HANDLE;
  VGImage patternChild = VG_INVALID_HANDLE;
  VGImage chainRoot = VG_INVALID_HANDLE;
  VGImage chainParent = VG_INVALID_HANDLE;
  VGImage chainChild = VG_INVALID_HANDLE;
  VGPaint patternPaint = VG_INVALID_HANDLE;
  VGPath patternPath = VG_INVALID_HANDLE;
  VGubyte parentData[8 * 6 * 4];
  VGubyte readData[8 * 6 * 4];
  VGubyte blueData[2 * 2 * 4];
  VGubyte patternData[4 * 2 * 4];
  VGubyte yellowPixel[4];
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGfloat tileFill[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGfloat chainGreen[] = {0.0f, 0.5f, 0.0f, 1.0f};
  int i;
  int result = 0;

  for (i=0; i<8 * 6; ++i)
    set_rgba(parentData, 8 * 4, i % 8, i / 8, 255, 0, 0, 255);
  for (i=0; i<2 * 2; ++i)
    set_rgba(blueData, 2 * 4, i % 2, i / 2, 0, 0, 255, 255);
  for (i=0; i<4 * 2; ++i) {
    VGint x = i % 4;
    set_rgba(patternData, 4 * 4, x, i / 4,
             x < 2 ? 255 : 0, 0, x < 2 ? 0 : 255, 255);
  }
  set_rgba(yellowPixel, 4, 0, 0, 255, 255, 0, 255);

  parent = vgCreateImage(VG_lABGR_8888, 8, 6, VG_IMAGE_QUALITY_BETTER);
  dest = vgCreateImage(VG_lABGR_8888, 6, 6, VG_IMAGE_QUALITY_BETTER);
  if (parent == VG_INVALID_HANDLE || dest == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG child image test setup failed");
    goto cleanup;
  }

  vgChildImage(VG_INVALID_HANDLE, 0, 0, 1, 1);
  if (expect_vg_error("OpenVG accepted an invalid child image parent",
                      VG_BAD_HANDLE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  if (vgChildImage(parent, -1, 0, 1, 1) != VG_INVALID_HANDLE ||
      expect_vg_error("OpenVG accepted a negative child image origin",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  if (vgChildImage(parent, 7, 0, 2, 1) != VG_INVALID_HANDLE ||
      expect_vg_error("OpenVG accepted an out-of-bounds child image",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgImageSubData(parent, parentData, 8 * 4, VG_lABGR_8888, 0, 0, 8, 6);
  child = vgChildImage(parent, 2, 1, 3, 3);
  grandchild = vgChildImage(child, 1, 1, 2, 2);
  destChild = vgChildImage(dest, 1, 1, 3, 3);
  if (child == VG_INVALID_HANDLE ||
      grandchild == VG_INVALID_HANDLE ||
      destChild == VG_INVALID_HANDLE ||
      expect_no_vg_error("OpenVG child image creation failed")) {
    result = 1;
    goto cleanup;
  }

  if (vgGetParent(parent) != parent ||
      vgGetParent(child) != parent ||
      vgGetParent(grandchild) != child ||
      expect_no_vg_error("OpenVG child image parent query failed")) {
    fprintf(stderr, "OpenVG child image parent chain was incorrect\n");
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, green);
  vgClearImage(child, 0, 0, 3, 3);
  vgImageSubData(grandchild, blueData, 2 * 4, VG_lABGR_8888, 0, 0, 2, 2);
  vgGetImageSubData(parent, readData, 8 * 4, VG_lABGR_8888, 0, 0, 8, 6);
  if (expect_no_vg_error("OpenVG child image shared storage update failed") ||
      expect_rgba_at(readData, 8 * 4, 2, 1, 0, 255, 0, 255,
                     "OpenVG child clear did not update parent storage") ||
      expect_rgba_at(readData, 8 * 4, 3, 2, 0, 0, 255, 255,
                     "OpenVG grandchild upload did not update parent storage") ||
      expect_rgba_at(readData, 8 * 4, 1, 1, 255, 0, 0, 255,
                     "OpenVG child update wrote outside its storage view")) {
    result = 1;
    goto cleanup;
  }

  memset(readData, 0, sizeof(readData));
  vgGetImageSubData(child, readData, 3 * 4, VG_lABGR_8888, 0, 0, 3, 3);
  if (expect_no_vg_error("OpenVG child image readback failed") ||
      expect_rgba_at(readData, 3 * 4, 1, 1, 0, 0, 255, 255,
                     "OpenVG child readback sampled the wrong storage offset")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClearImage(dest, 0, 0, 6, 6);
  vgCopyImage(destChild, 0, 0, child, 0, 0, 3, 3, VG_FALSE);
  vgGetImageSubData(dest, readData, 6 * 4, VG_lABGR_8888, 0, 0, 6, 6);
  if (expect_no_vg_error("OpenVG child image copy failed") ||
      expect_rgba_at(readData, 6 * 4, 1, 1, 0, 255, 0, 255,
                     "OpenVG child copy used the wrong destination offset") ||
      expect_rgba_at(readData, 6 * 4, 2, 2, 0, 0, 255, 255,
                     "OpenVG child copy used the wrong source offset")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSetPixels(10, 10, child, 0, 0, 3, 3);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(20.0f, 10.0f);
  vgDrawImage(child);
  vgWritePixels(yellowPixel, 4, VG_lABGR_8888, 30, 10, 1, 1);
  vgGetPixels(grandchild, 0, 0, 30, 10, 1, 1);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  vgGetImageSubData(parent, readData, 8 * 4, VG_lABGR_8888, 0, 0, 8, 6);
  if (expect_no_vg_error("OpenVG child image surface transfer failed") ||
      expect_rgba_at(pixels, width * 4, 10, 10, 0, 255, 0, 255,
                     "OpenVG vgSetPixels did not sample child storage") ||
      expect_rgba_at(pixels, width * 4, 21, 11, 0, 0, 255, 255,
                     "OpenVG vgDrawImage did not crop to the child image") ||
      expect_rgba_at(readData, 8 * 4, 3, 2, 255, 255, 0, 255,
                     "OpenVG vgGetPixels did not write through grandchild storage")) {
    result = 1;
    goto cleanup;
  }

  patternParent = vgCreateImage(VG_lABGR_8888, 4, 2,
                                VG_IMAGE_QUALITY_BETTER);
  patternChild = vgChildImage(patternParent, 2, 0, 2, 2);
  patternPaint = vgCreatePaint();
  patternPath = create_rect_path(6.0f, 4.0f);
  if (patternParent == VG_INVALID_HANDLE ||
      patternChild == VG_INVALID_HANDLE ||
      patternPaint == VG_INVALID_HANDLE ||
      patternPath == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG child pattern test setup failed");
    goto cleanup;
  }

  vgImageSubData(patternParent, patternData, 4 * 4,
                 VG_lABGR_8888, 0, 0, 4, 2);
  vgSetParameteri(patternPaint, VG_PAINT_TYPE, VG_PAINT_TYPE_PATTERN);
  vgSetfv(VG_TILE_FILL_COLOR, 4, tileFill);
  vgSetPaint(patternPaint, VG_FILL_PATH);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_FILL_PAINT_TO_USER);
  vgLoadIdentity();
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

  vgSetParameteri(patternPaint, VG_PAINT_PATTERN_TILING_MODE, VG_TILE_REPEAT);
  vgPaintPattern(patternPaint, patternChild);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(40.0f, 20.0f);
  vgDrawPath(patternPath, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG child repeat pattern paint failed") ||
      expect_rgba_at(pixels, width * 4, 41, 21, 0, 0, 255, 255,
                     "OpenVG child repeat pattern sampled outside the child image") ||
      expect_rgba_at(pixels, width * 4, 44, 21, 0, 0, 255, 255,
                     "OpenVG child repeat pattern did not repeat the child image")) {
    result = 1;
    goto cleanup;
  }

  vgSetParameteri(patternPaint, VG_PAINT_PATTERN_TILING_MODE, VG_TILE_PAD);
  vgPaintPattern(patternPaint, patternChild);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(40.0f, 20.0f);
  vgDrawPath(patternPath, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG child pad pattern paint failed") ||
      expect_rgba_at(pixels, width * 4, 41, 21, 0, 0, 255, 255,
                     "OpenVG child pad pattern sampled outside the child image") ||
      expect_rgba_at(pixels, width * 4, 44, 21, 0, 0, 255, 255,
                     "OpenVG child pad pattern did not clamp to the child image")) {
    result = 1;
    goto cleanup;
  }

  vgSetParameteri(patternPaint, VG_PAINT_PATTERN_TILING_MODE, VG_TILE_FILL);
  vgPaintPattern(patternPaint, patternChild);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(40.0f, 20.0f);
  vgDrawPath(patternPath, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG child fill pattern paint failed") ||
      expect_rgba_at(pixels, width * 4, 41, 21, 0, 0, 255, 255,
                     "OpenVG child fill pattern sampled outside the child image") ||
      expect_rgba_at(pixels, width * 4, 44, 21, 0, 255, 0, 255,
                     "OpenVG child fill pattern did not use the tile fill color")) {
    result = 1;
    goto cleanup;
  }

  chainRoot = vgCreateImage(VG_lABGR_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  chainParent = vgChildImage(chainRoot, 0, 0, 3, 3);
  chainChild = vgChildImage(chainParent, 1, 1, 2, 2);
  if (chainRoot == VG_INVALID_HANDLE ||
      chainParent == VG_INVALID_HANDLE ||
      chainChild == VG_INVALID_HANDLE ||
      expect_no_vg_error("OpenVG child lifetime chain setup failed")) {
    result = 1;
    goto cleanup;
  }

  if (vgGetParent(chainChild) != chainParent) {
    fprintf(stderr, "OpenVG child lifetime parent query returned the wrong live parent\n");
    result = 1;
    goto cleanup;
  }
  vgDestroyImage(chainParent);
  chainParent = VG_INVALID_HANDLE;
  if (vgGetParent(chainChild) != chainRoot ||
      expect_no_vg_error("OpenVG child lifetime skipped wrong destroyed parent")) {
    fprintf(stderr, "OpenVG child lifetime did not skip destroyed intermediate parent\n");
    result = 1;
    goto cleanup;
  }
  vgDestroyImage(chainRoot);
  chainRoot = VG_INVALID_HANDLE;
  if (vgGetParent(chainChild) != chainChild ||
      expect_no_vg_error("OpenVG child lifetime final parent query failed")) {
    fprintf(stderr, "OpenVG child lifetime did not fall back to the live child\n");
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, chainGreen);
  vgClearImage(chainChild, 0, 0, 2, 2);
  vgGetImageSubData(chainChild, readData, 2 * 4, VG_lABGR_8888, 0, 0, 2, 2);
  if (expect_no_vg_error("OpenVG child storage was not retained after parent destruction") ||
      expect_rgba_at(readData, 2 * 4, 0, 0, 0, 128, 0, 255,
                     "OpenVG child storage changed after parent handles were destroyed")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if (chainChild != VG_INVALID_HANDLE)
    vgDestroyImage(chainChild);
  if (chainParent != VG_INVALID_HANDLE)
    vgDestroyImage(chainParent);
  if (chainRoot != VG_INVALID_HANDLE)
    vgDestroyImage(chainRoot);
  if (patternPath != VG_INVALID_HANDLE)
    vgDestroyPath(patternPath);
  if (patternPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(patternPaint);
  if (patternChild != VG_INVALID_HANDLE)
    vgDestroyImage(patternChild);
  if (patternParent != VG_INVALID_HANDLE)
    vgDestroyImage(patternParent);
  if (destChild != VG_INVALID_HANDLE)
    vgDestroyImage(destChild);
  if (grandchild != VG_INVALID_HANDLE)
    vgDestroyImage(grandchild);
  if (child != VG_INVALID_HANDLE)
    vgDestroyImage(child);
  if (dest != VG_INVALID_HANDLE)
    vgDestroyImage(dest);
  if (parent != VG_INVALID_HANDLE)
    vgDestroyImage(parent);
  return result;
}

static int run_glyph_image_batch_test(unsigned char *pixels,
                                      EGLint width,
                                      EGLint height)
{
  VGImage atlas = VG_INVALID_HANDLE;
  VGImage redChild = VG_INVALID_HANDLE;
  VGImage greenChild = VG_INVALID_HANDLE;
  VGImage blueChild = VG_INVALID_HANDLE;
  VGImage otherImage = VG_INVALID_HANDLE;
  VGFont font = VG_INVALID_HANDLE;
  VGPath path = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGubyte atlasData[24 * 8 * 4];
  VGubyte otherData[8 * 8 * 4];
  VGfloat glyphOrigin[] = {0.0f, 0.0f};
  VGfloat escapement[] = {9.0f, 0.0f};
  VGfloat firstOrigin[] = {10.0f, 10.0f};
  VGfloat secondOrigin[] = {10.0f, 30.0f};
  VGfloat yellow[] = {1.0f, 1.0f, 0.0f, 1.0f};
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGuint imageRun[] = {1u, 2u, 3u};
  VGfloat imageAdjustX[] = {1.0f, 2.0f, 0.0f};
  VGfloat imageAdjustY[] = {0.0f, 1.0f, 0.0f};
  VGuint mixedRun[] = {1u, 4u, 5u, 2u};
  int x, y;
  int result = 0;

  memset(atlasData, 0, sizeof(atlasData));
  memset(otherData, 0, sizeof(otherData));

  for (y=0; y<8; ++y) {
    for (x=0; x<24; ++x) {
      if (x < 8)
        set_rgba(atlasData, 24 * 4, x, y, 255, 0, 0, 255);
      else if (x < 16)
        set_rgba(atlasData, 24 * 4, x, y, 0, 255, 0, 255);
      else
        set_rgba(atlasData, 24 * 4, x, y, 0, 0, 255, 255);
    }
  }
  for (y=0; y<8; ++y)
    for (x=0; x<8; ++x)
      set_rgba(otherData, 8 * 4, x, y, 0, 255, 255, 255);

  atlas = vgCreateImage(VG_lABGR_8888, 24, 8, VG_IMAGE_QUALITY_BETTER);
  otherImage = vgCreateImage(VG_lABGR_8888, 8, 8,
                             VG_IMAGE_QUALITY_BETTER);
  font = vgCreateFont(5);
  path = create_rect_path(8.0f, 8.0f);
  paint = vgCreatePaint();
  if (atlas == VG_INVALID_HANDLE ||
      otherImage == VG_INVALID_HANDLE ||
      font == VG_INVALID_HANDLE ||
      path == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG glyph image batch setup failed");
    goto cleanup;
  }

  redChild = vgChildImage(atlas, 0, 0, 8, 8);
  greenChild = vgChildImage(atlas, 8, 0, 8, 8);
  blueChild = vgChildImage(atlas, 16, 0, 8, 8);
  if (redChild == VG_INVALID_HANDLE ||
      greenChild == VG_INVALID_HANDLE ||
      blueChild == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG glyph image batch child setup failed");
    goto cleanup;
  }

  vgImageSubData(atlas, atlasData, 24 * 4, VG_lABGR_8888,
                 0, 0, 24, 8);
  vgImageSubData(otherImage, otherData, 8 * 4, VG_lABGR_8888,
                 0, 0, 8, 8);
  vgSetGlyphToImage(font, 1, redChild, glyphOrigin, escapement);
  vgSetGlyphToImage(font, 2, greenChild, glyphOrigin, escapement);
  vgSetGlyphToImage(font, 3, blueChild, glyphOrigin, escapement);
  vgSetGlyphToPath(font, 4, path, VG_FALSE, glyphOrigin, escapement);
  vgSetGlyphToImage(font, 5, otherImage, glyphOrigin, escapement);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, yellow);
  vgSetPaint(paint, VG_FILL_PATH);
  if (expect_no_vg_error("OpenVG glyph image batch resource setup failed")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSetfv(VG_GLYPH_ORIGIN, 2, firstOrigin);
  vgDrawGlyphs(font, 3, imageRun, imageAdjustX, imageAdjustY,
               VG_FILL_PATH, VG_FALSE);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG batched image glyph draw failed") ||
      expect_rgba_at(pixels, width * 4, 12, 12, 255, 0, 0, 255,
                     "OpenVG batched image glyph draw lost the red glyph") ||
      expect_rgba_at(pixels, width * 4, 22, 12, 0, 255, 0, 255,
                     "OpenVG batched image glyph draw lost the green glyph") ||
      expect_rgba_at(pixels, width * 4, 33, 13, 0, 0, 255, 255,
                     "OpenVG batched image glyph draw ignored glyph adjustments")) {
    result = 1;
    goto cleanup;
  }

  vgClear(0, 0, width, height);
  vgSetfv(VG_GLYPH_ORIGIN, 2, secondOrigin);
  vgDrawGlyphs(font, 4, mixedRun, NULL, NULL, VG_FILL_PATH, VG_FALSE);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG mixed glyph batch draw failed") ||
      expect_rgba_at(pixels, width * 4, 12, 32, 255, 0, 0, 255,
                     "OpenVG mixed glyph batch lost the leading image glyph") ||
      expect_rgba_at(pixels, width * 4, 21, 32, 255, 255, 0, 255,
                     "OpenVG mixed glyph batch did not flush before a path glyph") ||
      expect_rgba_at(pixels, width * 4, 30, 32, 0, 255, 255, 255,
                     "OpenVG mixed glyph batch did not flush for a new root texture") ||
      expect_rgba_at(pixels, width * 4, 39, 32, 0, 255, 0, 255,
                     "OpenVG mixed glyph batch did not resume the original atlas")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (path != VG_INVALID_HANDLE)
    vgDestroyPath(path);
  if (font != VG_INVALID_HANDLE)
    vgDestroyFont(font);
  if (blueChild != VG_INVALID_HANDLE)
    vgDestroyImage(blueChild);
  if (greenChild != VG_INVALID_HANDLE)
    vgDestroyImage(greenChild);
  if (redChild != VG_INVALID_HANDLE)
    vgDestroyImage(redChild);
  if (otherImage != VG_INVALID_HANDLE)
    vgDestroyImage(otherImage);
  if (atlas != VG_INVALID_HANDLE)
    vgDestroyImage(atlas);
  return result;
}

static VGPath create_small_fill_rule_path(VGboolean innerSameDirection)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH,
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat sameDirectionCoords[] = {
    0.0f, 0.0f,
    12.0f, 0.0f,
    12.0f, 12.0f,
    0.0f, 12.0f,
    4.0f, 4.0f,
    8.0f, 4.0f,
    8.0f, 8.0f,
    4.0f, 8.0f
  };
  VGfloat oppositeDirectionCoords[] = {
    0.0f, 0.0f,
    12.0f, 0.0f,
    12.0f, 12.0f,
    0.0f, 12.0f,
    4.0f, 4.0f,
    4.0f, 8.0f,
    8.0f, 8.0f,
    8.0f, 4.0f
  };

  return create_test_path(segments, 10,
                          innerSameDirection ? sameDirectionCoords :
                                               oppositeDirectionCoords);
}

static int run_glyph_path_batch_test(unsigned char *pixels,
                                     EGLint width,
                                     EGLint height)
{
  VGFont font = VG_INVALID_HANDLE;
  VGPath square = VG_INVALID_HANDLE;
  VGPath sameDirection = VG_INVALID_HANDLE;
  VGPath oppositeDirection = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGfloat glyphOrigin[] = {0.0f, 0.0f};
  VGfloat squareEscapement[] = {10.0f, 0.0f};
  VGfloat compoundEscapement[] = {16.0f, 0.0f};
  VGfloat origin[] = {4.0f, 8.0f};
  VGfloat ruleOrigin[] = {4.0f, 28.0f};
  VGfloat overlapOrigin[] = {8.0f, 48.0f};
  VGfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGfloat redHalf[] = {1.0f, 0.0f, 0.0f, 0.5f};
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat transparent[] = {0.0f, 0.0f, 0.0f, 0.0f};
  VGuint disjointRun[] = {1u, 1u, 1u};
  VGfloat disjointAdjustX[] = {2.0f, 3.0f, 0.0f};
  VGfloat disjointAdjustY[] = {0.0f, 1.0f, 0.0f};
  VGuint ruleRun[] = {2u, 3u};
  VGuint overlapRun[] = {1u, 1u};
  VGfloat overlapAdjustX[] = {-6.0f, 0.0f};
  VGfloat finalOrigin[2];
  int result = 0;

  font = vgCreateFont(3);
  square = create_rect_path(6.0f, 6.0f);
  sameDirection = create_small_fill_rule_path(VG_TRUE);
  oppositeDirection = create_small_fill_rule_path(VG_FALSE);
  paint = vgCreatePaint();
  if (font == VG_INVALID_HANDLE ||
      square == VG_INVALID_HANDLE ||
      sameDirection == VG_INVALID_HANDLE ||
      oppositeDirection == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG glyph path batch setup failed");
    goto cleanup;
  }

  vgSetGlyphToPath(font, 1, square, VG_FALSE,
                   glyphOrigin, squareEscapement);
  vgSetGlyphToPath(font, 2, sameDirection, VG_FALSE,
                   glyphOrigin, compoundEscapement);
  vgSetGlyphToPath(font, 3, oppositeDirection, VG_FALSE,
                   glyphOrigin, compoundEscapement);
  if (expect_no_vg_error("OpenVG glyph path batch resource setup failed")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);
  vgSeti(VG_FILL_RULE, VG_EVEN_ODD);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_NONANTIALIASED);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, green);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  vgSetfv(VG_GLYPH_ORIGIN, 2, origin);
  vgDrawGlyphs(font, 3, disjointRun, disjointAdjustX, disjointAdjustY,
               VG_FILL_PATH, VG_FALSE);
  vgGetfv(VG_GLYPH_ORIGIN, 2, finalOrigin);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG batched path glyph draw failed") ||
      expect_rgba_at(pixels, width * 4, 6, 10, 0, 255, 0, 255,
                     "OpenVG batched path glyph draw lost the first glyph") ||
      expect_rgba_at(pixels, width * 4, 18, 10, 0, 255, 0, 255,
                     "OpenVG batched path glyph draw lost the second glyph") ||
      expect_rgba_at(pixels, width * 4, 31, 11, 0, 255, 0, 255,
                     "OpenVG batched path glyph draw ignored adjustments") ||
      expect_float_close("OpenVG batched path glyph draw advanced x origin",
                         finalOrigin[0], 39.0f) ||
      expect_float_close("OpenVG batched path glyph draw advanced y origin",
                         finalOrigin[1], 9.0f)) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_FILL_RULE, VG_NON_ZERO);
  vgClear(0, 0, width, height);
  vgSetfv(VG_GLYPH_ORIGIN, 2, ruleOrigin);
  vgDrawGlyphs(font, 2, ruleRun, NULL, NULL, VG_FILL_PATH, VG_FALSE);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG batched path glyph fill-rule draw failed") ||
      expect_rgba_at(pixels, width * 4, 10, 34, 0, 255, 0, 255,
                     "OpenVG batched path glyph nonzero fill lost same winding") ||
      expect_rgba_at(pixels, width * 4, 26, 34, 0, 0, 0, 255,
                     "OpenVG batched path glyph nonzero fill ignored opposite winding") ||
      expect_rgba_at(pixels, width * 4, 22, 30, 0, 255, 0, 255,
                     "OpenVG batched path glyph nonzero fill lost outer coverage")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_FILL_RULE, VG_EVEN_ODD);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, redHalf);
  vgSetfv(VG_CLEAR_COLOR, 4, transparent);
  vgClear(0, 0, width, height);
  vgSetfv(VG_GLYPH_ORIGIN, 2, overlapOrigin);
  vgDrawGlyphs(font, 2, overlapRun, overlapAdjustX, NULL,
               VG_FILL_PATH, VG_FALSE);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG overlapping path glyph fallback failed") ||
      expect_rgba_at(pixels, width * 4, 13, 52, 255, 0, 0, 192,
                     "OpenVG overlapping path glyph fallback changed alpha")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (oppositeDirection != VG_INVALID_HANDLE)
    vgDestroyPath(oppositeDirection);
  if (sameDirection != VG_INVALID_HANDLE)
    vgDestroyPath(sameDirection);
  if (square != VG_INVALID_HANDLE)
    vgDestroyPath(square);
  if (font != VG_INVALID_HANDLE)
    vgDestroyFont(font);
  return result;
}

static int run_shared_context_test(EGLDisplay display,
                                   EGLConfig config,
                                   EGLSurface surface,
                                   EGLContext baseContext,
                                   unsigned char *pixels,
                                   EGLint width,
                                   EGLint height)
{
  EGLContext sharedContext = EGL_NO_CONTEXT;
  VGPath rect = VG_INVALID_HANDLE;
  VGPath fullRect = VG_INVALID_HANDLE;
  VGPath glyphPath = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGImage image = VG_INVALID_HANDLE;
  VGFont font = VG_INVALID_HANDLE;
  VGMaskLayer maskLayer = VG_INVALID_HANDLE;
  VGubyte imageData[16 * 16 * 4];
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGfloat glyphOrigin[] = {0.0f, 0.0f};
  VGfloat escapement[] = {12.0f, 0.0f};
  VGfloat drawOrigin[] = {24.0f, 24.0f};
  int i;
  int result = 0;

  rect = create_rect_path(16.0f, 16.0f);
  fullRect = create_rect_path((VGfloat)width, (VGfloat)height);
  glyphPath = create_rect_path(10.0f, 10.0f);
  paint = vgCreatePaint();
  image = vgCreateImage(VG_sRGBA_8888, 16, 16, VG_IMAGE_QUALITY_BETTER);
  font = vgCreateFont(1);
  maskLayer = vgCreateMaskLayer(width, height);

  if (rect == VG_INVALID_HANDLE ||
      fullRect == VG_INVALID_HANDLE ||
      glyphPath == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      image == VG_INVALID_HANDLE ||
      font == VG_INVALID_HANDLE ||
      maskLayer == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG shared context test setup failed");
    goto cleanup;
  }

  for (i=0; i<16 * 16; ++i) {
    imageData[i * 4 + 0] = 255;
    imageData[i * 4 + 1] = 0;
    imageData[i * 4 + 2] = 0;
    imageData[i * 4 + 3] = 255;
  }

  vgImageSubData(image, imageData, 16 * 4, VG_sRGBA_8888,
                 0, 0, 16, 16);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, green);
  vgSetGlyphToPath(font, 7, glyphPath, VG_FALSE,
                   glyphOrigin, escapement);
  if (expect_no_vg_error("OpenVG shared context resource setup failed")) {
    result = 1;
    goto cleanup;
  }

  sharedContext = eglCreateContext(display, config, baseContext, NULL);
  if (sharedContext == EGL_NO_CONTEXT) {
    result = fail_egl("EGL could not create a shared OpenVG context");
    goto cleanup;
  }

  if (!eglMakeCurrent(display, surface, surface, sharedContext)) {
    result = fail_egl("EGL could not bind a shared OpenVG context");
    goto cleanup;
  }

  vgSetPaint(paint, VG_FILL_PATH);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);

  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG shared context path draw failed") ||
      expect_green_visibility(pixels, width, 8, 8, 1,
                              "OpenVG shared context could not draw a shared path")) {
    result = 1;
    goto cleanup;
  }

  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(24.0f, 0.0f);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG shared context image draw failed") ||
      expect_pixel(pixels, width, 32, 8, 192, 32, 32,
                   "OpenVG shared context could not draw a shared image")) {
    result = 1;
    goto cleanup;
  }

  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSetfv(VG_GLYPH_ORIGIN, 2, drawOrigin);
  vgDrawGlyph(font, 7, VG_FILL_PATH, VG_FALSE);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG shared context font draw failed") ||
      expect_green_visibility(pixels, width, 28, 28, 1,
                              "OpenVG shared context could not draw a shared font")) {
    result = 1;
    goto cleanup;
  }

  vgFillMaskLayer(maskLayer, 0, 0, width, height, 0.0f);
  vgFillMaskLayer(maskLayer, 0, 0, width / 2, height, 1.0f);
  vgSeti(VG_MASKING, VG_TRUE);
  vgMask(maskLayer, VG_SET_MASK, 0, 0, width, height);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgDrawPath(fullRect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG shared context mask layer draw failed") ||
      expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG shared context mask layer hid covered pixels") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 0,
                              "OpenVG shared context mask layer exposed uncovered pixels")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if (sharedContext != EGL_NO_CONTEXT)
    eglMakeCurrent(display, surface, surface, sharedContext);

  vgSeti(VG_MASKING, VG_FALSE);
  if (maskLayer != VG_INVALID_HANDLE)
    vgDestroyMaskLayer(maskLayer);
  if (font != VG_INVALID_HANDLE)
    vgDestroyFont(font);
  if (image != VG_INVALID_HANDLE)
    vgDestroyImage(image);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (glyphPath != VG_INVALID_HANDLE)
    vgDestroyPath(glyphPath);
  if (fullRect != VG_INVALID_HANDLE)
    vgDestroyPath(fullRect);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);

  if (sharedContext != EGL_NO_CONTEXT &&
      !eglMakeCurrent(display, surface, surface, baseContext) &&
      result == 0)
    result = fail_egl("EGL could not restore the base context after shared context test");

  if (sharedContext != EGL_NO_CONTEXT &&
      !eglDestroyContext(display, sharedContext) &&
      result == 0)
    result = fail_egl("EGL could not destroy the shared OpenVG context");

  if (result == 0 &&
      !eglMakeCurrent(display, surface, surface, baseContext))
    result = fail_egl("EGL could not leave the base context current after shared context test");

  return result;
}

static int run_client_buffer_pbuffer_test(EGLDisplay display,
                                          EGLConfig config,
                                          EGLSurface baseSurface,
                                          EGLContext context,
                                          unsigned char *pixels,
                                          EGLint width,
                                          EGLint height)
{
  const EGLint imageWidth = 16;
  const EGLint imageHeight = 16;
  EGLSurface imageSurface = EGL_NO_SURFACE;
  VGImage image = VG_INVALID_HANDLE;
  VGubyte imageData[16 * 16 * 4];
  VGfloat translucentRed[] = {1.0f, 0.0f, 0.0f, 0.5f};
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  EGLint textureAttribs[] = {
    EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
    EGL_NONE
  };
  EGLint value;
  int result = 0;

  image = vgCreateImage(VG_sRGBA_8888, imageWidth, imageHeight,
                        VG_IMAGE_QUALITY_BETTER);
  if (image == VG_INVALID_HANDLE)
    return fail_vg("OpenVG image-backed pbuffer setup failed");

  if (eglCreatePbufferFromClientBuffer(display, 0,
                                       (EGLClientBuffer)image,
                                       config, NULL) != EGL_NO_SURFACE ||
      expect_egl_error("EGL accepted an invalid client buffer type",
                       EGL_BAD_PARAMETER)) {
    result = 1;
    goto cleanup;
  }

  if (eglCreatePbufferFromClientBuffer(display, EGL_OPENVG_IMAGE,
                                       (EGLClientBuffer)VG_INVALID_HANDLE,
                                       config, NULL) != EGL_NO_SURFACE ||
      expect_egl_error("EGL accepted an invalid OpenVG image client buffer",
                       EGL_BAD_PARAMETER)) {
    result = 1;
    goto cleanup;
  }

  if (!eglMakeCurrent(display,
                      EGL_NO_SURFACE,
                      EGL_NO_SURFACE,
                      EGL_NO_CONTEXT)) {
    result = fail_egl("EGL could not release the current OpenVG context");
    goto cleanup;
  }

  if (eglCreatePbufferFromClientBuffer(display, EGL_OPENVG_IMAGE,
                                       (EGLClientBuffer)image,
                                       config, NULL) != EGL_NO_SURFACE ||
      expect_egl_error("EGL created an OpenVG image pbuffer without a current context",
                       EGL_BAD_ACCESS)) {
    result = 1;
    goto cleanup;
  }

  if (!eglMakeCurrent(display, baseSurface, baseSurface, context)) {
    result = fail_egl("EGL could not restore the base pbuffer context");
    goto cleanup;
  }

  if (eglCreatePbufferFromClientBuffer(display, EGL_OPENVG_IMAGE,
                                       (EGLClientBuffer)image,
                                       config,
                                       textureAttribs) != EGL_NO_SURFACE ||
      expect_egl_error("EGL accepted texture binding for an OpenVG image pbuffer",
                       EGL_BAD_MATCH)) {
    result = 1;
    goto cleanup;
  }

  imageSurface = eglCreatePbufferFromClientBuffer(display, EGL_OPENVG_IMAGE,
                                                 (EGLClientBuffer)image,
                                                 config, NULL);
  if (imageSurface == EGL_NO_SURFACE) {
    result = fail_egl("EGL OpenVG image pbuffer creation failed");
    goto cleanup;
  }

  if (eglCreatePbufferFromClientBuffer(display, EGL_OPENVG_IMAGE,
                                       (EGLClientBuffer)image,
                                       config, NULL) != EGL_NO_SURFACE ||
      expect_egl_error("EGL allowed one OpenVG image to back two pbuffers",
                       EGL_BAD_ACCESS)) {
    result = 1;
    goto cleanup;
  }

  if (!eglQuerySurface(display, imageSurface, EGL_WIDTH, &value) ||
      value != imageWidth ||
      !eglQuerySurface(display, imageSurface, EGL_HEIGHT, &value) ||
      value != imageHeight ||
      !eglQuerySurface(display, imageSurface, EGL_TEXTURE_FORMAT, &value) ||
      value != EGL_NO_TEXTURE ||
      !eglQuerySurface(display, imageSurface, EGL_TEXTURE_TARGET, &value) ||
      value != EGL_NO_TEXTURE ||
      !eglQuerySurface(display, imageSurface, EGL_MIPMAP_TEXTURE, &value) ||
      value != EGL_FALSE ||
      !eglQuerySurface(display, imageSurface, EGL_VG_COLORSPACE, &value) ||
      value != EGL_VG_COLORSPACE_sRGB ||
      !eglQuerySurface(display, imageSurface, EGL_VG_ALPHA_FORMAT, &value) ||
      value != EGL_VG_ALPHA_FORMAT_NONPRE) {
    result = fail_egl("EGL OpenVG image pbuffer surface query failed");
    goto cleanup;
  }

  if (!eglMakeCurrent(display, imageSurface, imageSurface, context)) {
    result = fail_egl("EGL could not bind an OpenVG image pbuffer");
    goto cleanup;
  }

  vgGetImageSubData(image, imageData, imageWidth * 4,
                    VG_sRGBA_8888, 0, 0, imageWidth, imageHeight);
  if (expect_vg_error("OpenVG allowed direct access to a current render target image",
                      VG_IMAGE_IN_USE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, translucentRed);
  vgClear(0, 0, imageWidth, imageHeight);
  vgFinish();
  vgReadPixels(pixels, imageWidth * 4,
               VG_sRGBA_8888, 0, 0, imageWidth, imageHeight);
  if (expect_no_vg_error("OpenVG image pbuffer rendering failed") ||
      expect_rgba_at(pixels, imageWidth * 4, 8, 8, 255, 0, 0, 128,
                     "OpenVG image pbuffer readback exposed premultiplied bytes")) {
    result = 1;
    goto cleanup;
  }

  if (!eglMakeCurrent(display, baseSurface, baseSurface, context)) {
    result = fail_egl("EGL could not restore the base pbuffer after image rendering");
    goto cleanup;
  }

  vgGetImageSubData(image, imageData, imageWidth * 4,
                    VG_sRGBA_8888, 0, 0, imageWidth, imageHeight);
  if (expect_no_vg_error("OpenVG could not read back a rendered image pbuffer") ||
      expect_rgba_at(imageData, imageWidth * 4, 8, 8, 128, 0, 0, 255,
                     "OpenVG image data did not use straight sRGBA pbuffer bytes")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG could not draw an image-backed pbuffer") ||
      expect_rgba_at(pixels, width * 4, 8, 8, 255, 0, 0, 128,
                     "OpenVG drawing double-premultiplied an image-backed pbuffer")) {
    result = 1;
    goto cleanup;
  }

  if (!eglDestroySurface(display, imageSurface)) {
    result = fail_egl("EGL could not destroy an OpenVG image pbuffer");
    goto cleanup;
  }
  imageSurface = EGL_NO_SURFACE;
  vgDestroyImage(image);
  image = VG_INVALID_HANDLE;

  image = vgCreateImage(VG_sRGBA_8888_PRE, imageWidth, imageHeight,
                        VG_IMAGE_QUALITY_BETTER);
  if (image == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG PRE image-backed pbuffer setup failed");
    goto cleanup;
  }

  imageSurface = eglCreatePbufferFromClientBuffer(display, EGL_OPENVG_IMAGE,
                                                 (EGLClientBuffer)image,
                                                 config, NULL);
  if (imageSurface == EGL_NO_SURFACE) {
    result = fail_egl("EGL PRE OpenVG image pbuffer creation failed");
    goto cleanup;
  }

  if (!eglQuerySurface(display, imageSurface, EGL_VG_COLORSPACE, &value) ||
      value != EGL_VG_COLORSPACE_sRGB ||
      !eglQuerySurface(display, imageSurface, EGL_VG_ALPHA_FORMAT, &value) ||
      value != EGL_VG_ALPHA_FORMAT_PRE) {
    result = fail_egl("EGL PRE OpenVG image pbuffer surface query failed");
    goto cleanup;
  }

cleanup:
  if ((eglGetCurrentContext() != context ||
       eglGetCurrentSurface(EGL_DRAW) != baseSurface) &&
      !eglMakeCurrent(display, baseSurface, baseSurface, context) &&
      result == 0)
    result = fail_egl("EGL could not restore the base pbuffer during cleanup");
  if (imageSurface != EGL_NO_SURFACE &&
      !eglDestroySurface(display, imageSurface) &&
      result == 0)
    result = fail_egl("EGL could not destroy an OpenVG image pbuffer");
  if (image != VG_INVALID_HANDLE)
    vgDestroyImage(image);
  if (result == 0 &&
      !eglMakeCurrent(display, baseSurface, baseSurface, context))
    result = fail_egl("EGL could not leave the base pbuffer current");
  return result;
}

static int run_mask_test(unsigned char *pixels, EGLint width, EGLint height)
{
  VGImage mask = VG_INVALID_HANDLE;
  VGImage rgbMask = VG_INVALID_HANDLE;
  VGMaskLayer maskLayer = VG_INVALID_HANDLE;
  VGMaskLayer copiedLayer = VG_INVALID_HANDLE;
  VGMaskLayer invalidLayer = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGPath rect = VG_INVALID_HANDLE;
  unsigned char *maskData;
  unsigned short *rgbMaskData = NULL;
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat paintColor[] = {0.0f, 1.0f, 0.0f, 1.0f};
  size_t left = ((size_t)(height / 2) * (size_t)width + (size_t)(width / 4)) * 4u;
  size_t right = ((size_t)(height / 2) * (size_t)width + (size_t)(3 * width / 4)) * 4u;
  EGLint x, y;
  int result = 0;

  maskData = (unsigned char*)calloc((size_t)width * (size_t)height, 1);
  if (!maskData)
    return 1;

  for (y=0; y<height; ++y) {
    for (x=0; x<width / 2; ++x)
      maskData[(size_t)y * (size_t)width + (size_t)x] = 255;
  }

  mask = vgCreateImage(VG_A_8, width, height, VG_IMAGE_QUALITY_FASTER);
  rgbMask = vgCreateImage(VG_sRGB_565, width, height,
                          VG_IMAGE_QUALITY_FASTER);
  maskLayer = vgCreateMaskLayer(width, height);
  copiedLayer = vgCreateMaskLayer(width, height);
  paint = vgCreatePaint();
  rect = create_rect_path((VGfloat)width, (VGfloat)height);

  if (mask == VG_INVALID_HANDLE ||
      rgbMask == VG_INVALID_HANDLE ||
      maskLayer == VG_INVALID_HANDLE ||
      copiedLayer == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      rect == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG mask test setup failed");
    goto cleanup;
  }

  vgImageSubData(mask, maskData, width, VG_A_8, 0, 0, width, height);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, paintColor);

  vgSeti(VG_MASKING, VG_TRUE);
  vgMask(mask, VG_SET_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] > 32) {
    fprintf(stderr, "OpenVG VG_SET_MASK did not clip drawing as expected\n");
    result = 1;
    goto cleanup;
  }

  for (y=0; y<height; ++y) {
    for (x=0; x<width; ++x)
      maskData[(size_t)y * (size_t)width + (size_t)x] =
        (x >= width / 2) ? 255 : 0;
  }

  vgImageSubData(mask, maskData, width, VG_A_8, 0, 0, width, height);
  vgMask(mask, VG_UNION_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] < 128) {
    fprintf(stderr, "OpenVG VG_UNION_MASK did not combine mask coverage\n");
    result = 1;
    goto cleanup;
  }

  vgMask(mask, VG_INTERSECT_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] > 32 || pixels[right + 1] < 128) {
    fprintf(stderr, "OpenVG VG_INTERSECT_MASK did not intersect mask coverage\n");
    result = 1;
    goto cleanup;
  }

  vgMask(mask, VG_SUBTRACT_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] > 32 || pixels[right + 1] > 32) {
    fprintf(stderr, "OpenVG VG_SUBTRACT_MASK did not remove mask coverage\n");
    result = 1;
    goto cleanup;
  }

  vgMask(VG_INVALID_HANDLE, VG_FILL_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] < 128) {
    fprintf(stderr, "OpenVG VG_FILL_MASK did not expose masked drawing\n");
    result = 1;
    goto cleanup;
  }

  vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] > 32 || pixels[right + 1] > 32) {
    fprintf(stderr, "OpenVG VG_CLEAR_MASK did not suppress masked drawing\n");
    result = 1;
    goto cleanup;
  }

  vgFillMaskLayer(maskLayer, 0, 0, width, height, 0.0f);
  vgFillMaskLayer(maskLayer, 0, 0, width / 2, height, 1.0f);
  vgMask(maskLayer, VG_SET_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] > 32) {
    fprintf(stderr, "OpenVG mask layer did not clip drawing as expected\n");
    result = 1;
    goto cleanup;
  }

  vgCopyMask(copiedLayer, 0, 0, 0, 0, width, height);
  vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK, 0, 0, width, height);
  vgMask(copiedLayer, VG_SET_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] > 32) {
    fprintf(stderr, "OpenVG vgCopyMask did not preserve current mask coverage\n");
    result = 1;
    goto cleanup;
  }

  rgbMaskData = (unsigned short*)calloc((size_t)width * (size_t)height,
                                        sizeof(unsigned short));
  if (!rgbMaskData) {
    result = 1;
    goto cleanup;
  }

  for (y=0; y<height; ++y) {
    for (x=0; x<width; ++x)
      rgbMaskData[(size_t)y * (size_t)width + (size_t)x] =
        (x < width / 2) ? 0xF800u : 0x0000u;
  }

  vgImageSubData(rgbMask, rgbMaskData,
                 width * (VGint)sizeof(unsigned short),
                 VG_sRGB_565, 0, 0, width, height);
  vgMask(rgbMask, VG_SET_MASK, 0, 0, width, height);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] > 32) {
    fprintf(stderr, "OpenVG no-alpha image mask did not use source red coverage\n");
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  draw_masked_rect(rect, paint, clearColor, pixels, width, height);

  if (pixels[left + 1] < 128 || pixels[right + 1] < 128) {
    fprintf(stderr, "OpenVG disabled masking still affected drawing\n");
    result = 1;
    goto cleanup;
  }

  vgMask(mask, (VGMaskOperation)0, 0, 0, width, height);
  if (expect_vg_error("OpenVG accepted an invalid mask operation",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgMask(mask, VG_SET_MASK, 0, 0, 0, height);
  if (expect_vg_error("OpenVG accepted an invalid mask extent",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgMask(VG_INVALID_HANDLE, VG_SET_MASK, 0, 0, width, height);
  if (expect_vg_error("OpenVG accepted an invalid mask image",
                      VG_BAD_HANDLE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  invalidLayer = vgCreateMaskLayer(0, height);
  if (invalidLayer != VG_INVALID_HANDLE ||
      expect_vg_error("OpenVG accepted an invalid mask layer size",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    if (invalidLayer != VG_INVALID_HANDLE)
      vgDestroyMaskLayer(invalidLayer);
    result = 1;
    goto cleanup;
  }

  vgFillMaskLayer(maskLayer, 0, 0, 0, height, 1.0f);
  if (expect_vg_error("OpenVG accepted an invalid mask layer fill extent",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgFillMaskLayer(maskLayer, -1, 0, 1, 1, 1.0f);
  if (expect_vg_error("OpenVG accepted a negative mask layer fill origin",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgFillMaskLayer(maskLayer, 0, 0, width + 1, height, 1.0f);
  if (expect_vg_error("OpenVG accepted an out-of-bounds mask layer fill",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgFillMaskLayer(maskLayer, 0, 0, 1, 1, -1.0f);
  if (expect_vg_error("OpenVG accepted an invalid mask layer fill value",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgCopyMask(VG_INVALID_HANDLE, 0, 0, 0, 0, width, height);
  if (expect_vg_error("OpenVG accepted an invalid mask layer copy target",
                      VG_BAD_HANDLE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgCopyMask(maskLayer, 0, 0, 0, 0, 0, height);
  if (expect_vg_error("OpenVG accepted an invalid mask copy extent",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

cleanup:
  vgSeti(VG_MASKING, VG_FALSE);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (copiedLayer != VG_INVALID_HANDLE)
    vgDestroyMaskLayer(copiedLayer);
  if (maskLayer != VG_INVALID_HANDLE)
    vgDestroyMaskLayer(maskLayer);
  if (rgbMask != VG_INVALID_HANDLE)
    vgDestroyImage(rgbMask);
  if (mask != VG_INVALID_HANDLE)
    vgDestroyImage(mask);
  free(rgbMaskData);
  free(maskData);
  return result;
}

static int run_render_to_mask_test(unsigned char *pixels,
                                   EGLint width, EGLint height)
{
  VGPaint paint = VG_INVALID_HANDLE;
  VGPath fullRect = VG_INVALID_HANDLE;
  VGPath halfRect = VG_INVALID_HANDLE;
  VGPath box = VG_INVALID_HANDLE;
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGfloat transparent[] = {0.0f, 0.0f, 0.0f, 0.0f};
  VGint scissor[] = {0, 0, width / 2, height};
  int result = 0;

  paint = vgCreatePaint();
  fullRect = create_rect_path((VGfloat)width, (VGfloat)height);
  halfRect = create_rect_path((VGfloat)(width / 2), (VGfloat)height);
  box = create_rect_path(24.0f, 24.0f);
  if (paint == VG_INVALID_HANDLE ||
      fullRect == VG_INVALID_HANDLE ||
      halfRect == VG_INVALID_HANDLE ||
      box == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG render-to-mask test setup failed");
    goto cleanup;
  }

  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, green);
  vgSeti(VG_MASKING, VG_TRUE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);

  vgLoadIdentity();
  vgRenderToMask(halfRect, VG_FILL_PATH, VG_SET_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask fill did not expose covered pixels") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask fill did not clear uncovered pixels")) {
    result = 1;
    goto cleanup;
  }

  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, transparent);
  vgLoadIdentity();
  vgTranslate((VGfloat)(width / 2), 0.0f);
  vgRenderToMask(halfRect, VG_FILL_PATH, VG_UNION_MASK);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, green);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask union lost previous coverage") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask union ignored path geometry")) {
    result = 1;
    goto cleanup;
  }

  vgLoadIdentity();
  vgRenderToMask(halfRect, VG_FILL_PATH, VG_INTERSECT_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask intersect removed covered pixels") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask intersect kept uncovered pixels")) {
    result = 1;
    goto cleanup;
  }

  vgRenderToMask(halfRect, VG_FILL_PATH, VG_FILL_MASK);
  vgLoadIdentity();
  vgRenderToMask(halfRect, VG_FILL_PATH, VG_SUBTRACT_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask subtract kept subtracted pixels") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask subtract removed untouched pixels")) {
    result = 1;
    goto cleanup;
  }

  vgRenderToMask(halfRect, VG_FILL_PATH, VG_CLEAR_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask clear did not clear the surface mask") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask clear was limited by path coverage")) {
    result = 1;
    goto cleanup;
  }

  vgRenderToMask(halfRect, VG_FILL_PATH, VG_FILL_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask fill-mask did not fill the mask") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask fill-mask was limited by path coverage")) {
    result = 1;
    goto cleanup;
  }

  vgSetf(VG_STROKE_LINE_WIDTH, 8.0f);
  vgLoadIdentity();
  vgTranslate(20.0f, 20.0f);
  vgRenderToMask(box, VG_FILL_PATH | VG_STROKE_PATH, VG_SET_MASK);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, 20, 32, 1,
                              "OpenVG vgRenderToMask stroke pass did not affect the stroke edge") ||
      expect_green_visibility(pixels, width, 32, 32, 0,
                              "OpenVG vgRenderToMask fill+stroke did not apply stroke as the second pass")) {
    result = 1;
    goto cleanup;
  }

  vgSetiv(VG_SCISSOR_RECTS, 4, scissor);
  vgSeti(VG_SCISSORING, VG_TRUE);
  vgLoadIdentity();
  vgRenderToMask(fullRect, VG_FILL_PATH, VG_SET_MASK);
  vgSeti(VG_SCISSORING, VG_FALSE);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask scissor removed covered pixels") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask ignored scissoring")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_MASKING, VG_FALSE);
  vgLoadIdentity();
  vgRenderToMask(halfRect, VG_FILL_PATH, VG_SET_MASK);
  vgSeti(VG_MASKING, VG_TRUE);
  draw_masked_rect(fullRect, paint, clearColor, pixels, width, height);
  if (expect_green_visibility(pixels, width, width / 4, height / 2, 1,
                              "OpenVG vgRenderToMask did not modify the mask while masking was disabled") ||
      expect_green_visibility(pixels, width, 3 * width / 4, height / 2, 0,
                              "OpenVG vgRenderToMask disabled-masking update leaked coverage")) {
    result = 1;
    goto cleanup;
  }

  if (expect_no_vg_error("OpenVG vgRenderToMask produced an unexpected error")) {
    result = 1;
    goto cleanup;
  }

  vgRenderToMask(VG_INVALID_HANDLE, VG_FILL_PATH, VG_SET_MASK);
  if (expect_vg_error("OpenVG accepted an invalid render-to-mask path",
                      VG_BAD_HANDLE_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgRenderToMask(halfRect, VG_FILL_PATH | 0x4000, VG_SET_MASK);
  if (expect_vg_error("OpenVG accepted invalid render-to-mask paint modes",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

  vgRenderToMask(halfRect, VG_FILL_PATH, (VGMaskOperation)0);
  if (expect_vg_error("OpenVG accepted an invalid render-to-mask operation",
                      VG_ILLEGAL_ARGUMENT_ERROR)) {
    result = 1;
    goto cleanup;
  }

cleanup:
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSetf(VG_STROKE_LINE_WIDTH, 1.0f);
  if (box != VG_INVALID_HANDLE)
    vgDestroyPath(box);
  if (halfRect != VG_INVALID_HANDLE)
    vgDestroyPath(halfRect);
  if (fullRect != VG_INVALID_HANDLE)
    vgDestroyPath(fullRect);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  return result;
}

static int run_rendering_quality_antialias_test(unsigned char *pixels,
                                                EGLint width, EGLint height)
{
  VGPaint paint = VG_INVALID_HANDLE;
  VGPath rect = VG_INVALID_HANDLE;
  VGPath fullRect = VG_INVALID_HANDLE;
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat red[] = {1.0f, 0.0f, 0.0f, 1.0f};
  VGfloat green[] = {0.0f, 1.0f, 0.0f, 1.0f};
  VGint edgeX = 16;
  VGint fullX = 17;
  VGint sampleY = 32;
  int result = 0;

  paint = vgCreatePaint();
  rect = create_rect_path(32.0f, 32.0f);
  fullRect = create_rect_path((VGfloat)width, (VGfloat)height);
  if (paint == VG_INVALID_HANDLE ||
      rect == VG_INVALID_HANDLE ||
      fullRect == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG antialiasing test setup failed");
    goto cleanup;
  }

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSetPaint(paint, VG_FILL_PATH);

  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, red);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_NONANTIALIASED);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(16.30f, 16.0f);
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
               0, 0, width, height);
  if (expect_channel_between(pixels, width * 4, edgeX, sampleY, 0,
                             248, 255,
                             "OpenVG non-antialiased edge was not binary")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_FASTER);
  vgClear(0, 0, width, height);
  vgLoadIdentity();
  vgTranslate(16.30f, 16.0f);
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
               0, 0, width, height);
  if (expect_channel_between(pixels, width * 4, edgeX, sampleY, 0,
                             32, 224,
                             "OpenVG faster antialiasing did not produce partial coverage") ||
      expect_channel_between(pixels, width * 4, fullX, sampleY, 0,
                             248, 255,
                             "OpenVG faster antialiasing lost full interior coverage")) {
    result = 1;
    goto cleanup;
  }

  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
  vgClear(0, 0, width, height);
  vgLoadIdentity();
  vgTranslate(16.30f, 16.0f);
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
               0, 0, width, height);
  if (expect_channel_between(pixels, width * 4, edgeX, sampleY, 0,
                             32, 240,
                             "OpenVG better antialiasing did not produce partial coverage") ||
      expect_channel_between(pixels, width * 4, fullX, sampleY, 0,
                             248, 255,
                             "OpenVG better antialiasing lost full interior coverage")) {
    result = 1;
    goto cleanup;
  }

  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, green);
  vgSeti(VG_MASKING, VG_FALSE);
  vgLoadIdentity();
  vgRenderToMask(fullRect, VG_FILL_PATH, VG_CLEAR_MASK);
  vgLoadIdentity();
  vgTranslate(16.30f, 16.0f);
  vgRenderToMask(rect, VG_FILL_PATH, VG_SET_MASK);

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_MASKING, VG_TRUE);
  vgLoadIdentity();
  vgDrawPath(fullRect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
               0, 0, width, height);
  if (expect_channel_between(pixels, width * 4, edgeX, sampleY, 1,
                             32, 240,
                             "OpenVG vgRenderToMask antialiasing did not produce partial mask coverage") ||
      expect_channel_between(pixels, width * 4, fullX, sampleY, 1,
                             248, 255,
                             "OpenVG vgRenderToMask antialiasing lost full mask coverage")) {
    result = 1;
    goto cleanup;
  }

  if (expect_no_vg_error("OpenVG antialiasing test produced an unexpected error"))
    result = 1;

cleanup:
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
  if (fullRect != VG_INVALID_HANDLE)
    vgDestroyPath(fullRect);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  return result;
}

static int run_user_fragment_antialias_coverage_test(unsigned char *pixels,
                                                     EGLint width,
                                                     EGLint height)
{
  static const VGbyte fragmentShader[] =
    "void shMain(){\n"
    "    fragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";
  VGPaint paint = VG_INVALID_HANDLE;
  VGPath rect = VG_INVALID_HANDLE;
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat red[] = {1.0f, 0.0f, 0.0f, 1.0f};
  VGint fullX = 17;
  VGint sampleY = 32;
  int result = 0;

  vgShaderSourceSH(VG_VERTEX_SHADER_SH, NULL);
  vgShaderSourceSH(VG_FRAGMENT_SHADER_SH, fragmentShader);
  vgCompileShaderSH();

  paint = vgCreatePaint();
  rect = create_rect_path(32.0f, 32.0f);
  if (paint == VG_INVALID_HANDLE || rect == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG user fragment antialiasing setup failed");
    goto cleanup;
  }

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, red);
  vgSetPaint(paint, VG_FILL_PATH);
  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(16.30f, 16.0f);
  vgDrawPath(rect, VG_FILL_PATH);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888,
               0, 0, width, height);

  if (expect_channel_between(pixels, width * 4, fullX, sampleY, 1,
                             248, 255,
                             "OpenVG antialiasing coverage used the user fragment shader") ||
      expect_channel_between(pixels, width * 4, fullX, sampleY, 0,
                             0, 8,
                             "OpenVG user fragment shader did not control final color")) {
    result = 1;
    goto cleanup;
  }

  if (expect_no_vg_error("OpenVG user fragment antialiasing test failed"))
    result = 1;

cleanup:
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
  if (rect != VG_INVALID_HANDLE)
    vgDestroyPath(rect);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  vgShaderSourceSH(VG_VERTEX_SHADER_SH, NULL);
  vgShaderSourceSH(VG_FRAGMENT_SHADER_SH, NULL);
  vgCompileShaderSH();
  return result;
}

static int expect_hardware_query(const char *message,
                                 VGHardwareQueryType key,
                                 VGint setting)
{
  VGHardwareQueryResult result = vgHardwareQuery(key, setting);
  VGErrorCode error = vgGetError();

  if (result == VG_HARDWARE_UNACCELERATED && error == VG_NO_ERROR)
    return 0;

  fprintf(stderr, "%s (result 0x%04x, VG error 0x%04x)\n",
          message, result, error);
  return 1;
}

static int run_hardware_query_test(void)
{
  if (expect_hardware_query("OpenVG rejected a valid image hardware query",
                            VG_IMAGE_FORMAT_QUERY, VG_sRGBA_8888))
    return 1;

  if (expect_hardware_query("OpenVG rejected a valid unsupported image hardware query",
                            VG_IMAGE_FORMAT_QUERY, VG_BW_1))
    return 1;

  if (expect_hardware_query("OpenVG rejected a valid path datatype hardware query",
                            VG_PATH_DATATYPE_QUERY, VG_PATH_DATATYPE_F))
    return 1;

  vgHardwareQuery((VGHardwareQueryType)0, VG_sRGBA_8888);
  if (expect_vg_error("OpenVG accepted an invalid hardware query key",
                      VG_ILLEGAL_ARGUMENT_ERROR))
    return 1;

  vgHardwareQuery(VG_IMAGE_FORMAT_QUERY, 0x5555);
  if (expect_vg_error("OpenVG accepted an invalid image hardware query setting",
                      VG_ILLEGAL_ARGUMENT_ERROR))
    return 1;

  vgHardwareQuery(VG_PATH_DATATYPE_QUERY, 0x5555);
  if (expect_vg_error("OpenVG accepted an invalid path datatype hardware query setting",
                      VG_ILLEGAL_ARGUMENT_ERROR))
    return 1;

  return 0;
}

static int expect_channel_between(const VGubyte *data,
                                  VGint stride,
                                  VGint x,
                                  VGint y,
                                  int channel,
                                  VGubyte minValue,
                                  VGubyte maxValue,
                                  const char *message)
{
  size_t sample = (size_t)y * (size_t)stride + (size_t)x * 4u +
                  (size_t)channel;
  VGubyte value = data[sample];

  if (value >= minValue && value <= maxValue)
    return 0;

  fprintf(stderr,
          "%s (channel %d at %d,%d got %u, expected %u..%u)\n",
          message, channel, x, y, value, minValue, maxValue);
  return 1;
}

static int warp_close(VGfloat a, VGfloat b)
{
  VGfloat diff = a - b;
  if (diff < 0.0f)
    diff = -diff;

  return diff <= WARP_TEST_EPSILON;
}

static void apply_warp(const VGfloat *matrix,
                       VGfloat x, VGfloat y,
                       VGfloat *outX, VGfloat *outY)
{
  VGfloat denominator = matrix[2] * x + matrix[5] * y + matrix[8];

  *outX = (matrix[0] * x + matrix[3] * y + matrix[6]) / denominator;
  *outY = (matrix[1] * x + matrix[4] * y + matrix[7]) / denominator;
}

static int expect_warp_point(const char *message,
                             const VGfloat *matrix,
                             VGfloat sourceX, VGfloat sourceY,
                             VGfloat expectedX, VGfloat expectedY)
{
  VGfloat actualX, actualY;

  apply_warp(matrix, sourceX, sourceY, &actualX, &actualY);
  if (warp_close(actualX, expectedX) &&
      warp_close(actualY, expectedY))
    return 0;

  fprintf(stderr, "%s (expected %.4f,%.4f, got %.4f,%.4f)\n",
          message, expectedX, expectedY, actualX, actualY);
  return 1;
}

static int expect_vgu_error(const char *message,
                            VGUErrorCode actual,
                            VGUErrorCode expected)
{
  if (actual == expected)
    return 0;

  fprintf(stderr, "%s (expected VGU error 0x%04x, got 0x%04x)\n",
          message, expected, actual);
  return 1;
}

static int matrix_unchanged(const VGfloat *before, const VGfloat *after)
{
  int i;

  for (i=0; i<9; ++i) {
    if (!warp_close(before[i], after[i]))
      return 0;
  }

  return 1;
}

static int run_warp_test(void)
{
  VGfloat matrix[9];
  VGfloat before[9];
  VGfloat source[] = {
    3.0f, 4.0f,
    44.0f, 6.0f,
    8.0f, 50.0f,
    48.0f, 45.0f
  };
  VGfloat destination[] = {
    6.0f, 7.0f,
    52.0f, 11.0f,
    10.0f, 55.0f,
    58.0f, 48.0f
  };
  int i;

  if (expect_vgu_error("VGU rejected identity square-to-quad warp",
                       vguComputeWarpSquareToQuad(0.0f, 0.0f,
                                                  1.0f, 0.0f,
                                                  0.0f, 1.0f,
                                                  1.0f, 1.0f,
                                                  matrix),
                       VGU_NO_ERROR))
    return 1;

  if (expect_warp_point("Identity warp moved lower-left corner",
                        matrix, 0.0f, 0.0f, 0.0f, 0.0f) ||
      expect_warp_point("Identity warp moved lower-right corner",
                        matrix, 1.0f, 0.0f, 1.0f, 0.0f) ||
      expect_warp_point("Identity warp moved upper-left corner",
                        matrix, 0.0f, 1.0f, 0.0f, 1.0f) ||
      expect_warp_point("Identity warp moved upper-right corner",
                        matrix, 1.0f, 1.0f, 1.0f, 1.0f))
    return 1;

  if (expect_vgu_error("VGU rejected square-to-quad warp",
                       vguComputeWarpSquareToQuad(destination[0], destination[1],
                                                  destination[2], destination[3],
                                                  destination[4], destination[5],
                                                  destination[6], destination[7],
                                                  matrix),
                       VGU_NO_ERROR))
    return 1;

  if (expect_warp_point("Square-to-quad warp missed destination 0",
                        matrix, 0.0f, 0.0f,
                        destination[0], destination[1]) ||
      expect_warp_point("Square-to-quad warp missed destination 1",
                        matrix, 1.0f, 0.0f,
                        destination[2], destination[3]) ||
      expect_warp_point("Square-to-quad warp missed destination 2",
                        matrix, 0.0f, 1.0f,
                        destination[4], destination[5]) ||
      expect_warp_point("Square-to-quad warp missed destination 3",
                        matrix, 1.0f, 1.0f,
                        destination[6], destination[7]))
    return 1;

  if (expect_vgu_error("VGU rejected quad-to-square warp",
                       vguComputeWarpQuadToSquare(destination[0], destination[1],
                                                  destination[2], destination[3],
                                                  destination[4], destination[5],
                                                  destination[6], destination[7],
                                                  matrix),
                       VGU_NO_ERROR))
    return 1;

  if (expect_warp_point("Quad-to-square warp missed source 0",
                        matrix, destination[0], destination[1],
                        0.0f, 0.0f) ||
      expect_warp_point("Quad-to-square warp missed source 1",
                        matrix, destination[2], destination[3],
                        1.0f, 0.0f) ||
      expect_warp_point("Quad-to-square warp missed source 2",
                        matrix, destination[4], destination[5],
                        0.0f, 1.0f) ||
      expect_warp_point("Quad-to-square warp missed source 3",
                        matrix, destination[6], destination[7],
                        1.0f, 1.0f))
    return 1;

  if (expect_vgu_error("VGU rejected quad-to-quad warp",
                       vguComputeWarpQuadToQuad(destination[0], destination[1],
                                                destination[2], destination[3],
                                                destination[4], destination[5],
                                                destination[6], destination[7],
                                                source[0], source[1],
                                                source[2], source[3],
                                                source[4], source[5],
                                                source[6], source[7],
                                                matrix),
                       VGU_NO_ERROR))
    return 1;

  if (expect_warp_point("Quad-to-quad warp missed corner 0",
                        matrix, source[0], source[1],
                        destination[0], destination[1]) ||
      expect_warp_point("Quad-to-quad warp missed corner 1",
                        matrix, source[2], source[3],
                        destination[2], destination[3]) ||
      expect_warp_point("Quad-to-quad warp missed corner 2",
                        matrix, source[4], source[5],
                        destination[4], destination[5]) ||
      expect_warp_point("Quad-to-quad warp missed corner 3",
                        matrix, source[6], source[7],
                        destination[6], destination[7]))
    return 1;

  for (i=0; i<9; ++i)
    before[i] = matrix[i] = (VGfloat)(i + 10);

  if (expect_vgu_error("VGU accepted a degenerate warp",
                       vguComputeWarpSquareToQuad(0.0f, 0.0f,
                                                  16.0f, 0.0f,
                                                  32.0f, 0.0f,
                                                  48.0f, 0.0f,
                                                  matrix),
                       VGU_BAD_WARP_ERROR))
    return 1;

  if (!matrix_unchanged(before, matrix)) {
    fprintf(stderr, "VGU degenerate warp modified the output matrix\n");
    return 1;
  }

  if (expect_vgu_error("VGU accepted a null warp output matrix",
                       vguComputeWarpSquareToQuad(0.0f, 0.0f,
                                                  1.0f, 0.0f,
                                                  0.0f, 1.0f,
                                                  1.0f, 1.0f,
                                                  NULL),
                       VGU_ILLEGAL_ARGUMENT_ERROR))
    return 1;

  return 0;
}

static int run_alignment_validation_test(void)
{
  VGubyte floatStorage[sizeof(VGfloat) * 64u + 8u];
  VGubyte intStorage[sizeof(VGint) * 16u + 8u];
  VGubyte shortStorage[sizeof(VGshort) * 4u + 8u];
  VGubyte uintStorage[sizeof(VGuint) * 260u + 8u];
  VGubyte pixel2Storage[32u];
  VGubyte pixel4Storage[64u];
  VGfloat *badFloat;
  VGint *badInt;
  VGshort *badShort;
  VGuint *badUint;
  VGubyte *badPixel2;
  VGubyte *badPixel4;
  VGPath path = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGFont font = VG_INVALID_HANDLE;
  VGImage src = VG_INVALID_HANDLE;
  VGImage dst = VG_INVALID_HANDLE;
  VGubyte lineSegments[] = {VG_MOVE_TO_ABS, VG_LINE_TO_ABS};
  VGfloat lineCoords[] = {0.0f, 0.0f, 10.0f, 0.0f};
  VGfloat bounds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  VGfloat glyphOrigin[] = {0.0f, 0.0f};
  VGfloat escapement[] = {12.0f, 0.0f};
  VGuint glyphIndices[] = {0u};
  VGshort goodKernel[] = {1};
  int result = 0;

#define EXPECT_VG_ILLEGAL(call, message) \
  do { \
    vgGetError(); \
    call; \
    if (expect_vg_error(message, VG_ILLEGAL_ARGUMENT_ERROR)) { \
      result = 1; \
      goto cleanup; \
    } \
  } while (0)

#define EXPECT_VGU_ILLEGAL(call, message) \
  do { \
    vgGetError(); \
    if (expect_vgu_error(message, (call), VGU_ILLEGAL_ARGUMENT_ERROR)) { \
      result = 1; \
      goto cleanup; \
    } \
  } while (0)

  memset(floatStorage, 0, sizeof(floatStorage));
  memset(intStorage, 0, sizeof(intStorage));
  memset(shortStorage, 0, sizeof(shortStorage));
  memset(uintStorage, 0, sizeof(uintStorage));
  memset(pixel2Storage, 0, sizeof(pixel2Storage));
  memset(pixel4Storage, 0, sizeof(pixel4Storage));

  badFloat = (VGfloat*)misaligned_pointer(floatStorage,
                                          sizeof(floatStorage),
                                          sizeof(VGfloat));
  badInt = (VGint*)misaligned_pointer(intStorage,
                                      sizeof(intStorage),
                                      sizeof(VGint));
  badShort = (VGshort*)misaligned_pointer(shortStorage,
                                          sizeof(shortStorage),
                                          sizeof(VGshort));
  badUint = (VGuint*)misaligned_pointer(uintStorage,
                                        sizeof(uintStorage),
                                        sizeof(VGuint));
  badPixel2 = (VGubyte*)misaligned_pointer(pixel2Storage,
                                           sizeof(pixel2Storage), 2u);
  badPixel4 = (VGubyte*)misaligned_pointer(pixel4Storage,
                                           sizeof(pixel4Storage), 4u);
  if (!badFloat || !badInt || !badShort || !badUint ||
      !badPixel2 || !badPixel4) {
    fprintf(stderr, "Could not construct misaligned test pointers\n");
    result = 1;
    goto cleanup;
  }

  path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
                      1.0f, 0.0f, 2, 4, VG_PATH_CAPABILITY_ALL);
  paint = vgCreatePaint();
  font = vgCreateFont(1);
  src = vgCreateImage(VG_sRGBA_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  dst = vgCreateImage(VG_sRGBA_8888, 4, 4, VG_IMAGE_QUALITY_BETTER);
  if (path == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      font == VG_INVALID_HANDLE ||
      src == VG_INVALID_HANDLE ||
      dst == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG alignment test setup failed");
    goto cleanup;
  }

  vgAppendPathData(path, 2, lineSegments, lineCoords);
  if (expect_no_vg_error("OpenVG alignment path setup failed")) {
    result = 1;
    goto cleanup;
  }

  EXPECT_VG_ILLEGAL(vgSetfv(VG_CLEAR_COLOR, 4, badFloat),
                    "OpenVG accepted misaligned vgSetfv data");
  EXPECT_VG_ILLEGAL(vgSetiv(VG_SCISSOR_RECTS, 4, badInt),
                    "OpenVG accepted misaligned vgSetiv data");
  EXPECT_VG_ILLEGAL(vgGetfv(VG_CLEAR_COLOR, 4, badFloat),
                    "OpenVG accepted misaligned vgGetfv data");
  EXPECT_VG_ILLEGAL(vgGetiv(VG_MATRIX_MODE, 1, badInt),
                    "OpenVG accepted misaligned vgGetiv data");

  EXPECT_VG_ILLEGAL(vgSetParameterfv(paint, VG_PAINT_COLOR, 4, badFloat),
                    "OpenVG accepted misaligned vgSetParameterfv data");
  EXPECT_VG_ILLEGAL(vgSetParameteriv(paint,
                                     VG_PAINT_COLOR_RAMP_PREMULTIPLIED,
                                     1, badInt),
                    "OpenVG accepted misaligned vgSetParameteriv data");
  EXPECT_VG_ILLEGAL(vgGetParameterfv(paint, VG_PAINT_COLOR, 4, badFloat),
                    "OpenVG accepted misaligned vgGetParameterfv data");
  EXPECT_VG_ILLEGAL(vgGetParameteriv(paint, VG_PAINT_TYPE, 1, badInt),
                    "OpenVG accepted misaligned vgGetParameteriv data");

  EXPECT_VG_ILLEGAL(vgLoadMatrix(badFloat),
                    "OpenVG accepted misaligned vgLoadMatrix data");
  EXPECT_VG_ILLEGAL(vgGetMatrix(badFloat),
                    "OpenVG accepted misaligned vgGetMatrix data");
  EXPECT_VG_ILLEGAL(vgMultMatrix(badFloat),
                    "OpenVG accepted misaligned vgMultMatrix data");

  EXPECT_VG_ILLEGAL(vgAppendPathData(path, 2, lineSegments, badFloat),
                    "OpenVG accepted misaligned vgAppendPathData coordinates");
  EXPECT_VG_ILLEGAL(vgModifyPathCoords(path, 0, 1, badFloat),
                    "OpenVG accepted misaligned vgModifyPathCoords data");
  EXPECT_VG_ILLEGAL(vgPathBounds(path,
                                 badFloat,
                                 &bounds[1],
                                 &bounds[2],
                                 &bounds[3]),
                    "OpenVG accepted misaligned vgPathBounds output");
  EXPECT_VG_ILLEGAL(vgPathTransformedBounds(path,
                                            badFloat,
                                            &bounds[1],
                                            &bounds[2],
                                            &bounds[3]),
                    "OpenVG accepted misaligned vgPathTransformedBounds output");
  EXPECT_VG_ILLEGAL(vgPointAlongPath(path, 0, 1, 0.5f,
                                     badFloat, &bounds[1],
                                     NULL, NULL),
                    "OpenVG accepted misaligned vgPointAlongPath output");

  EXPECT_VG_ILLEGAL(vgSetGlyphToPath(font, 0, path, VG_FALSE,
                                     badFloat, escapement),
                    "OpenVG accepted misaligned vgSetGlyphToPath origin");
  EXPECT_VG_ILLEGAL(vgSetGlyphToImage(font, 0, src,
                                      badFloat, escapement),
                    "OpenVG accepted misaligned vgSetGlyphToImage origin");
  vgSetGlyphToPath(font, 0, path, VG_FALSE, glyphOrigin, escapement);
  if (expect_no_vg_error("OpenVG alignment glyph setup failed")) {
    result = 1;
    goto cleanup;
  }
  EXPECT_VG_ILLEGAL(vgDrawGlyphs(font, 1, badUint,
                                 NULL, NULL,
                                 VG_FILL_PATH, VG_FALSE),
                    "OpenVG accepted misaligned vgDrawGlyphs indices");
  EXPECT_VG_ILLEGAL(vgDrawGlyphs(font, 1, glyphIndices,
                                 badFloat, NULL,
                                 VG_FILL_PATH, VG_FALSE),
                    "OpenVG accepted misaligned vgDrawGlyphs x adjustments");
  EXPECT_VG_ILLEGAL(vgDrawGlyphs(font, 1, glyphIndices,
                                 NULL, badFloat,
                                 VG_FILL_PATH, VG_FALSE),
                    "OpenVG accepted misaligned vgDrawGlyphs y adjustments");

  EXPECT_VG_ILLEGAL(vgImageSubData(src, badPixel4, 16,
                                   VG_sRGBA_8888, 0, 0, 1, 1),
                    "OpenVG accepted misaligned 4-byte image upload data");
  EXPECT_VG_ILLEGAL(vgGetImageSubData(src, badPixel4, 16,
                                      VG_sRGBA_8888, 0, 0, 1, 1),
                    "OpenVG accepted misaligned 4-byte image readback data");
  EXPECT_VG_ILLEGAL(vgWritePixels(badPixel4, 16,
                                  VG_sRGBA_8888, 0, 0, 1, 1),
                    "OpenVG accepted misaligned 4-byte write pixels data");
  EXPECT_VG_ILLEGAL(vgReadPixels(badPixel4, 16,
                                 VG_sRGBA_8888, 0, 0, 1, 1),
                    "OpenVG accepted misaligned 4-byte read pixels data");
  EXPECT_VG_ILLEGAL(vgImageSubData(src, badPixel2, 2,
                                   VG_sRGB_565, 0, 0, 1, 1),
                    "OpenVG accepted misaligned 2-byte image upload data");
  EXPECT_VG_ILLEGAL(vgWritePixels(badPixel2, 2,
                                  VG_sRGB_565, 0, 0, 1, 1),
                    "OpenVG accepted misaligned 2-byte write pixels data");

  EXPECT_VG_ILLEGAL(vgColorMatrix(dst, src, badFloat),
                    "OpenVG accepted misaligned color matrix data");
  EXPECT_VG_ILLEGAL(vgConvolve(dst, src, 1, 1, 0, 0,
                               badShort, 1.0f, 0.0f, VG_TILE_PAD),
                    "OpenVG accepted misaligned convolution kernel data");
  EXPECT_VG_ILLEGAL(vgSeparableConvolve(dst, src, 1, 1, 0, 0,
                                        badShort, goodKernel,
                                        1.0f, 0.0f, VG_TILE_PAD),
                    "OpenVG accepted misaligned separable X kernel data");
  EXPECT_VG_ILLEGAL(vgSeparableConvolve(dst, src, 1, 1, 0, 0,
                                        goodKernel, badShort,
                                        1.0f, 0.0f, VG_TILE_PAD),
                    "OpenVG accepted misaligned separable Y kernel data");
  EXPECT_VG_ILLEGAL(vgLookupSingle(dst, src, badUint,
                                   VG_RED, VG_FALSE, VG_FALSE),
                    "OpenVG accepted misaligned lookup table data");

  EXPECT_VGU_ILLEGAL(vguPolygon(path, badFloat, 2, VG_FALSE),
                     "VGU accepted misaligned polygon point data");
  EXPECT_VGU_ILLEGAL(vguComputeWarpSquareToQuad(0.0f, 0.0f,
                                                1.0f, 0.0f,
                                                0.0f, 1.0f,
                                                1.0f, 1.0f,
                                                badFloat),
                     "VGU accepted misaligned warp output data");
  EXPECT_VGU_ILLEGAL(vguGradientGlowKHR(dst, src,
                                        2.0f, 2.0f, 1u,
                                        1.0f, 0.0f, 0.0f,
                                        VG_RED | VG_GREEN |
                                        VG_BLUE | VG_ALPHA,
                                        VG_IMAGE_QUALITY_BETTER,
                                        1u, badFloat),
                     "VGU accepted misaligned gradient glow stop data");

cleanup:
  if (dst != VG_INVALID_HANDLE)
    vgDestroyImage(dst);
  if (src != VG_INVALID_HANDLE)
    vgDestroyImage(src);
  if (font != VG_INVALID_HANDLE)
    vgDestroyFont(font);
  if (paint != VG_INVALID_HANDLE)
    vgDestroyPaint(paint);
  if (path != VG_INVALID_HANDLE)
    vgDestroyPath(path);

#undef EXPECT_VG_ILLEGAL
#undef EXPECT_VGU_ILLEGAL

  return result;
}

static int expect_alpha_mask_config(EGLDisplay display,
                                    EGLint requestedSize,
                                    EGLBoolean expectMatch,
                                    EGLConfig *configOut)
{
  EGLConfig config;
  EGLint count = 0;
  EGLint actualSize = -1;
  EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_STENCIL_SIZE, 8,
    EGL_ALPHA_MASK_SIZE, requestedSize,
    EGL_NONE
  };

  if (!eglChooseConfig(display, configAttribs, &config, 1, &count))
    return fail_egl("EGL OpenVG alpha mask config selection failed");

  if (!expectMatch) {
    if (count != 0) {
      fprintf(stderr,
              "EGL_ALPHA_MASK_SIZE %d unexpectedly matched %d configs\n",
              requestedSize, count);
      return 1;
    }
    return 0;
  }

  if (count < 1) {
    fprintf(stderr,
            "EGL_ALPHA_MASK_SIZE %d did not match an OpenVG config\n",
            requestedSize);
    return 1;
  }

  if (!eglGetConfigAttrib(display, config,
                          EGL_ALPHA_MASK_SIZE, &actualSize))
    return fail_egl("EGL OpenVG alpha mask config query failed");

  if (actualSize != 8) {
    fprintf(stderr,
            "EGL_ALPHA_MASK_SIZE query returned %d, expected 8\n",
            actualSize);
    return 1;
  }

  if (configOut)
    *configOut = config;

  return 0;
}

int main(void)
{
  const EGLint width = 64;
  const EGLint height = 64;
  EGLDisplay display;
  EGLConfig config;
  EGLSurface surface;
  EGLContext context;
  EGLint major, minor;
  unsigned char *pixels;
  int result = 0;
  EGLint pbufferAttribs[] = {
    EGL_WIDTH, width,
    EGL_HEIGHT, height,
    EGL_NONE
  };
  VGfloat clearColor[] = { 0.2f, 0.4f, 0.7f, 1.0f };

  display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY)
    return fail_egl("eglGetDisplay failed");

  if (!eglInitialize(display, &major, &minor))
    return fail_egl("eglInitialize failed");

  if (!eglBindAPI(EGL_OPENVG_API)) {
    eglTerminate(display);
    return fail_egl("EGL OpenVG config selection failed");
  }

  if (expect_alpha_mask_config(display, 1, EGL_TRUE, &config) ||
      expect_alpha_mask_config(display, 8, EGL_TRUE, NULL) ||
      expect_alpha_mask_config(display, 9, EGL_FALSE, NULL)) {
    eglTerminate(display);
    return 1;
  }

  surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
  context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
  if (surface == EGL_NO_SURFACE ||
      context == EGL_NO_CONTEXT ||
      !eglMakeCurrent(display, surface, surface, context)) {
    if (context != EGL_NO_CONTEXT)
      eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE)
      eglDestroySurface(display, surface);
    eglTerminate(display);
    return fail_egl("EGL OpenVG pbuffer creation failed");
  }

  pixels = (unsigned char*)calloc((size_t)width * (size_t)height * 4u, 1);
  if (!pixels) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, width, height);
  draw_retained_path_glyph();
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);

  if (vgGetError() != VG_NO_ERROR) {
    result = fail_vg("OpenVG rendering failed");
  } else if (pixels[3] == 0) {
    fprintf(stderr, "OpenVG pbuffer readback produced a transparent pixel\n");
    result = 1;
  } else {
    size_t offset = ((size_t)32 * (size_t)width + 32u) * 4u;
    if (pixels[offset] < 128 || pixels[offset + 3] == 0) {
      fprintf(stderr, "OpenVG retained glyph drawing did not affect the expected pixel\n");
      result = 1;
    } else {
      result = run_image_draw_test(pixels, width, height);
      if (result == 0)
        result = run_gradient_ramp_test(pixels, width, height);
      if (result == 0)
        result = run_path_measurement_test();
      if (result == 0)
        result = run_fill_rule_test(pixels, width, height);
      if (result == 0)
        result = run_review_regression_test();
      if (result == 0)
        result = run_src_over_alpha_test(pixels, width, height);
      if (result == 0)
        result = run_core_blend_mode_test(pixels, width, height);
      if (result == 0)
        result = run_advanced_blend_mode_test(pixels, width, height);
      if (result == 0)
        result = run_advanced_blend_mask_test(pixels, width, height);
      if (result == 0)
        result = run_rendering_quality_antialias_test(pixels, width, height);
      if (result == 0)
        result = run_user_fragment_antialias_coverage_test(pixels,
                                                           width, height);
      if (result == 0)
        result = run_shared_context_test(display, config, surface,
                                         context, pixels, width, height);
      if (result == 0)
        result = run_client_buffer_pbuffer_test(display, config, surface,
                                                context, pixels, width, height);
      if (result == 0)
        result = run_pixel_transfer_test(pixels, width, height);
      if (result == 0)
        result = run_core_image_format_test(pixels, width, height);
      if (result == 0)
        result = run_child_image_test(pixels, width, height);
      if (result == 0)
        result = run_glyph_image_batch_test(pixels, width, height);
      if (result == 0)
        result = run_glyph_path_batch_test(pixels, width, height);
      if (result == 0)
        result = run_image_filter_test();
      if (result == 0)
        result = run_mask_test(pixels, width, height);
      if (result == 0)
        result = run_render_to_mask_test(pixels, width, height);
      if (result == 0)
        result = run_hardware_query_test();
      if (result == 0)
        result = run_warp_test();
      if (result == 0)
        result = run_alignment_validation_test();
      if (result == 0)
        printf("EGL/OpenVG pbuffer smoke test passed on EGL %d.%d\n", major, minor);
    }
  }

  free(pixels);

cleanup:
  eglDestroyContext(display, context);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(display, surface);
  eglTerminate(display);
  return result;
}
