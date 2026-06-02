#include "test.h"

#define PANEL_COUNT 3
#define PANEL_MARGIN 28.0f
#define PANEL_GAP 18.0f

static VGPath panelPath = VG_INVALID_HANDLE;
static VGPath blobPath = VG_INVALID_HANDLE;
static VGPath linePath = VG_INVALID_HANDLE;
static VGPath circlePath = VG_INVALID_HANDLE;
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

static void drawPathFill(VGPath path,
                         VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  setPaintColor(fillPaint, r, g, b, a);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(path, VG_FILL_PATH);
}

static void drawPathStroke(VGPath path,
                           VGfloat r, VGfloat g, VGfloat b, VGfloat a,
                           VGfloat width)
{
  setPaintColor(strokePaint, r, g, b, a);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, width);
  vgDrawPath(path, VG_STROKE_PATH);
}

static void drawPanel(VGint index, VGRenderingQuality quality)
{
  VGfloat width = (VGfloat)testWidth();
  VGfloat height = (VGfloat)testHeight();
  VGfloat panelWidth =
    (width - PANEL_MARGIN * 2.0f - PANEL_GAP * (PANEL_COUNT - 1)) /
    PANEL_COUNT;
  VGfloat panelHeight = height - PANEL_MARGIN * 2.0f - 36.0f;
  VGfloat panelX = PANEL_MARGIN + (panelWidth + PANEL_GAP) * index;
  VGfloat panelY = PANEL_MARGIN;
  VGfloat centerX = panelX + panelWidth * 0.5f;
  VGfloat centerY = panelY + panelHeight * 0.56f;

  vgSeti(VG_RENDERING_QUALITY, quality);

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(panelX, panelY);
  vgScale(panelWidth / 220.0f, panelHeight / 250.0f);
  drawPathFill(panelPath, 0.11f, 0.12f, 0.15f, 1.0f);
  drawPathStroke(panelPath, 0.34f, 0.37f, 0.43f, 1.0f, 1.6f);

  vgLoadIdentity();
  vgTranslate(centerX + 0.3f, centerY + 0.2f);
  vgRotate(-8.0f);
  drawPathFill(blobPath, 0.04f, 0.72f, 0.82f, 0.78f);
  drawPathStroke(blobPath, 1.0f, 1.0f, 1.0f, 0.88f, 2.4f);

  vgLoadIdentity();
  vgTranslate(centerX - 6.4f, centerY - 6.7f);
  vgRotate(24.0f);
  drawPathStroke(linePath, 1.0f, 0.82f, 0.12f, 0.95f, 3.0f);

  vgLoadIdentity();
  vgTranslate(centerX + 48.35f, centerY - 56.2f);
  drawPathFill(circlePath, 0.96f, 0.18f, 0.34f, 0.80f);
  drawPathStroke(circlePath, 1.0f, 1.0f, 1.0f, 0.72f, 1.8f);
}

static void display(float interval)
{
  VGfloat clear[] = {0.05f, 0.06f, 0.08f, 1.0f};

  (void)interval;

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());

  drawPanel(0, VG_RENDERING_QUALITY_NONANTIALIASED);
  drawPanel(1, VG_RENDERING_QUALITY_FASTER);
  drawPanel(2, VG_RENDERING_QUALITY_BETTER);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
}

static void cleanup(void)
{
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
  if (blobPath != VG_INVALID_HANDLE)
    vgDestroyPath(blobPath);
  if (linePath != VG_INVALID_HANDLE)
    vgDestroyPath(linePath);
  if (circlePath != VG_INVALID_HANDLE)
    vgDestroyPath(circlePath);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

static void createBlobPath(void)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat coords[] = {
    -74.0f, -20.0f,
    -58.0f, 62.0f, -12.0f, 78.0f, 34.0f, 54.0f,
    90.0f, 25.0f, 74.0f, -44.0f, 28.0f, -66.0f,
    -22.0f, -90.0f, -82.0f, -62.0f, -74.0f, -20.0f,
    -74.0f, -20.0f, -74.0f, -20.0f, -74.0f, -20.0f
  };

  blobPath = testCreatePath();
  if (blobPath != VG_INVALID_HANDLE)
    vgAppendPathData(blobPath, 6, segments, coords);
}

static void createLinePath(void)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS
  };
  VGfloat coords[] = {
    -88.0f, -42.0f,
    88.0f, 42.0f
  };

  linePath = testCreatePath();
  if (linePath != VG_INVALID_HANDLE)
    vgAppendPathData(linePath, 2, segments, coords);
}

static void createContent(void)
{
  panelPath = testCreatePath();
  circlePath = testCreatePath();
  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();

  createBlobPath();
  createLinePath();

  if (panelPath == VG_INVALID_HANDLE ||
      blobPath == VG_INVALID_HANDLE ||
      linePath == VG_INVALID_HANDLE ||
      circlePath == VG_INVALID_HANDLE ||
      fillPaint == VG_INVALID_HANDLE ||
      strokePaint == VG_INVALID_HANDLE)
    exit(EXIT_FAILURE);

  if (vguRect(panelPath, 0.0f, 0.0f, 220.0f, 250.0f) != VGU_NO_ERROR ||
      vguEllipse(circlePath, 0.0f, 0.0f, 54.0f, 54.0f) != VGU_NO_ERROR)
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
  testInitSingleSample(argc, argv, 900, 420, "ShaderVG: Antialiasing Test");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testOverlayString("left: non-antialiased    center: faster    right: better");
  testOverlayColor(1.0f, 1.0f, 1.0f, 0.86f);
  testRun();

  return EXIT_SUCCESS;
}
