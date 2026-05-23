#include "test.h"

static VGFont font;
static VGPaint strokePaint;

static void appendPolyline(VGPath path, const VGfloat *coords, int pointCount)
{
  int i;

  if (pointCount <= 0)
    return;

  testMoveTo(path, coords[0], coords[1], VG_ABSOLUTE);
  for (i=1; i<pointCount; ++i)
    testLineTo(path, coords[i*2], coords[i*2+1], VG_ABSOLUTE);
}

static void addGlyph(VGuint glyphIndex,
                     const VGfloat *coords,
                     const int *pointCounts,
                     int contourCount,
                     VGfloat escapement)
{
  int contour;
  const VGfloat *cursor = coords;
  VGPath path = testCreatePath();
  VGfloat origin[2] = {0.0f, 0.0f};
  VGfloat advance[2] = {escapement, 0.0f};

  for (contour=0; contour<contourCount; ++contour) {
    appendPolyline(path, cursor, pointCounts[contour]);
    cursor += pointCounts[contour] * 2;
  }

  vgSetGlyphToPath(font, glyphIndex, path, VG_FALSE, origin, advance);
  vgDestroyPath(path);
}

static void createFont(void)
{
  VGfloat origin[2] = {0.0f, 0.0f};
  VGfloat space[2] = {26.0f, 0.0f};
  VGfloat s[] = {36,48, 0,48, 0,24, 36,24, 36,0, 0,0};
  VGfloat h[] = {0,0, 0,48, 36,0, 36,48, 0,24, 36,24};
  VGfloat a[] = {0,0, 18,48, 36,0, 8,22, 28,22};
  VGfloat d[] = {0,0, 0,48, 26,48, 38,36, 38,12, 26,0, 0,0};
  VGfloat e[] = {36,48, 0,48, 0,0, 36,0, 0,24, 28,24};
  VGfloat r[] = {0,0, 0,48, 30,48, 38,40, 38,28, 30,24, 0,24, 18,24, 40,0};
  VGfloat v[] = {0,48, 18,0, 36,48};
  VGfloat g[] = {38,38, 30,48, 8,48, 0,40, 0,8, 8,0, 32,0, 40,8, 40,22, 24,22};
  int sCounts[] = {6};
  int hCounts[] = {2, 2, 2};
  int aCounts[] = {3, 2};
  int dCounts[] = {7};
  int eCounts[] = {4, 2};
  int rCounts[] = {7, 2};
  int vCounts[] = {3};
  int gCounts[] = {10};

  font = vgCreateFont(8);
  vgSetGlyphToPath(font, ' ', VG_INVALID_HANDLE, VG_FALSE, origin, space);
  addGlyph('S', s, sCounts, 1, 46.0f);
  addGlyph('H', h, hCounts, 3, 46.0f);
  addGlyph('A', a, aCounts, 2, 46.0f);
  addGlyph('D', d, dCounts, 1, 48.0f);
  addGlyph('E', e, eCounts, 2, 44.0f);
  addGlyph('R', r, rCounts, 2, 48.0f);
  addGlyph('V', v, vCounts, 1, 46.0f);
  addGlyph('G', g, gCounts, 1, 50.0f);
}

static void display(float interval)
{
  const VGuint text[] = {'S','H','A','D','E','R',' ','V','G'};
  VGfloat clear[] = {0.03f, 0.04f, 0.05f, 1.0f};
  VGfloat origin[2] = {0.0f, 0.0f};
  VGfloat scaleX = (VGfloat)(testWidth() - 80) / 406.0f;
  VGfloat scaleY = (VGfloat)(testHeight() - 80) / 56.0f;
  VGfloat scale = scaleX < scaleY ? scaleX : scaleY;
  (void)interval;

  if (scale < 0.1f)
    scale = 0.1f;

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());

  vgSeti(VG_STROKE_CAP_STYLE, VG_CAP_ROUND);
  vgSeti(VG_STROKE_JOIN_STYLE, VG_JOIN_ROUND);
  vgSetf(VG_STROKE_LINE_WIDTH, 4.0f);

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(40.0f, (VGfloat)testHeight() * 0.5f - 26.0f * scale);
  vgScale(scale, scale);

  vgSetfv(VG_GLYPH_ORIGIN, 2, origin);
  vgDrawGlyphs(font, 9, text, NULL, NULL, VG_STROKE_PATH, VG_FALSE);
}

static void cleanup(void)
{
  vgDestroyFont(font);
  vgDestroyPaint(strokePaint);
}

int main(int argc, char **argv)
{
  VGfloat strokeColor[] = {0.95f, 0.97f, 0.90f, 1.0f};

  testInit(argc, argv, 640, 220, "ShaderVG: OpenVG Font API");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  strokePaint = vgCreatePaint();
  vgSetParameterfv(strokePaint, VG_PAINT_COLOR, 4, strokeColor);
  vgSetPaint(strokePaint, VG_STROKE_PATH);

  createFont();
  testRun();

  return EXIT_SUCCESS;
}
