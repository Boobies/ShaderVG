#include "test.h"

#include <png.h>

#ifndef IMAGE_DIR
#  define IMAGE_DIR "./"
#endif

#define BLEND_COUNT 5

typedef struct
{
  VGBlendMode mode;
  VGfloat color[4];
} BlendPanel;

static VGImage srcImage = VG_INVALID_HANDLE;
static VGImage dstImage = VG_INVALID_HANDLE;
static VGint srcWidth = 0;
static VGint srcHeight = 0;
static VGint dstWidth = 0;
static VGint dstHeight = 0;
static VGPath panelPath = VG_INVALID_HANDLE;
static VGPaint panelPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;

static BlendPanel blendPanels[BLEND_COUNT] = {
  { VG_BLEND_SRC,      { 0.40f, 0.48f, 0.58f, 1.0f } },
  { VG_BLEND_SRC_OVER, { 0.34f, 0.44f, 0.62f, 1.0f } },
  { VG_BLEND_DST_OVER, { 0.35f, 0.54f, 0.48f, 1.0f } },
  { VG_BLEND_SRC_IN,   { 0.56f, 0.45f, 0.62f, 1.0f } },
  { VG_BLEND_DST_IN,   { 0.58f, 0.50f, 0.34f, 1.0f } }
};

static void setPaintColor(VGPaint paint,
                          VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  VGfloat color[4];
  color[0] = r;
  color[1] = g;
  color[2] = b;
  color[3] = a;
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
}

static VGImageFormat nativeRGBAFormat(void)
{
  unsigned int littleEndianTest = 1;

  if (((unsigned char*)&littleEndianTest)[0] == 1)
    return VG_sABGR_8888;

  return VG_sRGBA_8888;
}

static VGImage createImageFromPng(const char *filename,
                                  VGint *outWidth,
                                  VGint *outHeight)
{
  FILE *file = NULL;
  png_structp png = NULL;
  png_infop info = NULL;
  png_bytep *rows = NULL;
  png_bytep data = NULL;
  png_uint_32 width = 0;
  png_uint_32 height = 0;
  png_size_t stride = 0;
  int bitDepth = 0;
  int colorType = 0;
  int y;
  VGImage image = VG_INVALID_HANDLE;
  VGImageFormat format = nativeRGBAFormat();

  file = fopen(filename, "rb");
  if (!file) {
    fprintf(stderr, "Failed opening '%s' for reading\n", filename);
    return VG_INVALID_HANDLE;
  }

  png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png)
    goto cleanup;

  info = png_create_info_struct(png);
  if (!info)
    goto cleanup;

  if (setjmp(png_jmpbuf(png)))
    goto cleanup;

  png_init_io(png, file);
  png_read_info(png, info);

  width = png_get_image_width(png, info);
  height = png_get_image_height(png, info);
  bitDepth = png_get_bit_depth(png, info);
  colorType = png_get_color_type(png, info);

  if (bitDepth == 16)
    png_set_strip_16(png);

  if (colorType == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);

  if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
    png_set_expand_gray_1_2_4_to_8(png);

  if (png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);

  if (colorType == PNG_COLOR_TYPE_GRAY ||
      colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);

  if (colorType == PNG_COLOR_TYPE_RGB ||
      colorType == PNG_COLOR_TYPE_GRAY ||
      colorType == PNG_COLOR_TYPE_PALETTE)
    png_set_filler(png, 0xff, PNG_FILLER_AFTER);

  png_read_update_info(png, info);
  stride = png_get_rowbytes(png, info);

  if (width == 0 || height == 0 ||
      width > (png_uint_32)VG_MAXINT ||
      height > (png_uint_32)VG_MAXINT ||
      stride > (png_size_t)VG_MAXINT)
    goto cleanup;

  data = (png_bytep)malloc(stride * height);
  rows = (png_bytep*)malloc(sizeof(png_bytep) * height);
  if (!data || !rows)
    goto cleanup;

  for (y=0; y<(int)height; ++y)
    rows[y] = data + (height - 1 - y) * stride;

  png_read_image(png, rows);
  png_read_end(png, NULL);

  image = vgCreateImage(format, (VGint)width, (VGint)height,
                        VG_IMAGE_QUALITY_BETTER);
  if (image != VG_INVALID_HANDLE) {
    vgImageSubData(image, data, (VGint)stride, format,
                   0, 0, (VGint)width, (VGint)height);
    *outWidth = (VGint)width;
    *outHeight = (VGint)height;
  }

cleanup:
  if (image == VG_INVALID_HANDLE)
    fprintf(stderr, "Failed loading '%s' as a PNG image\n", filename);

  if (rows)
    free(rows);
  if (data)
    free(data);
  if (png || info)
    png_destroy_read_struct(&png, &info, NULL);
  if (file)
    fclose(file);

  return image;
}

static VGImage createImageAsset(const char *name,
                                VGint *outWidth,
                                VGint *outHeight)
{
  char filename[1024];
  FILE *file;

  snprintf(filename, sizeof(filename), "%s%s", IMAGE_DIR, name);
  file = fopen(filename, "rb");
  if (!file) {
    snprintf(filename, sizeof(filename), "examples/%s", name);
    file = fopen(filename, "rb");
  }

  if (file)
    fclose(file);

  return createImageFromPng(filename, outWidth, outHeight);
}

static void drawImage(VGImage image, VGfloat x, VGfloat y, VGfloat scale)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgScale(scale, scale);
  vgDrawImage(image);
}

static void drawPanelBackground(VGfloat x, VGfloat y,
                                VGfloat width, VGfloat height,
                                BlendPanel *panel)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  vgSeti(VG_BLEND_MODE, VG_BLEND_DST_OVER);
  vgSetPaint(panelPaint, VG_FILL_PATH);
  vgSetParameterfv(panelPaint, VG_PAINT_COLOR, 4, panel->color);
  vgDrawPath(panelPath, VG_FILL_PATH);

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  setPaintColor(strokePaint, 0.90f, 0.92f, 0.94f, 1.0f);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 2.0f);
  vgDrawPath(panelPath, VG_STROKE_PATH);

  (void)width;
  (void)height;
}

static void display(float interval)
{
  VGfloat clear[] = {0.0f, 0.0f, 0.0f, 0.0f};
  VGfloat scale = 1.55f;
  VGfloat panelWidth = 116.0f;
  VGfloat panelHeight = 138.0f;
  VGfloat gap = 18.0f;
  VGfloat x0 = (testWidth() - (panelWidth * BLEND_COUNT +
                               gap * (BLEND_COUNT - 1))) * 0.5f;
  VGfloat y = (testHeight() - panelHeight) * 0.5f;
  VGfloat imageW = srcWidth > dstWidth ? srcWidth : dstWidth;
  VGfloat imageH = srcHeight > dstHeight ? srcHeight : dstHeight;
  VGfloat imageXOffset = (panelWidth - imageW * scale) * 0.5f;
  VGfloat imageYOffset = (panelHeight - imageH * scale) * 0.5f;
  int i;

  (void)interval;

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);

  for (i=0; i<BLEND_COUNT; ++i) {
    VGfloat x = x0 + i * (panelWidth + gap);

    vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
    drawImage(dstImage, x + imageXOffset, y + imageYOffset, scale);

    vgSeti(VG_BLEND_MODE, blendPanels[i].mode);
    drawImage(srcImage, x + imageXOffset, y + imageYOffset, scale);
  }

  for (i=0; i<BLEND_COUNT; ++i) {
    VGfloat x = x0 + i * (panelWidth + gap);
    drawPanelBackground(x, y, panelWidth, panelHeight, &blendPanels[i]);
  }
}

static void cleanup(void)
{
  if (srcImage != VG_INVALID_HANDLE)
    vgDestroyImage(srcImage);
  if (dstImage != VG_INVALID_HANDLE)
    vgDestroyImage(dstImage);
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
  if (panelPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(panelPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

static void createOperands(void)
{
  srcImage = createImageAsset("test_blend_src.png", &srcWidth, &srcHeight);
  dstImage = createImageAsset("test_blend_dst.png", &dstWidth, &dstHeight);

  if (srcImage == VG_INVALID_HANDLE || dstImage == VG_INVALID_HANDLE)
    exit(EXIT_FAILURE);

  panelPath = testCreatePath();
  vguRoundRect(panelPath, 0.0f, 0.0f, 116.0f, 138.0f, 12.0f, 12.0f);

  panelPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 720, 220, "ShaderVG: Blending Test");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createOperands();
  testRun();
  
  return EXIT_SUCCESS;
}
