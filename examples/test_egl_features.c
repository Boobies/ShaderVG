#include "test.h"

#define IMAGE_WIDTH 192
#define IMAGE_HEIGHT 128

static EGLDisplay eglDisplayHandle = EGL_NO_DISPLAY;
static EGLSurface windowSurface = EGL_NO_SURFACE;
static EGLSurface scratchSurface = EGL_NO_SURFACE;
static EGLSurface imageSurface = EGL_NO_SURFACE;
static EGLContext baseContext = EGL_NO_CONTEXT;
static EGLContext pbufferContext = EGL_NO_CONTEXT;
static EGLContext sharedContext = EGL_NO_CONTEXT;

static VGImage offscreenImage = VG_INVALID_HANDLE;
static VGMaskLayer windowMask = VG_INVALID_HANDLE;
static VGPaint fillPaint = VG_INVALID_HANDLE;
static VGPath unitRect = VG_INVALID_HANDLE;
static VGPath unitEllipse = VG_INVALID_HANDLE;
static VGPath diamond = VG_INVALID_HANDLE;

static VGint maskWidth = 0;
static VGint maskHeight = 0;
static VGfloat phase = 0.0f;

static void failEGL(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
  exit(EXIT_FAILURE);
}

static void failVG(const char *message)
{
  fprintf(stderr, "%s (VG error 0x%04x)\n", message, vgGetError());
  exit(EXIT_FAILURE);
}

static void checkVG(const char *message)
{
  VGErrorCode error = vgGetError();

  if (error != VG_NO_ERROR) {
    fprintf(stderr, "%s (VG error 0x%04x)\n", message, error);
    exit(EXIT_FAILURE);
  }
}

static EGLConfig chooseConfig(EGLint surfaceType)
{
  EGLConfig config = NULL;
  EGLint count = 0;
  EGLint attribs[] = {
    EGL_SURFACE_TYPE, surfaceType,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_ALPHA_MASK_SIZE, 8,
    EGL_NONE
  };

  if (!eglChooseConfig(eglDisplayHandle, attribs, &config, 1, &count) ||
      count < 1)
    failEGL("Could not choose an OpenVG EGL config");

  return config;
}

static VGPath createUnitRect(void)
{
  VGPath path = testCreatePath();

  if (path == VG_INVALID_HANDLE)
    failVG("Could not create a rectangle path");

  testMoveTo(path, 0.0f, 0.0f, VG_ABSOLUTE);
  testLineTo(path, 1.0f, 0.0f, VG_ABSOLUTE);
  testLineTo(path, 1.0f, 1.0f, VG_ABSOLUTE);
  testLineTo(path, 0.0f, 1.0f, VG_ABSOLUTE);
  testClosePath(path);
  checkVG("Could not build a rectangle path");

  return path;
}

static VGPath createUnitEllipse(void)
{
  VGPath path = testCreatePath();

  if (path == VG_INVALID_HANDLE ||
      vguEllipse(path, -0.5f, -0.5f, 1.0f, 1.0f) != VGU_NO_ERROR)
    failVG("Could not create an ellipse path");

  return path;
}

static VGPath createDiamond(void)
{
  VGPath path = testCreatePath();

  if (path == VG_INVALID_HANDLE)
    failVG("Could not create a diamond path");

  testMoveTo(path, 0.0f, 0.58f, VG_ABSOLUTE);
  testLineTo(path, 0.58f, 0.0f, VG_ABSOLUTE);
  testLineTo(path, 0.0f, -0.58f, VG_ABSOLUTE);
  testLineTo(path, -0.58f, 0.0f, VG_ABSOLUTE);
  testClosePath(path);
  checkVG("Could not build a diamond path");

  return path;
}

static void setFillColor(VGfloat r, VGfloat g, VGfloat b, VGfloat a)
{
  VGfloat color[] = {r, g, b, a};

  vgSetParameterfv(fillPaint, VG_PAINT_COLOR, 4, color);
  vgSetPaint(fillPaint, VG_FILL_PATH);
}

static void drawRect(VGfloat x, VGfloat y, VGfloat width, VGfloat height)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgScale(width, height);
  vgDrawPath(unitRect, VG_FILL_PATH);
}

static void drawCentered(VGPath path,
                         VGfloat x, VGfloat y,
                         VGfloat width, VGfloat height,
                         VGfloat rotation)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgRotate(rotation);
  vgScale(width, height);
  vgDrawPath(path, VG_FILL_PATH);
}

static void drawImageBox(VGfloat x, VGfloat y, VGfloat width, VGfloat height)
{
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(x, y);
  vgScale(width / (VGfloat)IMAGE_WIDTH, height / (VGfloat)IMAGE_HEIGHT);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawImage(offscreenImage);
}

static void drawFrame(VGfloat x, VGfloat y, VGfloat width, VGfloat height)
{
  setFillColor(0.04f, 0.05f, 0.06f, 1.0f);
  drawRect(x - 4.0f, y - 4.0f, width + 8.0f, 4.0f);
  drawRect(x - 4.0f, y + height, width + 8.0f, 4.0f);
  drawRect(x - 4.0f, y, 4.0f, height);
  drawRect(x + width, y, 4.0f, height);
}

static void renderImagePbuffer(void)
{
  VGfloat clear[] = {0.02f, 0.03f, 0.05f, 1.0f};
  int i;

  if (!eglMakeCurrent(eglDisplayHandle,
                      imageSurface, imageSurface,
                      pbufferContext))
    failEGL("Could not bind the OpenVG image pbuffer");

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, IMAGE_WIDTH, IMAGE_HEIGHT);

  for (i=0; i<4; ++i) {
    VGfloat shade = 0.09f + (VGfloat)i * 0.035f;
    setFillColor(shade, 0.16f + (VGfloat)i * 0.035f, 0.24f, 1.0f);
    drawRect((VGfloat)i * ((VGfloat)IMAGE_WIDTH / 4.0f),
             0.0f,
             (VGfloat)IMAGE_WIDTH / 4.0f,
             (VGfloat)IMAGE_HEIGHT);
  }

  setFillColor(0.14f, 0.68f, 0.72f, 0.82f);
  drawCentered(unitEllipse,
               (VGfloat)IMAGE_WIDTH * 0.5f,
               (VGfloat)IMAGE_HEIGHT * 0.5f,
               150.0f, 76.0f, phase * 0.35f);

  setFillColor(0.95f, 0.34f, 0.20f, 0.94f);
  drawCentered(diamond,
               (VGfloat)IMAGE_WIDTH * 0.5f,
               (VGfloat)IMAGE_HEIGHT * 0.5f,
               68.0f, 68.0f, -phase);

  setFillColor(1.0f, 0.86f, 0.28f, 0.76f);
  drawCentered(unitEllipse,
               (VGfloat)IMAGE_WIDTH * 0.72f,
               (VGfloat)IMAGE_HEIGHT * 0.72f,
               36.0f, 36.0f, 0.0f);

  vgFinish();
  checkVG("Could not render to the OpenVG image pbuffer");
}

static void ensureWindowMask(void)
{
  VGint width = testWidth();
  VGint height = testHeight();

  if (windowMask != VG_INVALID_HANDLE &&
      maskWidth == width &&
      maskHeight == height)
    return;

  if (windowMask != VG_INVALID_HANDLE)
    vgDestroyMaskLayer(windowMask);

  windowMask = vgCreateMaskLayer(width, height);
  if (windowMask == VG_INVALID_HANDLE)
    failVG("Could not create the shared window mask layer");

  maskWidth = width;
  maskHeight = height;
}

static void drawWindowSurface(void)
{
  VGfloat clear[] = {0.88f, 0.90f, 0.89f, 1.0f};
  VGfloat width = (VGfloat)testWidth();
  VGfloat height = (VGfloat)testHeight();
  VGfloat margin = 38.0f;
  VGfloat panelWidth = (width - margin * 4.0f) / 3.0f;
  VGfloat panelHeight = panelWidth * ((VGfloat)IMAGE_HEIGHT / (VGfloat)IMAGE_WIDTH);
  VGfloat panelY = height * 0.50f - panelHeight * 0.5f;
  VGfloat leftX = margin;
  VGfloat centerX = margin * 2.0f + panelWidth;
  VGfloat rightX = margin * 3.0f + panelWidth * 2.0f;
  VGfloat rightHeight = height - 130.0f;
  VGfloat rightY = 65.0f;
  VGint maskX = (VGint)(rightX + panelWidth * 0.20f);
  VGint maskY = (VGint)(rightY + rightHeight * 0.12f);
  VGint maskW = (VGint)(panelWidth * 0.54f);
  VGint maskH = (VGint)(rightHeight * 0.76f);

  if (!eglMakeCurrent(eglDisplayHandle,
                      windowSurface, windowSurface,
                      sharedContext))
    failEGL("Could not bind the shared OpenVG window context");

  ensureWindowMask();

  vgSetfv(VG_CLEAR_COLOR, 4, clear);
  vgClear(0, 0, testWidth(), testHeight());

  setFillColor(0.72f, 0.79f, 0.77f, 1.0f);
  drawRect(0.0f, 0.0f, width, height * 0.30f);

  setFillColor(1.0f, 1.0f, 1.0f, 0.55f);
  drawRect(leftX - 18.0f, panelY - 24.0f,
           panelWidth + 36.0f, panelHeight + 48.0f);
  drawRect(centerX - 18.0f, panelY - 24.0f,
           panelWidth + 36.0f, panelHeight + 48.0f);
  drawRect(rightX - 18.0f, rightY - 24.0f,
           panelWidth + 36.0f, rightHeight + 48.0f);

  drawImageBox(leftX, panelY, panelWidth, panelHeight);
  drawFrame(leftX, panelY, panelWidth, panelHeight);

  setFillColor(0.10f, 0.48f, 0.70f, 0.86f);
  drawCentered(unitEllipse,
               centerX + panelWidth * 0.50f,
               panelY + panelHeight * 0.52f,
               panelWidth * 0.92f,
               panelHeight * 0.72f,
               -phase * 0.25f);

  setFillColor(0.93f, 0.30f, 0.22f, 0.92f);
  drawCentered(diamond,
               centerX + panelWidth * 0.50f,
               panelY + panelHeight * 0.52f,
               panelWidth * 0.42f,
               panelWidth * 0.42f,
               phase);

  setFillColor(0.04f, 0.05f, 0.06f, 1.0f);
  drawRect(centerX + panelWidth * 0.13f,
           panelY + panelHeight * 0.16f,
           panelWidth * 0.74f,
           8.0f);

  vgFillMaskLayer(windowMask, 0, 0, testWidth(), testHeight(), 0.0f);
  vgFillMaskLayer(windowMask, maskX, maskY, maskW, maskH, 1.0f);
  vgSeti(VG_MASKING, VG_TRUE);
  vgMask(windowMask, VG_SET_MASK, 0, 0, testWidth(), testHeight());
  drawImageBox(rightX - panelWidth * 0.28f,
               rightY + rightHeight * 0.12f,
               panelWidth * 1.55f,
               rightHeight * 0.76f);
  vgSeti(VG_MASKING, VG_FALSE);

  drawFrame(rightX + panelWidth * 0.20f,
            rightY + rightHeight * 0.12f,
            panelWidth * 0.54f,
            rightHeight * 0.76f);

  checkVG("Could not draw the shared OpenVG window scene");
}

static void display(VGfloat interval)
{
  phase += interval * 62.0f;
  while (phase > 360.0f)
    phase -= 360.0f;

  renderImagePbuffer();
  drawWindowSurface();
}

static void cleanup(void)
{
  if (eglDisplayHandle == EGL_NO_DISPLAY)
    return;

  if (sharedContext != EGL_NO_CONTEXT &&
      windowSurface != EGL_NO_SURFACE)
    eglMakeCurrent(eglDisplayHandle, windowSurface, windowSurface, sharedContext);

  if (windowMask != VG_INVALID_HANDLE) {
    vgDestroyMaskLayer(windowMask);
    windowMask = VG_INVALID_HANDLE;
  }

  if (pbufferContext != EGL_NO_CONTEXT &&
      scratchSurface != EGL_NO_SURFACE)
    eglMakeCurrent(eglDisplayHandle, scratchSurface, scratchSurface, pbufferContext);

  if (imageSurface != EGL_NO_SURFACE) {
    eglDestroySurface(eglDisplayHandle, imageSurface);
    imageSurface = EGL_NO_SURFACE;
  }

  if (offscreenImage != VG_INVALID_HANDLE) {
    vgDestroyImage(offscreenImage);
    offscreenImage = VG_INVALID_HANDLE;
  }
  if (diamond != VG_INVALID_HANDLE) {
    vgDestroyPath(diamond);
    diamond = VG_INVALID_HANDLE;
  }
  if (unitEllipse != VG_INVALID_HANDLE) {
    vgDestroyPath(unitEllipse);
    unitEllipse = VG_INVALID_HANDLE;
  }
  if (unitRect != VG_INVALID_HANDLE) {
    vgDestroyPath(unitRect);
    unitRect = VG_INVALID_HANDLE;
  }
  if (fillPaint != VG_INVALID_HANDLE) {
    vgDestroyPaint(fillPaint);
    fillPaint = VG_INVALID_HANDLE;
  }

  if (baseContext != EGL_NO_CONTEXT &&
      windowSurface != EGL_NO_SURFACE)
    eglMakeCurrent(eglDisplayHandle, windowSurface, windowSurface, baseContext);

  if (scratchSurface != EGL_NO_SURFACE) {
    eglDestroySurface(eglDisplayHandle, scratchSurface);
    scratchSurface = EGL_NO_SURFACE;
  }

  if (pbufferContext != EGL_NO_CONTEXT) {
    eglDestroyContext(eglDisplayHandle, pbufferContext);
    pbufferContext = EGL_NO_CONTEXT;
  }

  if (sharedContext != EGL_NO_CONTEXT) {
    eglDestroyContext(eglDisplayHandle, sharedContext);
    sharedContext = EGL_NO_CONTEXT;
  }
}

static void createContent(void)
{
  EGLConfig windowConfig;
  EGLConfig pbufferConfig;
  EGLint alphaMaskSize = 0;
  EGLint scratchAttribs[] = {
    EGL_WIDTH, IMAGE_WIDTH,
    EGL_HEIGHT, IMAGE_HEIGHT,
    EGL_NONE
  };

  eglDisplayHandle = eglGetCurrentDisplay();
  windowSurface = eglGetCurrentSurface(EGL_DRAW);
  baseContext = eglGetCurrentContext();

  if (eglDisplayHandle == EGL_NO_DISPLAY ||
      windowSurface == EGL_NO_SURFACE ||
      baseContext == EGL_NO_CONTEXT)
    failEGL("The example harness did not create an EGL OpenVG context");

  windowConfig = testEGLConfig();
  if (!windowConfig)
    failEGL("The example harness did not expose its EGL window config");

  pbufferConfig = chooseConfig(EGL_PBUFFER_BIT);
  if (!eglGetConfigAttrib(eglDisplayHandle, windowConfig,
                          EGL_ALPHA_MASK_SIZE, &alphaMaskSize) ||
      alphaMaskSize < 8)
    failEGL("The EGL OpenVG config does not expose an 8-bit alpha mask");

  sharedContext = eglCreateContext(eglDisplayHandle,
                                   windowConfig,
                                   baseContext,
                                   NULL);
  if (sharedContext == EGL_NO_CONTEXT)
    failEGL("Could not create a shared OpenVG EGL context");

  pbufferContext = eglCreateContext(eglDisplayHandle,
                                    pbufferConfig,
                                    baseContext,
                                    NULL);
  if (pbufferContext == EGL_NO_CONTEXT)
    failEGL("Could not create a shared OpenVG pbuffer context");

  scratchSurface = eglCreatePbufferSurface(eglDisplayHandle,
                                           pbufferConfig,
                                           scratchAttribs);
  if (scratchSurface == EGL_NO_SURFACE)
    failEGL("Could not create an OpenVG setup pbuffer");

  if (!eglMakeCurrent(eglDisplayHandle,
                      scratchSurface, scratchSurface,
                      pbufferContext))
    failEGL("Could not bind the shared OpenVG pbuffer context");

  fillPaint = vgCreatePaint();
  unitRect = createUnitRect();
  unitEllipse = createUnitEllipse();
  diamond = createDiamond();
  offscreenImage = vgCreateImage(VG_sRGBA_8888,
                                 IMAGE_WIDTH, IMAGE_HEIGHT,
                                 VG_IMAGE_QUALITY_BETTER);
  if (fillPaint == VG_INVALID_HANDLE ||
      offscreenImage == VG_INVALID_HANDLE)
    failVG("Could not create shared OpenVG resources");

  imageSurface =
    eglCreatePbufferFromClientBuffer(eglDisplayHandle,
                                     EGL_OPENVG_IMAGE,
                                     (EGLClientBuffer)offscreenImage,
                                     pbufferConfig,
                                     NULL);
  if (imageSurface == EGL_NO_SURFACE)
    failEGL("Could not create an OpenVG image-backed pbuffer");

  if (!eglMakeCurrent(eglDisplayHandle,
                      windowSurface, windowSurface,
                      baseContext))
    failEGL("Could not restore the window OpenVG context");

  printf("EGL feature example: shared OpenVG context, VGImage pbuffer, alpha mask %d\n",
         alphaMaskSize);
  fflush(stdout);
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 760, 480, "ShaderVG: EGL OpenVG Features");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createContent();
  testRun();

  return EXIT_SUCCESS;
}
