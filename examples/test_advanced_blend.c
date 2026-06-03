#include "test.h"

#include <VG/vgext.h>

#define ADV_BLEND_COUNT 23
#define ADV_BLEND_COLUMNS 6
#define ADV_BLEND_ROWS ((ADV_BLEND_COUNT + ADV_BLEND_COLUMNS - 1) / ADV_BLEND_COLUMNS)

#define PANEL_WIDTH 132.0f
#define PANEL_HEIGHT 112.0f
#define PANEL_GAP 16.0f

typedef struct
{
  VGBlendMode mode;
  VGfloat tint[4];
} AdvancedBlendPanel;

static AdvancedBlendPanel panels[ADV_BLEND_COUNT] = {
  { VG_BLEND_OVERLAY_KHR,       { 0.22f, 0.42f, 0.64f, 1.0f } },
  { VG_BLEND_HARDLIGHT_KHR,     { 0.48f, 0.26f, 0.44f, 1.0f } },
  { VG_BLEND_SOFTLIGHT_SVG_KHR, { 0.33f, 0.48f, 0.40f, 1.0f } },
  { VG_BLEND_SOFTLIGHT_KHR,     { 0.50f, 0.44f, 0.28f, 1.0f } },
  { VG_BLEND_COLORDODGE_KHR,    { 0.28f, 0.46f, 0.58f, 1.0f } },
  { VG_BLEND_COLORBURN_KHR,     { 0.56f, 0.34f, 0.30f, 1.0f } },
  { VG_BLEND_DIFFERENCE_KHR,    { 0.38f, 0.38f, 0.58f, 1.0f } },
  { VG_BLEND_SUBTRACT_KHR,      { 0.42f, 0.32f, 0.48f, 1.0f } },
  { VG_BLEND_INVERT_KHR,        { 0.30f, 0.48f, 0.46f, 1.0f } },
  { VG_BLEND_EXCLUSION_KHR,     { 0.52f, 0.40f, 0.32f, 1.0f } },
  { VG_BLEND_LINEARDODGE_KHR,   { 0.24f, 0.42f, 0.56f, 1.0f } },
  { VG_BLEND_LINEARBURN_KHR,    { 0.56f, 0.30f, 0.34f, 1.0f } },
  { VG_BLEND_VIVIDLIGHT_KHR,    { 0.40f, 0.34f, 0.60f, 1.0f } },
  { VG_BLEND_LINEARLIGHT_KHR,   { 0.54f, 0.44f, 0.25f, 1.0f } },
  { VG_BLEND_PINLIGHT_KHR,      { 0.30f, 0.48f, 0.36f, 1.0f } },
  { VG_BLEND_HARDMIX_KHR,       { 0.50f, 0.30f, 0.42f, 1.0f } },
  { VG_BLEND_CLEAR_KHR,         { 0.30f, 0.34f, 0.42f, 1.0f } },
  { VG_BLEND_DST_KHR,           { 0.32f, 0.42f, 0.34f, 1.0f } },
  { VG_BLEND_SRC_OUT_KHR,       { 0.44f, 0.34f, 0.30f, 1.0f } },
  { VG_BLEND_DST_OUT_KHR,       { 0.30f, 0.40f, 0.52f, 1.0f } },
  { VG_BLEND_SRC_ATOP_KHR,      { 0.45f, 0.35f, 0.52f, 1.0f } },
  { VG_BLEND_DST_ATOP_KHR,      { 0.34f, 0.48f, 0.46f, 1.0f } },
  { VG_BLEND_XOR_KHR,           { 0.54f, 0.36f, 0.28f, 1.0f } }
};

static VGPath panelPath = VG_INVALID_HANDLE;
static VGPath unitRectPath = VG_INVALID_HANDLE;
static VGPath circlePath = VG_INVALID_HANDLE;
static VGPath diamondPath = VG_INVALID_HANDLE;
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

static void drawRect(VGfloat x, VGfloat y, VGfloat width, VGfloat height)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgScale(width, height);
  vgDrawPath(unitRectPath, VG_FILL_PATH);
}

static void drawPanelFill(VGfloat x, VGfloat y, AdvancedBlendPanel *panel)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, panel->tint);
  vgDrawPath(panelPath, VG_FILL_PATH);
}

static void drawDestinationScene(VGfloat x, VGfloat y, int index)
{
  int stripe;

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  drawPanelFill(x, y, &panels[index]);

  vgSetPaint(fillPaint, VG_FILL_PATH);
  for (stripe=0; stripe<5; ++stripe) {
    VGfloat k = (VGfloat)stripe / 4.0f;
    setPaintColor(fillPaint,
                  0.82f - 0.36f * k,
                  0.34f + 0.32f * k,
                  0.20f + 0.48f * k,
                  0.86f);
    drawRect(x + 8.0f,
             y + 10.0f + (VGfloat)stripe * 18.0f,
             PANEL_WIDTH - 16.0f,
             12.0f);
  }

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x + 38.0f, y + 70.0f);
  setPaintColor(fillPaint, 0.08f, 0.55f, 0.90f, 0.78f);
  vgDrawPath(circlePath, VG_FILL_PATH);

  vgLoadIdentity();
  vgTranslate(x + 93.0f, y + 42.0f);
  setPaintColor(fillPaint, 0.95f, 0.78f, 0.16f, 0.72f);
  vgDrawPath(diamondPath, VG_FILL_PATH);
}

static void drawSourceScene(VGfloat x, VGfloat y, VGBlendMode mode)
{
  vgSetPaint(fillPaint, VG_FILL_PATH);
  vgSeti(VG_BLEND_MODE, mode);

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x + 46.0f, y + 42.0f);
  setPaintColor(fillPaint, 0.98f, 0.12f, 0.42f, 0.68f);
  vgDrawPath(circlePath, VG_FILL_PATH);

  vgLoadIdentity();
  vgTranslate(x + 88.0f, y + 70.0f);
  setPaintColor(fillPaint, 0.08f, 0.90f, 0.78f, 0.68f);
  vgDrawPath(circlePath, VG_FILL_PATH);

  vgLoadIdentity();
  vgTranslate(x + 64.0f, y + 55.0f);
  vgRotate(28.0f);
  vgScale(72.0f, 18.0f);
  setPaintColor(fillPaint, 0.96f, 0.96f, 0.10f, 0.72f);
  vgDrawPath(unitRectPath, VG_FILL_PATH);
}

static void drawPanelFrame(VGfloat x, VGfloat y)
{
  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);

  vgSetPaint(strokePaint, VG_STROKE_PATH);
  setPaintColor(strokePaint, 0.86f, 0.88f, 0.90f, 0.68f);
  vgSetf(VG_STROKE_LINE_WIDTH, 1.5f);
  vgDrawPath(panelPath, VG_STROKE_PATH);
}

static void display(float interval)
{
  VGfloat clear[] = { 0.05f, 0.06f, 0.07f, 1.0f };
  VGfloat gridWidth = PANEL_WIDTH * ADV_BLEND_COLUMNS +
                      PANEL_GAP * (ADV_BLEND_COLUMNS - 1);
  VGfloat gridHeight = PANEL_HEIGHT * ADV_BLEND_ROWS +
                       PANEL_GAP * (ADV_BLEND_ROWS - 1);
  VGfloat x0 = ((VGfloat)testWidth() - gridWidth) * 0.5f;
  VGfloat y0 = ((VGfloat)testHeight() - gridHeight) * 0.5f;
  int i;

  (void)interval;

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());
  vgSeti(VG_MASKING, VG_FALSE);

  for (i=0; i<ADV_BLEND_COUNT; ++i) {
    VGint column = i % ADV_BLEND_COLUMNS;
    VGint row = i / ADV_BLEND_COLUMNS;
    VGfloat x = x0 + (VGfloat)column * (PANEL_WIDTH + PANEL_GAP);
    VGfloat y = y0 + (VGfloat)(ADV_BLEND_ROWS - 1 - row) *
                     (PANEL_HEIGHT + PANEL_GAP);
    VGint scissor[4];

    scissor[0] = (VGint)x;
    scissor[1] = (VGint)y;
    scissor[2] = (VGint)PANEL_WIDTH;
    scissor[3] = (VGint)PANEL_HEIGHT;
    vgSetiv(VG_SCISSOR_RECTS, 4, scissor);
    vgSeti(VG_SCISSORING, VG_TRUE);

    drawDestinationScene(x, y, i);
    drawSourceScene(x, y, panels[i].mode);

    vgSeti(VG_SCISSORING, VG_FALSE);
    drawPanelFrame(x, y);
  }
}

static void cleanup(void)
{
  if (panelPath != VG_INVALID_HANDLE)
    vgDestroyPath(panelPath);
  if (unitRectPath != VG_INVALID_HANDLE)
    vgDestroyPath(unitRectPath);
  if (circlePath != VG_INVALID_HANDLE)
    vgDestroyPath(circlePath);
  if (diamondPath != VG_INVALID_HANDLE)
    vgDestroyPath(diamondPath);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

static void createContent(void)
{
  panelPath = testCreatePath();
  unitRectPath = testCreatePath();
  circlePath = testCreatePath();
  diamondPath = testCreatePath();
  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();

  if (panelPath == VG_INVALID_HANDLE ||
      unitRectPath == VG_INVALID_HANDLE ||
      circlePath == VG_INVALID_HANDLE ||
      diamondPath == VG_INVALID_HANDLE ||
      fillPaint == VG_INVALID_HANDLE ||
      strokePaint == VG_INVALID_HANDLE)
    exit(EXIT_FAILURE);

  if (vguRoundRect(panelPath, 0.0f, 0.0f,
                   PANEL_WIDTH, PANEL_HEIGHT, 6.0f, 6.0f) != VGU_NO_ERROR ||
      vguRect(unitRectPath, 0.0f, 0.0f, 1.0f, 1.0f) != VGU_NO_ERROR ||
      vguEllipse(circlePath, 0.0f, 0.0f, 58.0f, 58.0f) != VGU_NO_ERROR)
    exit(EXIT_FAILURE);

  testMoveTo(diamondPath, 0.0f, 30.0f, VG_ABSOLUTE);
  testLineTo(diamondPath, 26.0f, 0.0f, VG_ABSOLUTE);
  testLineTo(diamondPath, 0.0f, -30.0f, VG_ABSOLUTE);
  testLineTo(diamondPath, -26.0f, 0.0f, VG_ABSOLUTE);
  testClosePath(diamondPath);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 960, 620, "ShaderVG: Advanced Blend Test");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testRun();

  return EXIT_SUCCESS;
}
