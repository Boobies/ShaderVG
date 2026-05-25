#include "test.h"

static VGPath vgPanel;
static VGPaint vgFill;
static VGPaint vgStroke;
static float angle = 0.0f;

static EGLDisplay eglDisplayHandle = EGL_NO_DISPLAY;
static EGLSurface windowSurface = EGL_NO_SURFACE;
static EGLConfig windowConfig = NULL;
static EGLContext openVGContext = EGL_NO_CONTEXT;
static EGLContext openGLContext = EGL_NO_CONTEXT;

static PFNGLUSEPROGRAMPROC useProgramProc = NULL;
static PFNGLACTIVETEXTUREPROC activeTextureProc = NULL;

static void failEGL(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
  exit(EXIT_FAILURE);
}

static const EGLint *openGLContextAttribs(void)
{
#if defined(EGL_CONTEXT_OPENGL_PROFILE_MASK) && defined(EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
  static const EGLint attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
    EGL_NONE
  };

  return attribs;
#else
  return NULL;
#endif
}

static void makeOpenGLCurrent(void)
{
  if (!eglBindAPI(EGL_OPENGL_API) ||
      !eglMakeCurrent(eglDisplayHandle,
                      windowSurface, windowSurface,
                      openGLContext))
    failEGL("Could not bind the OpenGL EGL context");
}

static void makeOpenVGCurrent(void)
{
  if (!eglBindAPI(EGL_OPENVG_API) ||
      !eglMakeCurrent(eglDisplayHandle,
                      windowSurface, windowSurface,
                      openVGContext))
    failEGL("Could not bind the OpenVG EGL context");
}

static void createOpenGLContext(void)
{
  eglDisplayHandle = eglGetCurrentDisplay();
  windowSurface = eglGetCurrentSurface(EGL_DRAW);
  openVGContext = eglGetCurrentContext();
  windowConfig = testEGLConfig();

  if (eglDisplayHandle == EGL_NO_DISPLAY ||
      windowSurface == EGL_NO_SURFACE ||
      openVGContext == EGL_NO_CONTEXT ||
      !windowConfig)
    failEGL("The example harness did not create an EGL OpenVG context");

  if (!eglBindAPI(EGL_OPENGL_API))
    failEGL("Could not bind the OpenGL EGL API");

  openGLContext = eglCreateContext(eglDisplayHandle,
                                   windowConfig,
                                   EGL_NO_CONTEXT,
                                   openGLContextAttribs());
  if (openGLContext == EGL_NO_CONTEXT)
    failEGL("Could not create the OpenGL EGL context");

  makeOpenVGCurrent();
}

static void loadGLFunctions(void)
{
  if (!useProgramProc)
    useProgramProc = (PFNGLUSEPROGRAMPROC)eglGetProcAddress("glUseProgram");
  if (!activeTextureProc)
    activeTextureProc = (PFNGLACTIVETEXTUREPROC)eglGetProcAddress("glActiveTexture");
}

static void resetGLState(void)
{
  loadGLFunctions();

  if (useProgramProc)
    useProgramProc(0);

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);

  if (activeTextureProc) {
    activeTextureProc(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    activeTextureProc(GL_TEXTURE0);
  }
  glDisable(GL_TEXTURE_2D);
}

static void setGLWindowProjection(void)
{
  glViewport(0, 0, testWidth(), testHeight());
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, (GLdouble)testWidth(), 0.0, (GLdouble)testHeight(), -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

static void drawOpenGLBackground(void)
{
  int x;
  int y;

  resetGLState();
  setGLWindowProjection();

  glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  glBegin(GL_QUADS);
  glColor3f(0.10f, 0.13f, 0.18f);
  glVertex2f(0.0f, 0.0f);
  glVertex2f((GLfloat)testWidth(), 0.0f);
  glColor3f(0.04f, 0.16f, 0.20f);
  glVertex2f((GLfloat)testWidth(), (GLfloat)testHeight());
  glVertex2f(0.0f, (GLfloat)testHeight());
  glEnd();

  glColor4f(1.0f, 1.0f, 1.0f, 0.18f);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBegin(GL_LINES);
  for (x = 0; x <= testWidth(); x += 48) {
    glVertex2f((GLfloat)x, 0.0f);
    glVertex2f((GLfloat)x, (GLfloat)testHeight());
  }
  for (y = 0; y <= testHeight(); y += 48) {
    glVertex2f(0.0f, (GLfloat)y);
    glVertex2f((GLfloat)testWidth(), (GLfloat)y);
  }
  glEnd();
  glDisable(GL_BLEND);
}

static void drawOpenVGPanel(void)
{
  VGfloat fillColor[] = {0.10f, 0.55f, 0.95f, 0.72f};
  VGfloat strokeColor[] = {1.0f, 1.0f, 1.0f, 0.95f};

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgTranslate(testWidth() * 0.5f, testHeight() * 0.5f);
  vgRotate(-angle * 0.4f);

  vgSetParameterfv(vgFill, VG_PAINT_COLOR, 4, fillColor);
  vgSetParameterfv(vgStroke, VG_PAINT_COLOR, 4, strokeColor);
  vgSetPaint(vgFill, VG_FILL_PATH);
  vgSetPaint(vgStroke, VG_STROKE_PATH);
  vgSetf(VG_STROKE_LINE_WIDTH, 8.0f);
  vgDrawPath(vgPanel, VG_FILL_PATH | VG_STROKE_PATH);
}

static void drawOpenGLForeground(void)
{
  GLfloat cx = testWidth() * 0.5f;
  GLfloat cy = testHeight() * 0.5f;

  resetGLState();
  setGLWindowProjection();

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glLineWidth(4.0f);
  glColor4f(1.0f, 0.82f, 0.18f, 0.95f);
  glBegin(GL_LINE_LOOP);
  glVertex2f(32.0f, 32.0f);
  glVertex2f((GLfloat)testWidth() - 32.0f, 32.0f);
  glVertex2f((GLfloat)testWidth() - 32.0f, (GLfloat)testHeight() - 32.0f);
  glVertex2f(32.0f, (GLfloat)testHeight() - 32.0f);
  glEnd();

  glPushMatrix();
  glTranslatef(cx, cy, 0.0f);
  glRotatef(angle, 0.0f, 0.0f, 1.0f);
  glColor4f(1.0f, 0.38f, 0.18f, 0.88f);
  glBegin(GL_TRIANGLES);
  glVertex2f(0.0f, 92.0f);
  glVertex2f(-22.0f, 34.0f);
  glVertex2f(22.0f, 34.0f);
  glEnd();
  glPopMatrix();

  glLineWidth(1.0f);
  glDisable(GL_BLEND);
}

static void display(float interval)
{
  angle += interval * 54.0f;
  if (angle > 360.0f)
    angle -= 360.0f;

  makeOpenGLCurrent();
  drawOpenGLBackground();
  glFlush();

  makeOpenVGCurrent();
  drawOpenVGPanel();
  vgFinish();

  makeOpenGLCurrent();
  drawOpenGLForeground();
  glFlush();

  makeOpenVGCurrent();
}

static void cleanup(void)
{
  if (eglDisplayHandle != EGL_NO_DISPLAY &&
      windowSurface != EGL_NO_SURFACE &&
      openVGContext != EGL_NO_CONTEXT)
    makeOpenVGCurrent();

  vgDestroyPath(vgPanel);
  vgDestroyPaint(vgFill);
  vgDestroyPaint(vgStroke);

  if (eglDisplayHandle != EGL_NO_DISPLAY &&
      openGLContext != EGL_NO_CONTEXT) {
    eglDestroyContext(eglDisplayHandle, openGLContext);
    openGLContext = EGL_NO_CONTEXT;
  }
}

static void createOpenVGContent(void)
{
  vgPanel = testCreatePath();
  vguRoundRect(vgPanel, -170.0f, -105.0f, 340.0f, 210.0f, 46.0f, 46.0f);

  vgFill = vgCreatePaint();
  vgStroke = vgCreatePaint();
}

int main(int argc, char **argv)
{
  testInit(argc, argv, 640, 480, "ShaderVG: EGL OpenGL OpenVG Interop");
  testCallback(TEST_CALLBACK_DISPLAY, (CallbackFunc)display);
  testCallback(TEST_CALLBACK_CLEANUP, (CallbackFunc)cleanup);

  createOpenGLContext();
  createOpenVGContent();
  testRun();

  return EXIT_SUCCESS;
}
