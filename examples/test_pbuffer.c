/*
 * Minimal EGL/OpenVG pbuffer smoke test for ShaderVG.
 */

#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <vg/openvg.h>

static int fail_egl(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
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

int main(void)
{
  const EGLint width = 64;
  const EGLint height = 64;
  EGLDisplay display;
  EGLConfig config;
  EGLSurface surface;
  EGLContext context;
  EGLint major, minor, count;
  unsigned char *pixels;
  int result = 0;
  EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_STENCIL_SIZE, 8,
    EGL_NONE
  };
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

  if (!eglBindAPI(EGL_OPENVG_API) ||
      !eglChooseConfig(display, configAttribs, &config, 1, &count) ||
      count < 1) {
    eglTerminate(display);
    return fail_egl("EGL OpenVG config selection failed");
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
      result = run_mask_test(pixels, width, height);
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
