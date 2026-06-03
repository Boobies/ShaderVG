#include "test.h"

#include <VG/vgext.h>
#include <math.h>

#define IMAGE_W 96
#define IMAGE_H 96
#define PANEL_W 116
#define PANEL_H 116
#define PANEL_MARGIN 10
#define PANEL_GAP 22
#define PANEL_COLS 3
#define PANEL_ROWS 2

static VGImage sourceImage = VG_INVALID_HANDLE;
static VGImage dropShadowImage = VG_INVALID_HANDLE;
static VGImage bevelImage = VG_INVALID_HANDLE;
static VGImage gradientGlowImage = VG_INVALID_HANDLE;
static VGImage gradientBevelImage = VG_INVALID_HANDLE;
static VGImage directImage = VG_INVALID_HANDLE;
static VGPath panelPath = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;

static VGfloat firstPanelX(void)
{
  return (testWidth() -
          (PANEL_W * PANEL_COLS + PANEL_GAP * (PANEL_COLS - 1))) * 0.5f;
}

static VGfloat firstPanelY(void)
{
  return (testHeight() -
          (PANEL_H * PANEL_ROWS + PANEL_GAP * (PANEL_ROWS - 1))) * 0.5f;
}

static VGubyte clampByte(VGint value)
{
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return (VGubyte)value;
}

static void setPixel(VGubyte *data,
                     VGint x,
                     VGint y,
                     VGubyte r,
                     VGubyte g,
                     VGubyte b,
                     VGubyte a)
{
  size_t offset = ((size_t)y * IMAGE_W + (size_t)x) * 4u;

  data[offset + 0] = r;
  data[offset + 1] = g;
  data[offset + 2] = b;
  data[offset + 3] = a;
}

static void createSourcePixels(VGubyte *data)
{
  VGint x;
  VGint y;

  for (y=0; y<IMAGE_H; ++y) {
    for (x=0; x<IMAGE_W; ++x) {
      VGint dx = x - IMAGE_W / 2;
      VGint dy = y - IMAGE_H / 2;
      VGfloat distance = sqrtf((VGfloat)(dx * dx + dy * dy));
      VGfloat edge = 31.0f - distance;
      VGint alpha = (VGint)(edge * 96.0f);
      VGint r = 236 - y;
      VGint g = 92 + x;
      VGint b = 160 + (x + y) / 4;

      if (dx > -10 && dx < 18 && dy > -24 && dy < 24)
        alpha = 255;
      if (dy > 7 && dy < 20 && dx > -28 && dx < 28)
        alpha = 255;

      setPixel(data, x, y,
               clampByte(r), clampByte(g), clampByte(b),
               clampByte(alpha));
    }
  }
}

static VGImage createImage(void)
{
  return vgCreateImage(VG_lABGR_8888, IMAGE_W, IMAGE_H,
                       VG_IMAGE_QUALITY_BETTER);
}

static void clearImage(VGImage image)
{
  VGfloat clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};

  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClearImage(image, 0, 0, IMAGE_W, IMAGE_H);
}

static VGPaint createColorPaint(VGuint rgba)
{
  VGPaint paint = vgCreatePaint();

  if (paint != VG_INVALID_HANDLE)
    vgSetColor(paint, rgba);
  return paint;
}

static VGPaint createGradientPaint(const VGfloat *stops, VGint count)
{
  VGPaint paint = vgCreatePaint();

  if (paint == VG_INVALID_HANDLE)
    return paint;

  vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_LINEAR_GRADIENT);
  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_SPREAD_MODE,
                  VG_COLOR_RAMP_SPREAD_PAD);
  vgSetParameteri(paint, VG_PAINT_COLOR_RAMP_PREMULTIPLIED, VG_FALSE);
  vgSetParameterfv(paint, VG_PAINT_COLOR_RAMP_STOPS, count * 5, stops);
  return paint;
}

static void createDirectParametricImage(void)
{
  VGImage blur = VG_INVALID_HANDLE;
  VGPaint highlight = VG_INVALID_HANDLE;
  VGPaint shadow = VG_INVALID_HANDLE;

  blur = vgCreateImage(VG_A_8, IMAGE_W, IMAGE_H, VG_IMAGE_QUALITY_BETTER);
  highlight = createColorPaint(0xfff1a0d8u);
  shadow = createColorPaint(0x174ea6d8u);
  if (blur == VG_INVALID_HANDLE ||
      highlight == VG_INVALID_HANDLE ||
      shadow == VG_INVALID_HANDLE)
    goto cleanup;

  vgIterativeAverageBlurKHR(blur, sourceImage, 7.0f, 7.0f, 2, VG_TILE_PAD);
  vgParametricFilterKHR(directImage, sourceImage, blur,
                        1.0f, 2.0f, 2.0f,
                        VG_PF_INNER_FLAG_KHR |
                        VG_PF_OUTER_FLAG_KHR |
                        VG_PF_OBJECT_VISIBLE_FLAG_KHR,
                        highlight, shadow);

cleanup:
  if (shadow != VG_INVALID_HANDLE)
    vgDestroyPaint(shadow);
  if (highlight != VG_INVALID_HANDLE)
    vgDestroyPaint(highlight);
  if (blur != VG_INVALID_HANDLE)
    vgDestroyImage(blur);
}

static void createImages(void)
{
  VGubyte *pixels;
  VGfloat glowStops[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.45f, 0.0f, 0.75f, 1.0f, 0.55f,
    1.0f, 0.2f, 1.0f, 0.62f, 0.95f
  };
  VGfloat bevelStops[] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.35f, 0.05f, 0.07f, 0.20f, 0.80f,
    0.65f, 1.0f, 0.86f, 0.34f, 0.92f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f
  };

  pixels = (VGubyte*)malloc(IMAGE_W * IMAGE_H * 4);
  if (!pixels)
    return;

  sourceImage = createImage();
  dropShadowImage = createImage();
  bevelImage = createImage();
  gradientGlowImage = createImage();
  gradientBevelImage = createImage();
  directImage = createImage();

  if (sourceImage == VG_INVALID_HANDLE ||
      dropShadowImage == VG_INVALID_HANDLE ||
      bevelImage == VG_INVALID_HANDLE ||
      gradientGlowImage == VG_INVALID_HANDLE ||
      gradientBevelImage == VG_INVALID_HANDLE ||
      directImage == VG_INVALID_HANDLE) {
    free(pixels);
    return;
  }

  createSourcePixels(pixels);
  vgImageSubData(sourceImage, pixels, IMAGE_W * 4,
                 VG_lABGR_8888, 0, 0, IMAGE_W, IMAGE_H);
  free(pixels);

  vgSeti(VG_FILTER_FORMAT_LINEAR, VG_FALSE);
  vgSeti(VG_FILTER_FORMAT_PREMULTIPLIED, VG_FALSE);
  vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);

  clearImage(dropShadowImage);
  clearImage(bevelImage);
  clearImage(gradientGlowImage);
  clearImage(gradientBevelImage);
  clearImage(directImage);

  vguDropShadowKHR(dropShadowImage, sourceImage,
                   9.0f, 7.0f, 2, 1.15f,
                   7.0f, 315.0f,
                   VG_PF_OUTER_FLAG_KHR |
                   VG_PF_OBJECT_VISIBLE_FLAG_KHR,
                   VG_IMAGE_QUALITY_BETTER,
                   0x111827c8u);
  vguBevelKHR(bevelImage, sourceImage,
              5.0f, 5.0f, 2, 1.0f,
              3.0f, 135.0f,
              VG_PF_INNER_FLAG_KHR |
              VG_PF_OBJECT_VISIBLE_FLAG_KHR,
              VG_IMAGE_QUALITY_BETTER,
              0xffffffe6u,
              0x1f2937ddu);
  vguGradientGlowKHR(gradientGlowImage, sourceImage,
                     9.0f, 9.0f, 2, 1.0f,
                     4.0f, 45.0f,
                     VG_PF_OUTER_FLAG_KHR |
                     VG_PF_OBJECT_VISIBLE_FLAG_KHR,
                     VG_IMAGE_QUALITY_BETTER,
                     3, glowStops);
  vguGradientBevelKHR(gradientBevelImage, sourceImage,
                      7.0f, 7.0f, 2, 1.0f,
                      3.0f, 135.0f,
                      VG_PF_INNER_FLAG_KHR |
                      VG_PF_OBJECT_VISIBLE_FLAG_KHR,
                      VG_IMAGE_QUALITY_BETTER,
                      4, bevelStops);
  createDirectParametricImage();
}

static void createContent(void)
{
  panelPath = testCreatePath();
  vguRoundRect(panelPath, 0.0f, 0.0f,
               (VGfloat)PANEL_W, (VGfloat)PANEL_H,
               6.0f, 6.0f);

  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();
  createImages();
}

static void setPaintColor(VGPaint paint,
                          VGfloat r,
                          VGfloat g,
                          VGfloat b,
                          VGfloat a)
{
  VGfloat color[4];
  color[0] = r;
  color[1] = g;
  color[2] = b;
  color[3] = a;
  vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
}

static void drawPanel(VGfloat x, VGfloat y, VGImage image)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  setPaintColor(fillPaint, 0.06f, 0.07f, 0.09f, 1.0f);
  setPaintColor(strokePaint, 0.40f, 0.44f, 0.48f, 1.0f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 1.4f);
  vgDrawPath(panelPath, VG_FILL_PATH | VG_STROKE_PATH);

  if (image == VG_INVALID_HANDLE)
    return;

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x + PANEL_MARGIN, y + PANEL_MARGIN);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
}

static void display(float interval)
{
  VGfloat clearColor[] = {0.14f, 0.16f, 0.18f, 1.0f};
  VGfloat x = firstPanelX();
  VGfloat y = firstPanelY();

  (void)interval;

  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, testWidth(), testHeight());

  drawPanel(x, y + PANEL_H + PANEL_GAP, sourceImage);
  drawPanel(x + PANEL_W + PANEL_GAP, y + PANEL_H + PANEL_GAP,
            dropShadowImage);
  drawPanel(x + (PANEL_W + PANEL_GAP) * 2, y + PANEL_H + PANEL_GAP,
            bevelImage);
  drawPanel(x, y, gradientGlowImage);
  drawPanel(x + PANEL_W + PANEL_GAP, y, gradientBevelImage);
  drawPanel(x + (PANEL_W + PANEL_GAP) * 2, y, directImage);
}

static void cleanup(void)
{
  if (directImage != VG_INVALID_HANDLE)
    vgDestroyImage(directImage);
  if (gradientBevelImage != VG_INVALID_HANDLE)
    vgDestroyImage(gradientBevelImage);
  if (gradientGlowImage != VG_INVALID_HANDLE)
    vgDestroyImage(gradientGlowImage);
  if (bevelImage != VG_INVALID_HANDLE)
    vgDestroyImage(bevelImage);
  if (dropShadowImage != VG_INVALID_HANDLE)
    vgDestroyImage(dropShadowImage);
  if (sourceImage != VG_INVALID_HANDLE)
    vgDestroyImage(sourceImage);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
}

int main(int argc, char **argv)
{
  testInit(argc, argv,
           PANEL_W * PANEL_COLS + PANEL_GAP * (PANEL_COLS - 1) + 64,
           PANEL_H * PANEL_ROWS + PANEL_GAP * (PANEL_ROWS - 1) + 64,
           "ShaderVG: Parametric Filter");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testRun();
  return EXIT_SUCCESS;
}
