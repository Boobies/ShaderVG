#include "test.h"
#include <string.h>

#define IMAGE_SIZE 64
#define READBACK_SIZE 32
#define WRITE_STRIDE 80
#define WRITE_HEIGHT 80

static VGImage baseImage = VG_INVALID_HANDLE;
static VGImage editedImage = VG_INVALID_HANDLE;
static VGImage readbackImage = VG_INVALID_HANDLE;
static VGPath unitRect = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;
static VGubyte writePixels[WRITE_STRIDE * WRITE_HEIGHT * 4];

static void setRGBA(VGubyte *data, VGint stride,
                    VGint x, VGint y,
                    VGubyte r, VGubyte g, VGubyte b, VGubyte a)
{
  VGint offset = y * stride + x * 4;

  data[offset + 0] = r;
  data[offset + 1] = g;
  data[offset + 2] = b;
  data[offset + 3] = a;
}

static void setFill(VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  VGfloat color[] = {r, g, b, a};

  vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, color);
  vgSetPaint(fillPaint, VG_FILL_PATH);
}

static void setStroke(VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  VGfloat color[] = {r, g, b, a};

  vgSetParameterfv(strokePaint, VG_PAINT_COLOR, 4, color);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
}

static void drawRect(VGfloat x, VGfloat y, VGfloat width, VGfloat height)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgScale(width, height);
  vgDrawPath(unitRect, VG_FILL_PATH);
}

static void drawFrame(VGfloat x, VGfloat y, VGfloat width, VGfloat height)
{
  setFill(0.07f, 0.08f, 0.09f, 1.0f);
  drawRect(x, y, width, height);

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x + 0.5f, y + 0.5f);
  vgScale(width - 1.0f, height - 1.0f);
  setStroke(0.78f, 0.82f, 0.86f, 1.0f);
  vgSetf(VG_STROKE_LINE_WIDTH, 1.0f);
  vgDrawPath(unitRect, VG_STROKE_PATH);
}

static void drawImage(VGImage image,
                      VGfloat x, VGfloat y,
                      VGfloat width, VGfloat height,
                      VGfloat imageWidth, VGfloat imageHeight)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgScale(width / imageWidth, height / imageHeight);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(image);
}

static void fillBasePixels(VGubyte *data)
{
  VGint x;
  VGint y;

  for (y=0; y<IMAGE_SIZE; ++y) {
    for (x=0; x<IMAGE_SIZE; ++x) {
      VGubyte r = (VGubyte)(40 + x * 3);
      VGubyte g = (VGubyte)(48 + y * 3);
      VGubyte b = ((x / 8 + y / 8) & 1) ? 215 : 120;
      setRGBA(data, IMAGE_SIZE * 4, x, y, r, g, b, 255);
    }
  }
}

static void fillWritePixels(void)
{
  VGint x;
  VGint y;

  memset(writePixels, 0, sizeof(writePixels));
  for (y=0; y<WRITE_HEIGHT; ++y) {
    for (x=0; x<WRITE_STRIDE; ++x) {
      VGubyte r = ((x / 10) & 1) ? 235 : 32;
      VGubyte g = ((y / 10) & 1) ? 210 : 72;
      VGubyte b = (VGubyte)(80 + ((x + y) % 120));
      setRGBA(writePixels, WRITE_STRIDE * 4, x, y, r, g, b, 255);
    }
  }
}

static VGPath createUnitRect(void)
{
  VGPath path = testCreatePath();

  testMoveTo(path, 0.0f, 0.0f, VG_ABSOLUTE);
  testLineTo(path, 1.0f, 0.0f, VG_ABSOLUTE);
  testLineTo(path, 1.0f, 1.0f, VG_ABSOLUTE);
  testLineTo(path, 0.0f, 1.0f, VG_ABSOLUTE);
  testClosePath(path);

  return path;
}

static void createContent(void)
{
  VGubyte data[IMAGE_SIZE * IMAGE_SIZE * 4];
  VGfloat clearBlue[] = {0.05f, 0.18f, 0.78f, 1.0f};
  VGubyte empty[READBACK_SIZE * READBACK_SIZE * 4];

  fillBasePixels(data);
  fillWritePixels();
  memset(empty, 0, sizeof(empty));

  unitRect = createUnitRect();
  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();
  baseImage = vgCreateImage(VG_lABGR_8888,
                            IMAGE_SIZE, IMAGE_SIZE,
                            VG_IMAGE_QUALITY_BETTER);
  editedImage = vgCreateImage(VG_lABGR_8888,
                              IMAGE_SIZE, IMAGE_SIZE,
                              VG_IMAGE_QUALITY_BETTER);
  readbackImage = vgCreateImage(VG_lABGR_8888,
                                READBACK_SIZE, READBACK_SIZE,
                                VG_IMAGE_QUALITY_BETTER);

  vgImageSubData(baseImage, data, IMAGE_SIZE * 4, VG_lABGR_8888,
                 0, 0, IMAGE_SIZE, IMAGE_SIZE);
  vgImageSubData(editedImage, data, IMAGE_SIZE * 4, VG_lABGR_8888,
                 0, 0, IMAGE_SIZE, IMAGE_SIZE);
  vgImageSubData(readbackImage, empty, READBACK_SIZE * 4, VG_lABGR_8888,
                 0, 0, READBACK_SIZE, READBACK_SIZE);

  vgSetfv(VG_CLEAR_COLOR, 4, clearBlue);
  vgClearImage(editedImage, 16, 16, 32, 32);
  vgCopyImage(editedImage, 8, 42, baseImage, 40, 8, 16, 16, VG_FALSE);
}

static void display(float interval)
{
  VGfloat clear[] = {0.13f, 0.14f, 0.15f, 1.0f};
  VGubyte sample[4] = {0, 0, 0, 255};
  VGfloat panel = 128.0f;
  VGfloat gap = 22.0f;
  VGfloat y = (testHeight() - panel) * 0.5f;
  VGfloat x0 = (testWidth() - (panel * 5.0f + gap * 4.0f)) * 0.5f;
  VGfloat x1 = x0 + panel + gap;
  VGfloat x2 = x1 + panel + gap;
  VGfloat x3 = x2 + panel + gap;
  VGfloat x4 = x3 + panel + gap;

  (void)interval;

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());

  drawFrame(x0, y, panel, panel);
  drawImage(baseImage, x0 + 16.0f, y + 16.0f, 96.0f, 96.0f,
            IMAGE_SIZE, IMAGE_SIZE);

  drawFrame(x1, y, panel, panel);
  drawImage(editedImage, x1 + 16.0f, y + 16.0f, 96.0f, 96.0f,
            IMAGE_SIZE, IMAGE_SIZE);

  drawFrame(x2, y, panel, panel);
  vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK, 0, 0, testWidth(), testHeight());
  vgSeti(VG_MASKING, VG_TRUE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_DST_IN);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(120.0f, 80.0f);
  vgWritePixels(writePixels, WRITE_STRIDE * 4, VG_lABGR_8888,
                (VGint)x2 + 16, (VGint)y + 16, 80, 80);
  vgSetPixels((VGint)x2 + 46, (VGint)y + 46,
              baseImage, 20, 20, 28, 28);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);

  vgGetPixels(readbackImage, 0, 0,
              (VGint)x2 + 32, (VGint)y + 32,
              READBACK_SIZE, READBACK_SIZE);
  vgReadPixels(sample, 4, VG_sRGBA_8888,
               (VGint)x2 + 48, (VGint)y + 48, 1, 1);

  drawFrame(x3, y, panel, panel);
  drawImage(readbackImage, x3 + 16.0f, y + 48.0f, 64.0f, 64.0f,
            READBACK_SIZE, READBACK_SIZE);
  setFill((VGfloat)sample[0] / 255.0f,
          (VGfloat)sample[1] / 255.0f,
          (VGfloat)sample[2] / 255.0f,
          1.0f);
  drawRect(x3 + 88.0f, y + 48.0f, 24.0f, 64.0f);

  drawFrame(x4, y, panel, panel);
  vgWritePixels(writePixels, WRITE_STRIDE * 4, VG_lABGR_8888,
                (VGint)x4 + 18, (VGint)y + 18, 56, 56);
  vgCopyPixels((VGint)x4 + 46, (VGint)y + 46,
               (VGint)x4 + 18, (VGint)y + 18,
               56, 56);
}

static void cleanup(void)
{
  if (readbackImage != VG_INVALID_HANDLE)
    vgDestroyImage(readbackImage);
  if (editedImage != VG_INVALID_HANDLE)
    vgDestroyImage(editedImage);
  if (baseImage != VG_INVALID_HANDLE)
    vgDestroyImage(baseImage);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (unitRect != VG_INVALID_HANDLE)
    vgDestroyPath(unitRect);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 860, 300, "ShaderVG: Pixel Transfer Test");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testRun();

  return EXIT_SUCCESS;
}
