#include "test.h"

#define PANEL_W 170
#define PANEL_H 220
#define PANEL_Y 70
#define PANEL_GAP 32

static VGPath panelPath = VG_INVALID_HANDLE;
static VGPath circlePath = VG_INVALID_HANDLE;
static VGPath diamondPath = VG_INVALID_HANDLE;
static VGPath slashPath = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;
static VGImage imageMask = VG_INVALID_HANDLE;
static VGMaskLayer barMask = VG_INVALID_HANDLE;
static VGMaskLayer savedMask = VG_INVALID_HANDLE;
static VGfloat angle = 0.0f;

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

static VGPath createPolygon(const VGfloat *points, VGint count)
{
  VGint i;
  VGPath path = testCreatePath();

  if (path == VG_INVALID_HANDLE || count <= 0)
    return path;

  testMoveTo(path, points[0], points[1], VG_ABSOLUTE);
  for (i=1; i<count; ++i)
    testLineTo(path, points[i * 2], points[i * 2 + 1], VG_ABSOLUTE);
  testClosePath(path);

  return path;
}

static void clearSurfaceMask(void)
{
  vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK,
         0, 0, testWidth(), testHeight());
}

static void fillSurfaceMask(void)
{
  vgMask(VG_INVALID_HANDLE, VG_FILL_MASK,
         0, 0, testWidth(), testHeight());
}

static void drawPanelFrame(VGfloat x, VGfloat y)
{
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  setPaintColor(fillPaint, 0.08f, 0.09f, 0.11f, 1.0f);
  setPaintColor(strokePaint, 0.58f, 0.62f, 0.66f, 1.0f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 2.0f);
  vgDrawPath(panelPath, VG_FILL_PATH | VG_STROKE_PATH);
}

static void drawPanelStroke(VGfloat x, VGfloat y)
{
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  setPaintColor(strokePaint, 0.78f, 0.82f, 0.86f, 1.0f);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 2.0f);
  vgDrawPath(panelPath, VG_STROKE_PATH);
}

static void drawArtwork(VGfloat x, VGfloat y,
                        VGfloat r, VGfloat g, VGfloat b)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);

  vgLoadIdentity();
  vgTranslate(x, y);
  setPaintColor(fillPaint, 0.95f, 0.96f, 0.91f, 1.0f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(panelPath, VG_FILL_PATH);

  vgLoadIdentity();
  vgTranslate(x + PANEL_W * 0.5f, y + PANEL_H * 0.53f);
  vgRotate(angle * 0.35f);
  setPaintColor(fillPaint, r, g, b, 0.92f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(circlePath, VG_FILL_PATH);

  vgLoadIdentity();
  vgTranslate(x + PANEL_W * 0.5f, y + PANEL_H * 0.50f);
  vgRotate(-18.0f);
  setPaintColor(fillPaint, 0.03f, 0.12f, 0.18f, 0.72f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(slashPath, VG_FILL_PATH);

  vgLoadIdentity();
  vgTranslate(x + PANEL_W * 0.5f, y + PANEL_H * 0.50f);
  vgRotate(-angle * 0.22f);
  setPaintColor(fillPaint, 1.0f, 0.82f, 0.16f, 0.88f);
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgDrawPath(diamondPath, VG_FILL_PATH);
}

static void createImageMask(void)
{
  VGint x;
  VGint y;
  VGubyte *data;

  imageMask = vgCreateImage(VG_A_8, PANEL_W, PANEL_H,
                            VG_IMAGE_QUALITY_BETTER);
  data = (VGubyte*)malloc(PANEL_W * PANEL_H);
  if (!data)
    return;

  for (y=0; y<PANEL_H; ++y) {
    for (x=0; x<PANEL_W; ++x) {
      VGint dx = x - PANEL_W / 2;
      VGint dy = y - PANEL_H / 2;
      VGint ellipse = dx * dx * 5 + dy * dy * 3 <
                      (PANEL_W * PANEL_W);
      VGint stripe = ((x + y) / 20) & 1;
      data[y * PANEL_W + x] = ellipse ? (stripe ? 255 : 96) : 0;
    }
  }

  vgImageSubData(imageMask, data, PANEL_W, VG_A_8,
                 0, 0, PANEL_W, PANEL_H);
  free(data);
}

static void createMaskLayers(void)
{
  barMask = vgCreateMaskLayer(PANEL_W, PANEL_H);
  savedMask = vgCreateMaskLayer(PANEL_W, PANEL_H);

  vgFillMaskLayer(barMask, 0, 0, PANEL_W, PANEL_H, 0.0f);
  vgFillMaskLayer(barMask, 0, 0, PANEL_W / 3, PANEL_H, 1.0f);
  vgFillMaskLayer(barMask, PANEL_W / 3 + 10, 0,
                  PANEL_W / 3 - 10, PANEL_H, 0.42f);
  vgFillMaskLayer(barMask, (PANEL_W * 2) / 3 + 8, 0,
                  PANEL_W / 3 - 8, PANEL_H, 0.82f);
}

static void createOpenVGContent(void)
{
  VGfloat diamond[] = {
    0.0f, 66.0f,
    54.0f, 0.0f,
    0.0f, -66.0f,
    -54.0f, 0.0f
  };
  VGfloat slash[] = {
    -118.0f, -28.0f,
    106.0f, -66.0f,
    118.0f, -28.0f,
    -106.0f, 66.0f
  };

  panelPath = testCreatePath();
  vguRoundRect(panelPath, 0.0f, 0.0f,
               (VGfloat)PANEL_W, (VGfloat)PANEL_H, 22.0f, 22.0f);

  circlePath = testCreatePath();
  vguEllipse(circlePath, 0.0f, 0.0f, 158.0f, 158.0f);

  diamondPath = createPolygon(diamond, 4);
  slashPath = createPolygon(slash, 4);

  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();

  createImageMask();
  createMaskLayers();
}

static void drawBackground(void)
{
  VGfloat clearColor[] = {0.16f, 0.18f, 0.20f, 1.0f};
  VGfloat x1 = (testWidth() - (PANEL_W * 3 + PANEL_GAP * 2)) * 0.5f;
  VGfloat x2 = x1 + PANEL_W + PANEL_GAP;
  VGfloat x3 = x2 + PANEL_W + PANEL_GAP;

  vgSeti(VG_MASKING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
  vgClear(0, 0, testWidth(), testHeight());

  drawPanelFrame(x1, PANEL_Y);
  drawPanelFrame(x2, PANEL_Y);
  drawPanelFrame(x3, PANEL_Y);
}

static void display(float interval)
{
  VGfloat x1 = (testWidth() - (PANEL_W * 3 + PANEL_GAP * 2)) * 0.5f;
  VGfloat x2 = x1 + PANEL_W + PANEL_GAP;
  VGfloat x3 = x2 + PANEL_W + PANEL_GAP;

  angle += interval * 48.0f;
  if (angle > 360.0f)
    angle -= 360.0f;

  drawBackground();

  vgSeti(VG_MASKING, VG_TRUE);

  clearSurfaceMask();
  vgMask(imageMask, VG_SET_MASK, (VGint)x1, PANEL_Y, PANEL_W, PANEL_H);
  drawArtwork(x1, PANEL_Y, 0.90f, 0.30f, 0.24f);

  clearSurfaceMask();
  vgMask(barMask, VG_SET_MASK, (VGint)x2, PANEL_Y, PANEL_W, PANEL_H);
  drawArtwork(x2, PANEL_Y, 0.10f, 0.62f, 0.86f);

  clearSurfaceMask();
  vgMask(imageMask, VG_SET_MASK, (VGint)x3, PANEL_Y, PANEL_W, PANEL_H);
  vgCopyMask(savedMask, 0, 0, (VGint)x3, PANEL_Y, PANEL_W, PANEL_H);
  clearSurfaceMask();
  vgMask(barMask, VG_SET_MASK, (VGint)x3, PANEL_Y, PANEL_W, PANEL_H);
  vgMask(savedMask, VG_INTERSECT_MASK, (VGint)x3, PANEL_Y, PANEL_W, PANEL_H);
  drawArtwork(x3, PANEL_Y, 0.52f, 0.46f, 0.92f);

  vgSeti(VG_MASKING, VG_FALSE);
  fillSurfaceMask();
  drawPanelStroke(x1, PANEL_Y);
  drawPanelStroke(x2, PANEL_Y);
  drawPanelStroke(x3, PANEL_Y);
}

static void cleanup(void)
{
  if (imageMask != VG_INVALID_HANDLE)
    vgDestroyImage(imageMask);
  if (barMask != VG_INVALID_HANDLE)
    vgDestroyMaskLayer(barMask);
  if (savedMask != VG_INVALID_HANDLE)
    vgDestroyMaskLayer(savedMask);
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
  if (circlePath != VG_INVALID_HANDLE)
    vgDestroyPath(circlePath);
  if (diamondPath != VG_INVALID_HANDLE)
    vgDestroyPath(diamondPath);
  if (slashPath != VG_INVALID_HANDLE)
    vgDestroyPath(slashPath);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 640, 360, "ShaderVG: Masking");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createOpenVGContent();
  testRun();

  return EXIT_SUCCESS;
}
