#include "test.h"

#define CIRCLE_SIZE 230.0f
#define PANEL_WIDTH 560.0f
#define PANEL_HEIGHT 310.0f
#define PANEL_BOTTOM_OFFSET 180.0f

static VGPath circlePath = VG_INVALID_HANDLE;
static VGPath panelPath = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;

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

static void drawCircle(VGfloat x, VGfloat y,
                       VGfloat r, VGfloat g, VGfloat b)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  setPaintColor(fillPaint, r, g, b, 0.55f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(circlePath, VG_FILL_PATH);
}

static void drawCircleOutline(VGfloat x, VGfloat y)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  setPaintColor(strokePaint, 1.0f, 1.0f, 1.0f, 0.38f);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 2.0f);
  vgDrawPath(circlePath, VG_STROKE_PATH);
}

static void drawBackground(void)
{
  VGfloat clear[] = {0.07f, 0.08f, 0.10f, 1.0f};
  VGfloat panelColor[] = {0.13f, 0.15f, 0.18f, 1.0f};
  VGfloat panelStroke[] = {0.45f, 0.48f, 0.54f, 1.0f};
  VGfloat panelX = (VGfloat)testWidth() * 0.5f - PANEL_WIDTH * 0.5f;
  VGfloat panelY = (VGfloat)testHeight() * 0.5f - PANEL_BOTTOM_OFFSET;

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(panelX, panelY);

  vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, panelColor);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(panelPath, VG_FILL_PATH);

  vgSetParameterfv(strokePaint, VG_PAINT_COLOR, 4, panelStroke);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 2.0f);
  vgDrawPath(panelPath, VG_STROKE_PATH);
}

static void display(float interval)
{
  VGfloat centerX = (VGfloat)testWidth() * 0.5f;
  VGfloat centerY = (VGfloat)testHeight() * 0.5f;
  VGfloat redX = centerX - 82.0f;
  VGfloat redY = centerY + 42.0f;
  VGfloat greenX = centerX + 82.0f;
  VGfloat greenY = centerY + 42.0f;
  VGfloat blueX = centerX;
  VGfloat blueY = centerY - 90.0f;

  (void)interval;

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  drawBackground();

  drawCircle(redX, redY, 1.0f, 0.0f, 0.0f);
  drawCircle(greenX, greenY, 0.0f, 1.0f, 0.0f);
  drawCircle(blueX, blueY, 0.0f, 0.28f, 1.0f);

  drawCircleOutline(redX, redY);
  drawCircleOutline(greenX, greenY);
  drawCircleOutline(blueX, blueY);
}

static void cleanup(void)
{
  if (circlePath != VG_INVALID_HANDLE)
    vgDestroyPath(circlePath);
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

static void createContent(void)
{
  circlePath = testCreatePath();
  panelPath = testCreatePath();
  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();

  if (circlePath == VG_INVALID_HANDLE ||
      panelPath == VG_INVALID_HANDLE ||
      fillPaint == VG_INVALID_HANDLE ||
      strokePaint == VG_INVALID_HANDLE)
    exit(EXIT_FAILURE);

  if (vguEllipse(circlePath, 0.0f, 0.0f,
                 CIRCLE_SIZE, CIRCLE_SIZE) != VGU_NO_ERROR ||
      vguRoundRect(panelPath, 0.0f, 0.0f,
                   PANEL_WIDTH, PANEL_HEIGHT, 18.0f, 18.0f) != VGU_NO_ERROR)
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 720, 460, "ShaderVG: Alpha Blending Test");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testRun();

  return EXIT_SUCCESS;
}
