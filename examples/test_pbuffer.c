/*
 * Minimal EGL/OpenVG pbuffer smoke test for ShaderVG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <vg/openvg.h>
#include <vg/vgu.h>

#define WARP_TEST_EPSILON 0.001f

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

  if (pixels[sample + 3] < 100) {
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

static int run_pixel_transfer_test(unsigned char *pixels,
                                   EGLint width,
                                   EGLint height)
{
  VGImage image = VG_INVALID_HANDLE;
  VGImage dstImage = VG_INVALID_HANDLE;
  VGubyte writeData[8 * 5 * 4];
  VGubyte imageData[5 * 5 * 4];
  VGubyte stridedImageData[7 * 4 * 4];
  VGubyte imageRead[6 * 6 * 4];
  VGfloat black[] = {0.0f, 0.0f, 0.0f, 1.0f};
  VGfloat blue[] = {0.0f, 0.0f, 1.0f, 1.0f};
  VGint scissor[] = {12, 9, 2, 2};
  int i;
  int result = 0;

  memset(writeData, 0, sizeof(writeData));
  memset(imageData, 0, sizeof(imageData));
  memset(stridedImageData, 0, sizeof(stridedImageData));
  memset(imageRead, 0, sizeof(imageRead));

  for (i=0; i<6 * 5; ++i) {
    VGint x = i % 6;
    VGint y = i / 6;
    set_rgba(writeData, 8 * 4, x, y, 255, 0, 0, 255);
  }
  set_rgba(writeData, 8 * 4, 2, 2, 0, 255, 0, 255);
  set_rgba(writeData, 8 * 4, 3, 2, 255, 255, 0, 255);
  set_rgba(writeData, 8 * 4, 0, 0, 0, 255, 0, 255);

  for (i=0; i<5 * 5; ++i)
    set_rgba(imageData, 5 * 4, i % 5, i / 5, 255, 0, 0, 255);
  for (i=0; i<4 * 4; ++i)
    set_rgba(stridedImageData, 7 * 4, i % 4, i / 4, 255, 0, 0, 255);
  set_rgba(stridedImageData, 7 * 4, 1, 1, 0, 255, 0, 255);

  image = vgCreateImage(VG_lABGR_8888, 5, 5, VG_IMAGE_QUALITY_BETTER);
  dstImage = vgCreateImage(VG_lABGR_8888, 5, 5, VG_IMAGE_QUALITY_BETTER);
  if (image == VG_INVALID_HANDLE || dstImage == VG_INVALID_HANDLE) {
    result = fail_vg("OpenVG pixel transfer test setup failed");
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
  vgSetPixels(20, 8, image, 2, 1, 2, 2);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG vgSetPixels failed") ||
      expect_rgba_at(pixels, width * 4, 20, 8, 0, 0, 255, 255,
                     "OpenVG vgSetPixels did not copy image pixels to the surface")) {
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
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  if (dstImage != VG_INVALID_HANDLE)
    vgDestroyImage(dstImage);
  if (image != VG_INVALID_HANDLE)
    vgDestroyImage(image);
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
  VGfloat red[] = {1.0f, 0.0f, 0.0f, 1.0f};
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

  vgSetfv(VG_CLEAR_COLOR, 4, red);
  vgClear(0, 0, imageWidth, imageHeight);
  vgFinish();
  vgReadPixels(pixels, imageWidth * 4,
               VG_sRGBA_8888, 0, 0, imageWidth, imageHeight);
  if (expect_no_vg_error("OpenVG image pbuffer rendering failed") ||
      expect_pixel(pixels, imageWidth, 8, 8, 192, 32, 32,
                   "OpenVG image pbuffer readback did not see rendered pixels")) {
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
      expect_pixel(imageData, imageWidth, 8, 8, 192, 32, 32,
                   "OpenVG image data did not reflect image pbuffer rendering")) {
    result = 1;
    goto cleanup;
  }

  vgSetfv(VG_CLEAR_COLOR, 4, black);
  vgClear(0, 0, width, height);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
  vgFinish();
  vgReadPixels(pixels, width * 4, VG_sRGBA_8888, 0, 0, width, height);
  if (expect_no_vg_error("OpenVG could not draw an image-backed pbuffer") ||
      expect_pixel(pixels, width, 8, 8, 192, 32, 32,
                   "OpenVG drawing did not sample the image-backed pbuffer")) {
    result = 1;
    goto cleanup;
  }

cleanup:
  if ((eglGetCurrentContext() != context ||
       eglGetCurrentSurface(EGL_DRAW) != baseSurface) &&
      !eglMakeCurrent(display, baseSurface, baseSurface, context) &&
      result == 0)
    result = fail_egl("EGL could not restore the base pbuffer during cleanup");
  if (image != VG_INVALID_HANDLE)
    vgDestroyImage(image);
  if (imageSurface != EGL_NO_SURFACE &&
      !eglDestroySurface(display, imageSurface) &&
      result == 0)
    result = fail_egl("EGL could not destroy an OpenVG image pbuffer");
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
        result = run_src_over_alpha_test(pixels, width, height);
      if (result == 0)
        result = run_shared_context_test(display, config, surface,
                                         context, pixels, width, height);
      if (result == 0)
        result = run_client_buffer_pbuffer_test(display, config, surface,
                                                context, pixels, width, height);
      if (result == 0)
        result = run_pixel_transfer_test(pixels, width, height);
      if (result == 0)
        result = run_mask_test(pixels, width, height);
      if (result == 0)
        result = run_render_to_mask_test(pixels, width, height);
      if (result == 0)
        result = run_hardware_query_test();
      if (result == 0)
        result = run_warp_test();
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
