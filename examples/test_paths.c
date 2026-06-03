#include "test.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.141592654f
#define DESIGN_WIDTH 1000.0f
#define DESIGN_HEIGHT 620.0f
#define ROUTE_SEGMENT_COUNT 4

static VGPath routePath = VG_INVALID_HANDLE;
static VGPath morphAPath = VG_INVALID_HANDLE;
static VGPath morphBPath = VG_INVALID_HANDLE;
static VGPath morphPath = VG_INVALID_HANDLE;
static VGPath markerPath = VG_INVALID_HANDLE;
static VGPath tickPath = VG_INVALID_HANDLE;
static VGPath tangentPath = VG_INVALID_HANDLE;
static VGPath normalPath = VG_INVALID_HANDLE;
static VGPath dotPath = VG_INVALID_HANDLE;
static VGPath boundsPath = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPaint strokePaint = VG_INVALID_HANDLE;

static VGfloat routeLength = 0.0f;
static VGfloat sceneTime = 0.0f;
static VGboolean paused = VG_FALSE;
static VGboolean showBounds = VG_TRUE;
static VGboolean showTangents = VG_TRUE;
static VGboolean showMorph = VG_TRUE;
static VGboolean showHelp = VG_FALSE;

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

static void setFill(VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  setPaintColor(fillPaint, r, g, b, a);
  vgSetPaint(fillPaint, VG_FILL_PATH);
}

static void clearDash(void)
{
  vgSetfv(VG_STROKE_DASH_PATTERN, 0, NULL);
  vgSetf(VG_STROKE_DASH_PHASE, 0.0f);
}

static void setStroke(VGfloat r, VGfloat g, VGfloat b, VGfloat a,
                      VGfloat width)
{
  setPaintColor(strokePaint, r, g, b, a);
  vgSetPaint(strokePaint, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, width);
}

static VGfloat sceneScale(void)
{
  VGfloat sx = (VGfloat)testWidth() / DESIGN_WIDTH;
  VGfloat sy = (VGfloat)testHeight() / DESIGN_HEIGHT;
  VGfloat scale = sx < sy ? sx : sy;

  if (scale <= 0.0f)
    scale = 1.0f;

  return scale;
}

static void loadSceneTransform(void)
{
  VGfloat scale = sceneScale();
  VGfloat tx = ((VGfloat)testWidth() - DESIGN_WIDTH * scale) * 0.5f;
  VGfloat ty = ((VGfloat)testHeight() - DESIGN_HEIGHT * scale) * 0.5f;

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(tx, ty);
  vgScale(scale, scale);
}

static VGfloat routeAngle(void)
{
  return -8.0f + (VGfloat)sin(sceneTime * 0.45f) * 4.0f;
}

static void loadRouteTransform(void)
{
  loadSceneTransform();
  vgTranslate(560.0f, 305.0f);
  vgRotate(routeAngle());
}

static void appendPathData(VGPath path,
                           const VGubyte *segments,
                           VGint segmentCount,
                           const VGfloat *coords)
{
  if (path != VG_INVALID_HANDLE)
    vgAppendPathData(path, segmentCount, segments, coords);
}

static void createRoutePath(void)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS
  };
  VGfloat coords[] = {
    -430.0f, -138.0f,
    -360.0f,   98.0f, -230.0f, -196.0f, -120.0f,  -22.0f,
     -28.0f,  126.0f,   66.0f,   96.0f,  142.0f,  -12.0f,
     228.0f, -134.0f,  318.0f, -104.0f,  424.0f,  116.0f
  };

  routePath = testCreatePath();
  appendPathData(routePath, segments, ROUTE_SEGMENT_COUNT, coords);
}

static void createMorphPaths(void)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CUBIC_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat a[] = {
       0.0f, -86.0f,
      80.0f, -84.0f, 112.0f, -22.0f,  76.0f,  42.0f,
      36.0f, 116.0f, -52.0f, 112.0f, -82.0f,  32.0f,
    -118.0f, -28.0f, -74.0f, -88.0f,   0.0f, -86.0f,
       0.0f, -86.0f,   0.0f, -86.0f,   0.0f, -86.0f
  };
  VGfloat b[] = {
     -10.0f, -104.0f,
      58.0f, -140.0f, 132.0f,  -72.0f, 112.0f,   8.0f,
      96.0f,   82.0f,  16.0f,  128.0f, -42.0f,  74.0f,
    -112.0f,  108.0f,-148.0f,   -4.0f, -70.0f, -36.0f,
     -34.0f,  -52.0f, -76.0f, -104.0f, -10.0f,-104.0f
  };

  morphAPath = testCreatePath();
  morphBPath = testCreatePath();
  morphPath = testCreatePath();
  appendPathData(morphAPath, segments, 6, a);
  appendPathData(morphBPath, segments, 6, b);
}

static VGPath createLinePath(VGfloat x0, VGfloat y0,
                             VGfloat x1, VGfloat y1)
{
  VGPath path = testCreatePath();

  if (path != VG_INVALID_HANDLE) {
    testMoveTo(path, x0, y0, VG_ABSOLUTE);
    testLineTo(path, x1, y1, VG_ABSOLUTE);
  }

  return path;
}

static void createMarkerPath(void)
{
  VGubyte segments[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat coords[] = {
     18.0f,  0.0f,
     -8.0f, -9.0f,
     -3.0f,  0.0f,
     -8.0f,  9.0f
  };

  markerPath = testCreatePath();
  appendPathData(markerPath, segments, 5, coords);
}

static void createContent(void)
{
  createRoutePath();
  createMorphPaths();
  createMarkerPath();

  tickPath = createLinePath(0.0f, -9.0f, 0.0f, 9.0f);
  tangentPath = createLinePath(0.0f, 0.0f, 38.0f, 0.0f);
  normalPath = createLinePath(0.0f, 0.0f, 0.0f, 26.0f);
  dotPath = testCreatePath();
  boundsPath = testCreatePath();
  fillPaint = vgCreatePaint();
  strokePaint = vgCreatePaint();

  if (dotPath != VG_INVALID_HANDLE)
    vguEllipse(dotPath, 0.0f, 0.0f, 10.0f, 10.0f);

  if (routePath == VG_INVALID_HANDLE ||
      morphAPath == VG_INVALID_HANDLE ||
      morphBPath == VG_INVALID_HANDLE ||
      morphPath == VG_INVALID_HANDLE ||
      markerPath == VG_INVALID_HANDLE ||
      tickPath == VG_INVALID_HANDLE ||
      tangentPath == VG_INVALID_HANDLE ||
      normalPath == VG_INVALID_HANDLE ||
      dotPath == VG_INVALID_HANDLE ||
      boundsPath == VG_INVALID_HANDLE ||
      fillPaint == VG_INVALID_HANDLE ||
      strokePaint == VG_INVALID_HANDLE)
    exit(EXIT_FAILURE);

  routeLength = vgPathLength(routePath, 0, ROUTE_SEGMENT_COUNT);
  if (routeLength <= 0.0f || vgGetError() != VG_NO_ERROR)
    exit(EXIT_FAILURE);
}

static void drawRectPath(VGfloat x, VGfloat y, VGfloat w, VGfloat h,
                         VGfloat r, VGfloat g, VGfloat b, VGfloat a,
                         VGfloat width)
{
  if (w < 0.0f || h < 0.0f)
    return;

  vgClearPath(boundsPath, VG_PATH_CAPABILITY_ALL);
  if (vguRect(boundsPath, x, y, w, h) != VGU_NO_ERROR)
    return;

  clearDash();
  setStroke(r, g, b, a, width);
  vgDrawPath(boundsPath, VG_STROKE_PATH);
}

static void drawMarkerAt(VGfloat distance,
                         VGfloat r, VGfloat g, VGfloat b, VGfloat a,
                         VGboolean filledArrow)
{
  VGfloat x, y, tx, ty;
  VGfloat angle;

  vgPointAlongPath(routePath, 0, ROUTE_SEGMENT_COUNT, distance,
                   &x, &y, &tx, &ty);
  if (vgGetError() != VG_NO_ERROR)
    return;

  angle = (VGfloat)atan2(ty, tx) * 180.0f / PI;
  loadRouteTransform();
  vgTranslate(x, y);
  vgRotate(angle);

  if (filledArrow) {
    setFill(r, g, b, a);
    setStroke(0.03f, 0.04f, 0.05f, 0.95f, 1.8f);
    vgDrawPath(markerPath, VG_FILL_PATH | VG_STROKE_PATH);
  } else {
    setFill(r, g, b, a);
    vgDrawPath(dotPath, VG_FILL_PATH);
  }
}

static void drawRouteTicks(void)
{
  int i;
  int count = 21;

  clearDash();
  setStroke(0.82f, 0.88f, 0.92f, 0.50f, 1.8f);
  for (i=0; i<count; ++i) {
    VGfloat x, y, tx, ty;
    VGfloat d = routeLength * (VGfloat)i / (VGfloat)(count - 1);
    VGfloat angle;

    vgPointAlongPath(routePath, 0, ROUTE_SEGMENT_COUNT, d, &x, &y, &tx, &ty);
    if (vgGetError() != VG_NO_ERROR)
      continue;

    angle = (VGfloat)atan2(ty, tx) * 180.0f / PI;
    loadRouteTransform();
    vgTranslate(x, y);
    vgRotate(angle);
    vgDrawPath(tickPath, VG_STROKE_PATH);
  }
}

static void drawTangentSamples(void)
{
  int i;
  int count = 7;

  for (i=0; i<count; ++i) {
    VGfloat x, y, tx, ty;
    VGfloat d = routeLength * ((VGfloat)i + 0.5f) / (VGfloat)count;
    VGfloat angle;

    vgPointAlongPath(routePath, 0, ROUTE_SEGMENT_COUNT, d, &x, &y, &tx, &ty);
    if (vgGetError() != VG_NO_ERROR)
      continue;

    angle = (VGfloat)atan2(ty, tx) * 180.0f / PI;
    loadRouteTransform();
    vgTranslate(x, y);
    vgRotate(angle);

    clearDash();
    setStroke(0.10f, 0.74f, 1.0f, 0.88f, 2.0f);
    vgDrawPath(tangentPath, VG_STROKE_PATH);
    setStroke(1.0f, 0.36f, 0.68f, 0.84f, 1.8f);
    vgDrawPath(normalPath, VG_STROKE_PATH);
  }
}

static void drawRouteBounds(void)
{
  VGfloat minX, minY, width, height;

  vgPathBounds(routePath, &minX, &minY, &width, &height);
  if (vgGetError() == VG_NO_ERROR) {
    loadRouteTransform();
    drawRectPath(minX, minY, width, height,
                 0.10f, 0.70f, 1.0f, 0.50f, 1.2f);
  }

  loadRouteTransform();
  vgPathTransformedBounds(routePath, &minX, &minY, &width, &height);
  if (vgGetError() == VG_NO_ERROR) {
    vgLoadIdentity();
    drawRectPath(minX, minY, width, height,
                 1.0f, 0.77f, 0.22f, 0.62f, 1.4f);
  }
}

static void drawRoute(void)
{
  VGfloat dash[] = {28.0f, 13.0f, 5.0f, 13.0f};
  VGfloat phase = (VGfloat)fmod(sceneTime * 78.0f, 59.0f);
  VGfloat markerDistance;

  vgSeti(VG_STROKE_CAP_STYLE, VG_CAP_ROUND);
  vgSeti(VG_STROKE_JOIN_STYLE, VG_JOIN_ROUND);

  loadRouteTransform();
  clearDash();
  setStroke(0.02f, 0.03f, 0.05f, 0.86f, 14.0f);
  vgDrawPath(routePath, VG_STROKE_PATH);

  loadRouteTransform();
  vgSetfv(VG_STROKE_DASH_PATTERN, 4, dash);
  vgSetf(VG_STROKE_DASH_PHASE, phase);
  setStroke(0.86f, 0.92f, 1.0f, 0.96f, 5.0f);
  vgDrawPath(routePath, VG_STROKE_PATH);
  clearDash();

  drawRouteTicks();
  drawMarkerAt(0.0f, 0.28f, 1.0f, 0.64f, 0.92f, VG_FALSE);
  drawMarkerAt(routeLength, 1.0f, 0.34f, 0.36f, 0.92f, VG_FALSE);

  if (showTangents)
    drawTangentSamples();

  markerDistance = (VGfloat)fmod(sceneTime * 92.0f, routeLength);
  drawMarkerAt(markerDistance, 1.0f, 0.86f, 0.14f, 1.0f, VG_TRUE);

  if (showBounds)
    drawRouteBounds();
}

static void drawMorph(void)
{
  VGfloat amount = ((VGfloat)sin(sceneTime * 0.80f) + 1.0f) * 0.5f;

  vgClearPath(morphPath, VG_PATH_CAPABILITY_ALL);
  vgInterpolatePath(morphPath, morphAPath, morphBPath, amount);
  if (vgGetError() != VG_NO_ERROR)
    return;

  loadSceneTransform();
  vgTranslate(244.0f, 382.0f);
  vgScale(1.35f, 1.35f);
  vgRotate(-10.0f + amount * 20.0f);

  setFill(0.30f, 0.86f, 0.72f, 0.32f);
  setStroke(0.72f, 1.0f, 0.86f, 0.82f, 2.0f);
  clearDash();
  vgDrawPath(morphPath, VG_FILL_PATH | VG_STROKE_PATH);
}

static void drawHelpGlyphs(void)
{
  int i;
  VGfloat step = 22.0f;

  loadSceneTransform();
  vgTranslate(54.0f, 542.0f);
  clearDash();
  setStroke(0.85f, 0.92f, 1.0f, 0.42f, 1.4f);

  for (i=0; i<5; ++i) {
    vgLoadIdentity();
    loadSceneTransform();
    vgTranslate(54.0f + step * (VGfloat)i, 542.0f);
    vgRotate((VGfloat)i * 22.0f);
    vgDrawPath(markerPath, VG_STROKE_PATH);
  }
}

static void updateHelpOverlay(void)
{
  if (showHelp) {
    testOverlayString("Space pause | B bounds | T tangents | M morph | H help");
  } else {
    testOverlayString("");
  }
}

static void display(float interval)
{
  VGfloat clear[] = {0.035f, 0.040f, 0.052f, 1.0f};

  if (interval > 0.10f)
    interval = 0.10f;
  if (!paused)
    sceneTime += interval;

  vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
  vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
  vgSeti(VG_MASKING, VG_FALSE);
  vgSeti(VG_SCISSORING, VG_FALSE);
  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());

  if (showMorph)
    drawMorph();
  drawRoute();
  if (showHelp)
    drawHelpGlyphs();
}

static void key(unsigned char key, int x, int y)
{
  (void)x;
  (void)y;

  switch (key) {
  case ' ':
    paused = paused ? VG_FALSE : VG_TRUE;
    break;
  case 'b': case 'B':
    showBounds = showBounds ? VG_FALSE : VG_TRUE;
    break;
  case 't': case 'T':
    showTangents = showTangents ? VG_FALSE : VG_TRUE;
    break;
  case 'm': case 'M':
    showMorph = showMorph ? VG_FALSE : VG_TRUE;
    break;
  case 'h': case 'H':
    showHelp = showHelp ? VG_FALSE : VG_TRUE;
    updateHelpOverlay();
    break;
  default:
    break;
  }

  testPostRedisplay();
}

static void cleanup(void)
{
  if (routePath != VG_INVALID_HANDLE)
    vgDestroyPath(routePath);
  if (morphAPath != VG_INVALID_HANDLE)
    vgDestroyPath(morphAPath);
  if (morphBPath != VG_INVALID_HANDLE)
    vgDestroyPath(morphBPath);
  if (morphPath != VG_INVALID_HANDLE)
    vgDestroyPath(morphPath);
  if (markerPath != VG_INVALID_HANDLE)
    vgDestroyPath(markerPath);
  if (tickPath != VG_INVALID_HANDLE)
    vgDestroyPath(tickPath);
  if (tangentPath != VG_INVALID_HANDLE)
    vgDestroyPath(tangentPath);
  if (normalPath != VG_INVALID_HANDLE)
    vgDestroyPath(normalPath);
  if (dotPath != VG_INVALID_HANDLE)
    vgDestroyPath(dotPath);
  if (boundsPath != VG_INVALID_HANDLE)
    vgDestroyPath(boundsPath);
  if (fillPaint != VG_INVALID_HANDLE)
    vgDestroyPaint(fillPaint);
  if (strokePaint != VG_INVALID_HANDLE)
    vgDestroyPaint(strokePaint);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 1000, 620, "ShaderVG: Paths");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_KEY, (CallbackFunc)key);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  updateHelpOverlay();
  fprintf(stderr,
          "test_paths controls: Space pause, B bounds, T tangents, M morph, H help\n");
  testRun();

  return EXIT_SUCCESS;
}
