#include <EGL/egl.h>
#include <VG/openvg.h>

#include <X11/Xlib.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
  EGLDisplay display;
  EGLConfig config;
  EGLSurface surface;
  EGLContext context;
  int ownsDisplay;
} TestEGL;

typedef struct
{
  int status;
  EGLDisplay display;
  EGLConfig config;
  EGLSurface surface;
  EGLContext context;
  int operation;
  int workerId;
  int iterations;
} ThreadArgs;

typedef struct
{
  int status;
  EGLDisplay display;
  EGLConfig config;
  EGLContext context;
  VGPath path;
  VGImage glyphImage;
  VGImage patternImage;
  int destroyedPath;
  int destroyedGlyphImage;
  int destroyedPatternImage;
} DestroySharedArgs;

typedef struct
{
  int status;
  EGLDisplay display;
  EGLConfig config;
  EGLContext context;
  EGLSurface surface;
  VGImage image;
  int destroyedSurface;
  int destroyedImage;
} PbufferRaceArgs;

typedef struct
{
  int repeat;
  int sharedWorkers;
  int sharedIterations;
} ThreadTestOptions;

enum {
  THREAD_TEARDOWN_DESTROY_CONTEXT,
  THREAD_TEARDOWN_DESTROY_SURFACE,
  THREAD_TEARDOWN_TERMINATE_DISPLAY
};

enum {
  THREAD_IMAGE_SIZE = 8,
  THREAD_DEFAULT_REPEAT = 1,
  THREAD_DEFAULT_SHARED_WORKERS = 2,
  THREAD_DEFAULT_SHARED_ITERATIONS = 64,
  THREAD_MAX_REPEAT = 1000,
  THREAD_MAX_SHARED_WORKERS = 16,
  THREAD_MAX_SHARED_ITERATIONS = 100000
};

static int parse_positive_env(const char *name,
                              int defaultValue,
                              int maxValue,
                              int *out)
{
  const char *value = getenv(name);
  char *end = NULL;
  long parsed;

  if (!value || !*value) {
    *out = defaultValue;
    return 0;
  }

  errno = 0;
  parsed = strtol(value, &end, 10);
  if (errno == ERANGE ||
      end == value ||
      *end != '\0' ||
      parsed < 1 ||
      parsed > maxValue) {
    fprintf(stderr,
            "%s must be an integer from 1 to %d for test_thread_safety\n",
            name,
            maxValue);
    return 1;
  }

  *out = (int)parsed;
  return 0;
}

static int parse_thread_test_options(ThreadTestOptions *options)
{
  if (parse_positive_env("SHADERVG_THREAD_TEST_REPEAT",
                         THREAD_DEFAULT_REPEAT,
                         THREAD_MAX_REPEAT,
                         &options->repeat))
    return 1;

  if (parse_positive_env("SHADERVG_THREAD_TEST_SHARED_WORKERS",
                         THREAD_DEFAULT_SHARED_WORKERS,
                         THREAD_MAX_SHARED_WORKERS,
                         &options->sharedWorkers))
    return 1;

  if (parse_positive_env("SHADERVG_THREAD_TEST_SHARED_ITERATIONS",
                         THREAD_DEFAULT_SHARED_ITERATIONS,
                         THREAD_MAX_SHARED_ITERATIONS,
                         &options->sharedIterations))
    return 1;

  return 0;
}

static int fail_egl(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
  return 1;
}

static int expect_egl_error(const char *message, EGLint expected)
{
  EGLint error = eglGetError();
  if (error == expected)
    return 0;

  fprintf(stderr, "%s (expected EGL error 0x%04x, got 0x%04x)\n",
          message, expected, error);
  return 1;
}

static int fail_vg(const char *message)
{
  fprintf(stderr, "%s (VG error 0x%04x)\n", message, vgGetError());
  return 1;
}

static int expect_vg_no_error(const char *message)
{
  VGErrorCode error = vgGetError();
  if (error == VG_NO_ERROR)
    return 0;

  fprintf(stderr, "%s (VG error 0x%04x)\n", message, error);
  return 1;
}

static int init_shared_egl(TestEGL *state,
                           EGLDisplay display,
                           EGLConfig config,
                           EGLContext shareContext);

static void select_test_platform(void)
{
#if !defined(_WIN32)
  if (!getenv("EGL_PLATFORM"))
    setenv("EGL_PLATFORM", "surfaceless", 0);
#endif
}

static int init_egl(TestEGL *state, EGLContext shareContext)
{
  EGLint major, minor;
  EGLint count = 0;
  const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };

  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  state->ownsDisplay = 1;
  if (state->display == EGL_NO_DISPLAY)
    return fail_egl("thread test could not get EGL display");

  if (!eglInitialize(state->display, &major, &minor))
    return fail_egl("thread test could not initialize EGL");

  if (!eglBindAPI(EGL_OPENVG_API))
    return fail_egl("thread test could not bind OpenVG API");

  if (!eglChooseConfig(state->display, configAttribs,
                       &state->config, 1, &count) ||
      count <= 0)
    return fail_egl("thread test could not choose EGL config");

  return init_shared_egl(state, state->display, state->config, shareContext);
}

static int init_shared_egl(TestEGL *state,
                           EGLDisplay display,
                           EGLConfig config,
                           EGLContext shareContext)
{
  const EGLint surfaceAttribs[] = {
    EGL_WIDTH, 32,
    EGL_HEIGHT, 32,
    EGL_NONE
  };
  int ownsDisplay = state->ownsDisplay;

  if (!eglBindAPI(EGL_OPENVG_API))
    return fail_egl("thread test could not bind OpenVG API");

  state->display = display;
  state->config = config;
  state->ownsDisplay = ownsDisplay;
  state->surface = eglCreatePbufferSurface(state->display,
                                           state->config,
                                           surfaceAttribs);
  if (state->surface == EGL_NO_SURFACE)
    return fail_egl("thread test could not create EGL pbuffer");

  state->context = eglCreateContext(state->display,
                                    state->config,
                                    shareContext,
                                    NULL);
  if (state->context == EGL_NO_CONTEXT)
    return fail_egl("thread test could not create EGL context");

  if (!eglMakeCurrent(state->display,
                      state->surface,
                      state->surface,
                      state->context))
    return fail_egl("thread test could not make context current");

  return 0;
}

static int init_display_config(TestEGL *state)
{
  EGLint major, minor;
  EGLint count = 0;
  const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };

  state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  state->ownsDisplay = 1;
  if (state->display == EGL_NO_DISPLAY)
    return fail_egl("thread test could not get EGL display");

  if (!eglInitialize(state->display, &major, &minor))
    return fail_egl("thread test could not initialize EGL");

  if (!eglBindAPI(EGL_OPENVG_API))
    return fail_egl("thread test could not bind OpenVG API");

  if (!eglChooseConfig(state->display, configAttribs,
                       &state->config, 1, &count) ||
      count <= 0)
    return fail_egl("thread test could not choose EGL config");

  return 0;
}

static int cleanup_egl(TestEGL *state)
{
  int status = 0;

  if (!eglMakeCurrent(state->display,
                      EGL_NO_SURFACE,
                      EGL_NO_SURFACE,
                      EGL_NO_CONTEXT))
    status = fail_egl("thread test could not clear current context");

  if (state->context != EGL_NO_CONTEXT &&
      !eglDestroyContext(state->display, state->context))
    status = fail_egl("thread test could not destroy context");

  if (state->surface != EGL_NO_SURFACE &&
      !eglDestroySurface(state->display, state->surface))
    status = fail_egl("thread test could not destroy surface");

  if (state->ownsDisplay &&
      state->display != EGL_NO_DISPLAY &&
      !eglTerminate(state->display))
    status = fail_egl("thread test could not terminate display");

  return status;
}

static VGPath create_rect_path(void)
{
  static const VGubyte commands[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  static const VGfloat coords[] = {
    4.0f, 4.0f,
    28.0f, 4.0f,
    28.0f, 28.0f,
    4.0f, 28.0f
  };
  VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD,
                             VG_PATH_DATATYPE_F,
                             1.0f,
                             0.0f,
                             5,
                             8,
                             VG_PATH_CAPABILITY_APPEND_TO);

  if (path == VG_INVALID_HANDLE)
    return path;

  vgAppendPathData(path, 5, commands, coords);
  if (vgGetError() != VG_NO_ERROR) {
    vgDestroyPath(path);
    return VG_INVALID_HANDLE;
  }

  return path;
}

static int reset_test_state(void)
{
  VGfloat origin[2] = { 0.0f, 0.0f };

  vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_FILL_PAINT_TO_USER);
  vgLoadIdentity();
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_STROKE_PAINT_TO_USER);
  vgLoadIdentity();
  vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
  vgLoadIdentity();
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgSetfv(VG_GLYPH_ORIGIN, 2, origin);

  return expect_vg_no_error("thread test could not reset OpenVG state");
}

static VGImage create_colored_image(int workerId, int iteration)
{
  VGubyte pixels[THREAD_IMAGE_SIZE * THREAD_IMAGE_SIZE * 4];
  VGImage image;
  int x;
  int y;

  for (y = 0; y < THREAD_IMAGE_SIZE; ++y) {
    for (x = 0; x < THREAD_IMAGE_SIZE; ++x) {
      int offset = (y * THREAD_IMAGE_SIZE + x) * 4;
      pixels[offset + 0] = (VGubyte)(40 + (workerId * 53 + x * 17) % 180);
      pixels[offset + 1] = (VGubyte)(30 + (iteration * 29 + y * 19) % 190);
      pixels[offset + 2] = (VGubyte)(60 + (workerId * 31 + iteration * 7) % 160);
      pixels[offset + 3] = 255;
    }
  }

  image = vgCreateImage(VG_sRGBA_8888,
                        THREAD_IMAGE_SIZE,
                        THREAD_IMAGE_SIZE,
                        VG_IMAGE_QUALITY_BETTER);
  if (image == VG_INVALID_HANDLE)
    return image;

  vgImageSubData(image,
                 pixels,
                 THREAD_IMAGE_SIZE * 4,
                 VG_sRGBA_8888,
                 0,
                 0,
                 THREAD_IMAGE_SIZE,
                 THREAD_IMAGE_SIZE);
  return image;
}

static int setup_retained_resources(VGPath path,
                                    VGImage glyphImage,
                                    VGImage patternImage,
                                    VGPaint paint,
                                    VGFont font)
{
  VGfloat glyphOrigin[2] = { 0.0f, 0.0f };
  VGfloat escapement[2] = { 10.0f, 0.0f };

  vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_PATTERN);
  vgPaintPattern(paint, patternImage);
  vgSetGlyphToPath(font, 1, path, VG_FALSE, glyphOrigin, escapement);
  vgSetGlyphToImage(font, 2, glyphImage, glyphOrigin, escapement);

  return expect_vg_no_error("thread test could not attach retained resources");
}

static int draw_retained_resources(VGPaint paint, VGFont font)
{
  VGuint glyphs[2] = { 1, 2 };

  if (reset_test_state())
    return 1;

  vgSetPaint(paint, VG_FILL_PATH);
  vgDrawGlyph(font, 1, VG_FILL_PATH, VG_FALSE);
  vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
  vgDrawGlyph(font, 2, VG_FILL_PATH, VG_FALSE);

  if (expect_vg_no_error("thread test could not draw retained resources"))
    return 1;

  if (reset_test_state())
    return 1;

  vgSetPaint(paint, VG_FILL_PATH);
  vgDrawGlyphs(font, 2, glyphs, NULL, NULL, VG_FILL_PATH, VG_FALSE);
  vgFinish();

  return expect_vg_no_error("thread test could not draw retained glyph run");
}

static int destroy_public_retained_handles(VGPath *path,
                                           VGImage *glyphImage,
                                           VGImage *patternImage)
{
  if (*path != VG_INVALID_HANDLE) {
    vgDestroyPath(*path);
    if (expect_vg_no_error("thread test could not destroy retained path handle"))
      return 1;
    *path = VG_INVALID_HANDLE;
  }

  if (*glyphImage != VG_INVALID_HANDLE) {
    vgDestroyImage(*glyphImage);
    if (expect_vg_no_error("thread test could not destroy retained glyph image handle"))
      return 1;
    *glyphImage = VG_INVALID_HANDLE;
  }

  if (*patternImage != VG_INVALID_HANDLE) {
    vgDestroyImage(*patternImage);
    if (expect_vg_no_error("thread test could not destroy retained pattern image handle"))
      return 1;
    *patternImage = VG_INVALID_HANDLE;
  }

  return 0;
}

static int run_shared_resource_iteration(int workerId, int iteration)
{
  VGPath path = VG_INVALID_HANDLE;
  VGImage glyphImage = VG_INVALID_HANDLE;
  VGImage patternImage = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGFont font = VG_INVALID_HANDLE;
  int status = 0;

  path = create_rect_path();
  glyphImage = create_colored_image(workerId, iteration);
  patternImage = create_colored_image(workerId, iteration + 1000);
  paint = vgCreatePaint();
  font = vgCreateFont(2);

  if (path == VG_INVALID_HANDLE ||
      glyphImage == VG_INVALID_HANDLE ||
      patternImage == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      font == VG_INVALID_HANDLE) {
    status = fail_vg("thread test could not create shared retained resources");
  } else if (expect_vg_no_error("thread test could not upload shared images")) {
    status = 1;
  }

  if (!status &&
      setup_retained_resources(path, glyphImage, patternImage, paint, font))
    status = 1;

  if (!status &&
      destroy_public_retained_handles(&path, &glyphImage, &patternImage))
    status = 1;

  if (!status && draw_retained_resources(paint, font))
    status = 1;

  if (!status) {
    vgClearGlyph(font, 1);
    vgClearGlyph(font, 2);
    if (expect_vg_no_error("thread test could not clear retained glyphs"))
      status = 1;
  }

  if (font != VG_INVALID_HANDLE) {
    vgDestroyFont(font);
    if (expect_vg_no_error("thread test could not destroy retained font"))
      status = 1;
  }

  if (paint != VG_INVALID_HANDLE) {
    vgDestroyPaint(paint);
    if (expect_vg_no_error("thread test could not destroy retained paint"))
      status = 1;
  }

  if (path != VG_INVALID_HANDLE) {
    vgDestroyPath(path);
    if (expect_vg_no_error("thread test could not clean up shared path"))
      status = 1;
  }

  if (glyphImage != VG_INVALID_HANDLE) {
    vgDestroyImage(glyphImage);
    if (expect_vg_no_error("thread test could not clean up shared glyph image"))
      status = 1;
  }

  if (patternImage != VG_INVALID_HANDLE) {
    vgDestroyImage(patternImage);
    if (expect_vg_no_error("thread test could not clean up shared pattern image"))
      status = 1;
  }

  return status;
}

static int exercise_resources(int iterations)
{
  int i;

  for (i = 0; i < iterations; ++i) {
    VGPath path = create_rect_path();
    VGPaint paint = vgCreatePaint();

    if (path == VG_INVALID_HANDLE || paint == VG_INVALID_HANDLE)
      return fail_vg("thread test could not create path/paint");

    vgSetColor(paint, 0x3366ccffu);
    vgSetPaint(paint, VG_FILL_PATH);
    vgDrawPath(path, VG_FILL_PATH);
    vgFinish();

    if (vgGetError() != VG_NO_ERROR)
      return fail_vg("thread test draw failed");

    vgDestroyPaint(paint);
    vgDestroyPath(path);

    if (vgGetError() != VG_NO_ERROR)
      return fail_vg("thread test destroy failed");
  }

  return 0;
}

static void *first_egl_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;

  (void)eglGetProcAddress("eglCreateImageKHR");
  args->status = 0;
  return NULL;
}

static void *independent_context_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  state.ownsDisplay = 0;
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 EGL_NO_CONTEXT);
  if (!args->status)
    args->status = exercise_resources(20);
  if (cleanup_egl(&state))
    args->status = 1;

  return NULL;
}

static void *shared_context_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  if (!args->status)
    args->status = exercise_resources(50);
  if (cleanup_egl(&state))
    args->status = 1;

  return NULL;
}

static void *shared_resource_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  int i;

  state.ownsDisplay = 0;
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  if (!args->status) {
    for (i = 0; i < args->iterations; ++i) {
      if (run_shared_resource_iteration(args->workerId, i)) {
        args->status = 1;
        break;
      }
    }
  }

  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  return NULL;
}

static void *destroy_shared_handles_worker(void *arg)
{
  DestroySharedArgs *args = (DestroySharedArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  state.ownsDisplay = 0;
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  if (!args->status) {
    if (args->path != VG_INVALID_HANDLE) {
      vgDestroyPath(args->path);
      if (expect_vg_no_error("thread test could not destroy shared path in peer"))
        args->status = 1;
      else
        args->destroyedPath = 1;
    }

    if (!args->status && args->glyphImage != VG_INVALID_HANDLE) {
      vgDestroyImage(args->glyphImage);
      if (expect_vg_no_error("thread test could not destroy shared glyph image in peer"))
        args->status = 1;
      else
        args->destroyedGlyphImage = 1;
    }

    if (!args->status && args->patternImage != VG_INVALID_HANDLE) {
      vgDestroyImage(args->patternImage);
      if (expect_vg_no_error("thread test could not destroy shared pattern image in peer"))
        args->status = 1;
      else
        args->destroyedPatternImage = 1;
    }
  }

  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  return NULL;
}

static void *destroy_pbuffer_image_worker(void *arg)
{
  PbufferRaceArgs *args = (PbufferRaceArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  state.ownsDisplay = 0;
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  if (!args->status) {
    vgDestroyImage(args->image);
    if (expect_vg_no_error("thread test could not destroy image pbuffer source"))
      args->status = 1;
    else
      args->destroyedImage = 1;
  }

  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  return NULL;
}

static void *destroy_image_pbuffer_surface_worker(void *arg)
{
  PbufferRaceArgs *args = (PbufferRaceArgs*)arg;

  if (!eglDestroySurface(args->display, args->surface)) {
    args->status = fail_egl("thread test could not destroy image pbuffer surface");
    return NULL;
  }

  args->destroyedSurface = 1;
  args->status = 0;
  return NULL;
}

static void *make_current_conflict_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;

  if (eglMakeCurrent(args->display,
                     args->surface,
                     args->surface,
                     args->context)) {
    args->status = 1;
    fprintf(stderr, "EGL allowed a context current on another thread\n");
    return NULL;
  }

  args->status = (eglGetError() == EGL_BAD_ACCESS) ? 0 : 1;
  if (args->status)
    fprintf(stderr, "EGL reported the wrong cross-thread current error\n");
  return NULL;
}

static void *teardown_conflict_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  const char *operation = "teardown";
  EGLBoolean result = EGL_FALSE;

  switch (args->operation) {
  case THREAD_TEARDOWN_DESTROY_CONTEXT:
    operation = "context destruction";
    result = eglDestroyContext(args->display, args->context);
    break;
  case THREAD_TEARDOWN_DESTROY_SURFACE:
    operation = "surface destruction";
    result = eglDestroySurface(args->display, args->surface);
    break;
  case THREAD_TEARDOWN_TERMINATE_DISPLAY:
    operation = "display termination";
    result = eglTerminate(args->display);
    break;
  default:
    fprintf(stderr, "thread test received unknown teardown operation\n");
    args->status = 1;
    return NULL;
  }

  if (result) {
    fprintf(stderr, "EGL allowed cross-thread %s of current objects\n",
            operation);
    args->status = 1;
    return NULL;
  }

  args->status = expect_egl_error("EGL reported wrong cross-thread teardown error",
                                  EGL_BAD_ACCESS);
  return NULL;
}

static int run_concurrent_first_egl(void)
{
  enum { THREAD_COUNT = 4 };
  pthread_t threads[THREAD_COUNT];
  ThreadArgs args[THREAD_COUNT];
  int i;
  int status = 0;

  for (i = 0; i < THREAD_COUNT; ++i) {
    args[i].status = 1;
    if (pthread_create(&threads[i], NULL, first_egl_worker, &args[i]) != 0)
      return 1;
  }

  for (i = 0; i < THREAD_COUNT; ++i) {
    pthread_join(threads[i], NULL);
    if (args[i].status)
      status = 1;
  }

  return status;
}

static int warm_up_egl(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  if (init_display_config(&state))
    return 1;

  if (!eglTerminate(state.display))
    return fail_egl("thread test could not terminate warm-up display");

  return 0;
}

static int run_independent_contexts(void)
{
  enum { THREAD_COUNT = 2 };
  TestEGL displayState = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  pthread_t threads[THREAD_COUNT];
  ThreadArgs args[THREAD_COUNT];
  int i;
  int status = 0;

  if (init_display_config(&displayState))
    return 1;

  for (i = 0; i < THREAD_COUNT; ++i) {
    args[i].status = 1;
    args[i].display = displayState.display;
    args[i].config = displayState.config;
    if (pthread_create(&threads[i], NULL,
                       independent_context_worker, &args[i]) != 0)
      return 1;
  }

  for (i = 0; i < THREAD_COUNT; ++i) {
    pthread_join(threads[i], NULL);
    if (args[i].status)
      status = 1;
  }

  if (!eglTerminate(displayState.display))
    status = fail_egl("thread test could not terminate independent display");

  return status;
}

static int run_shared_context_stress(void)
{
  TestEGL base = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  ThreadArgs args;
  pthread_t thread;
  int status;

  if (init_egl(&base, EGL_NO_CONTEXT))
    return 1;

  args.status = 1;
  args.display = base.display;
  args.config = base.config;
  args.context = base.context;
  if (pthread_create(&thread, NULL, shared_context_worker, &args) != 0)
    return 1;

  status = exercise_resources(50);
  pthread_join(thread, NULL);
  if (args.status)
    status = 1;

  if (cleanup_egl(&base))
    status = 1;

  return status;
}

static int run_shared_resource_churn(const ThreadTestOptions *options)
{
  TestEGL base = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  pthread_t *threads = NULL;
  ThreadArgs *args = NULL;
  int created = 0;
  int i;
  int status = 0;

  threads = (pthread_t*)calloc((size_t)options->sharedWorkers,
                               sizeof(pthread_t));
  args = (ThreadArgs*)calloc((size_t)options->sharedWorkers,
                             sizeof(ThreadArgs));
  if (!threads || !args) {
    fprintf(stderr, "thread test could not allocate shared resource workers\n");
    free(threads);
    free(args);
    return 1;
  }

  if (init_egl(&base, EGL_NO_CONTEXT)) {
    free(threads);
    free(args);
    return 1;
  }

  for (i = 0; !status && i < options->sharedWorkers; ++i) {
    args[i].status = 1;
    args[i].display = base.display;
    args[i].config = base.config;
    args[i].context = base.context;
    args[i].workerId = i + 1;
    args[i].iterations = options->sharedIterations;
    if (pthread_create(&threads[i], NULL,
                       shared_resource_churn_worker, &args[i]) != 0) {
      fprintf(stderr, "thread test could not create shared resource worker\n");
      status = 1;
      break;
    }
    ++created;
  }

  for (i = 0; i < created; ++i) {
    pthread_join(threads[i], NULL);
    if (args[i].status)
      status = 1;
  }

  if (cleanup_egl(&base))
    status = 1;

  free(threads);
  free(args);
  return status;
}

static int run_cross_thread_retained_resource_lifetime(void)
{
  TestEGL base = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  DestroySharedArgs args;
  pthread_t thread;
  VGPath path = VG_INVALID_HANDLE;
  VGImage glyphImage = VG_INVALID_HANDLE;
  VGImage patternImage = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGFont font = VG_INVALID_HANDLE;
  int status = 0;

  if (init_egl(&base, EGL_NO_CONTEXT))
    return 1;

  path = create_rect_path();
  glyphImage = create_colored_image(7, 1);
  patternImage = create_colored_image(7, 2);
  paint = vgCreatePaint();
  font = vgCreateFont(2);

  if (path == VG_INVALID_HANDLE ||
      glyphImage == VG_INVALID_HANDLE ||
      patternImage == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      font == VG_INVALID_HANDLE) {
    status = fail_vg("thread test could not create retained peer resources");
  } else if (expect_vg_no_error("thread test could not upload peer images")) {
    status = 1;
  }

  if (!status &&
      setup_retained_resources(path, glyphImage, patternImage, paint, font))
    status = 1;

  if (!status) {
    args.status = 1;
    args.display = base.display;
    args.config = base.config;
    args.context = base.context;
    args.path = path;
    args.glyphImage = glyphImage;
    args.patternImage = patternImage;
    args.destroyedPath = 0;
    args.destroyedGlyphImage = 0;
    args.destroyedPatternImage = 0;

    if (pthread_create(&thread, NULL,
                       destroy_shared_handles_worker, &args) != 0) {
      fprintf(stderr, "thread test could not create shared destroy worker\n");
      status = 1;
    } else {
      pthread_join(thread, NULL);
      if (args.destroyedPath)
        path = VG_INVALID_HANDLE;
      if (args.destroyedGlyphImage)
        glyphImage = VG_INVALID_HANDLE;
      if (args.destroyedPatternImage)
        patternImage = VG_INVALID_HANDLE;
      if (args.status)
        status = 1;
    }
  }

  if (!status &&
      (path != VG_INVALID_HANDLE ||
       glyphImage != VG_INVALID_HANDLE ||
       patternImage != VG_INVALID_HANDLE)) {
    fprintf(stderr, "thread test did not destroy all shared public handles\n");
    status = 1;
  }

  if (!status && draw_retained_resources(paint, font))
    status = 1;

  if (font != VG_INVALID_HANDLE) {
    vgDestroyFont(font);
    if (expect_vg_no_error("thread test could not destroy peer retained font"))
      status = 1;
  }

  if (paint != VG_INVALID_HANDLE) {
    vgDestroyPaint(paint);
    if (expect_vg_no_error("thread test could not destroy peer retained paint"))
      status = 1;
  }

  if (path != VG_INVALID_HANDLE) {
    vgDestroyPath(path);
    if (expect_vg_no_error("thread test could not clean up peer path"))
      status = 1;
  }

  if (glyphImage != VG_INVALID_HANDLE) {
    vgDestroyImage(glyphImage);
    if (expect_vg_no_error("thread test could not clean up peer glyph image"))
      status = 1;
  }

  if (patternImage != VG_INVALID_HANDLE) {
    vgDestroyImage(patternImage);
    if (expect_vg_no_error("thread test could not clean up peer pattern image"))
      status = 1;
  }

  if (cleanup_egl(&base))
    status = 1;

  return status;
}

static int run_image_pbuffer_ref_race(void)
{
  TestEGL base = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  PbufferRaceArgs imageArgs;
  PbufferRaceArgs surfaceArgs;
  pthread_t imageThread;
  pthread_t surfaceThread;
  VGImage image = VG_INVALID_HANDLE;
  EGLSurface imageSurface = EGL_NO_SURFACE;
  int imageThreadCreated = 0;
  int surfaceThreadCreated = 0;
  int status = 0;

  if (init_egl(&base, EGL_NO_CONTEXT))
    return 1;

  image = create_colored_image(9, 3);
  if (image == VG_INVALID_HANDLE ||
      expect_vg_no_error("thread test could not create image pbuffer source")) {
    status = 1;
  }

  if (!status) {
    imageSurface = eglCreatePbufferFromClientBuffer(base.display,
                                                   EGL_OPENVG_IMAGE,
                                                   (EGLClientBuffer)(uintptr_t)image,
                                                   base.config,
                                                   NULL);
    if (imageSurface == EGL_NO_SURFACE)
      status = fail_egl("thread test could not create image pbuffer surface");
  }

  imageArgs.status = 1;
  imageArgs.display = base.display;
  imageArgs.config = base.config;
  imageArgs.context = base.context;
  imageArgs.surface = imageSurface;
  imageArgs.image = image;
  imageArgs.destroyedSurface = 0;
  imageArgs.destroyedImage = 0;

  surfaceArgs = imageArgs;

  if (!status) {
    if (pthread_create(&imageThread, NULL,
                       destroy_pbuffer_image_worker, &imageArgs) != 0) {
      fprintf(stderr, "thread test could not create image destroy worker\n");
      status = 1;
    } else {
      imageThreadCreated = 1;
    }
  }

  if (!status) {
    if (pthread_create(&surfaceThread, NULL,
                       destroy_image_pbuffer_surface_worker,
                       &surfaceArgs) != 0) {
      fprintf(stderr, "thread test could not create image pbuffer destroy worker\n");
      status = 1;
    } else {
      surfaceThreadCreated = 1;
    }
  }

  if (imageThreadCreated) {
    pthread_join(imageThread, NULL);
    if (imageArgs.destroyedImage)
      image = VG_INVALID_HANDLE;
    if (imageArgs.status)
      status = 1;
  }

  if (surfaceThreadCreated) {
    pthread_join(surfaceThread, NULL);
    if (surfaceArgs.destroyedSurface)
      imageSurface = EGL_NO_SURFACE;
    if (surfaceArgs.status)
      status = 1;
  }

  if (imageSurface != EGL_NO_SURFACE &&
      !eglDestroySurface(base.display, imageSurface))
    status = fail_egl("thread test could not clean up image pbuffer surface");

  if (image != VG_INVALID_HANDLE) {
    vgDestroyImage(image);
    if (expect_vg_no_error("thread test could not clean up image pbuffer source"))
      status = 1;
  }

  if (cleanup_egl(&base))
    status = 1;

  return status;
}

static int run_make_current_conflict(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  ThreadArgs args;
  pthread_t thread;
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  args.status = 1;
  args.display = state.display;
  args.surface = state.surface;
  args.context = state.context;

  if (pthread_create(&thread, NULL,
                     make_current_conflict_worker, &args) != 0)
    status = 1;
  else {
    pthread_join(thread, NULL);
    status = args.status;
  }

  if (cleanup_egl(&state))
    status = 1;

  return status;
}

static int run_same_thread_deferred_destroy(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  EGLSurface surface;
  EGLContext context;
  EGLBoolean contextDestroyed = EGL_FALSE;
  EGLBoolean surfaceDestroyed = EGL_FALSE;
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  surface = state.surface;
  context = state.context;

  if (!eglDestroyContext(state.display, context))
    status = fail_egl("thread test could not defer current context destruction");
  else
    contextDestroyed = EGL_TRUE;

  if (!status) {
    if (!eglDestroySurface(state.display, surface))
      status = fail_egl("thread test could not defer current surface destruction");
    else
      surfaceDestroyed = EGL_TRUE;
  }

  if (!status && eglGetCurrentContext() != context) {
    fprintf(stderr, "EGL cleared current context before unbind\n");
    status = 1;
  }

  if (!status && eglGetCurrentSurface(EGL_DRAW) != surface) {
    fprintf(stderr, "EGL cleared current draw surface before unbind\n");
    status = 1;
  }

  if (!status && eglGetCurrentSurface(EGL_READ) != surface) {
    fprintf(stderr, "EGL cleared current read surface before unbind\n");
    status = 1;
  }

  if (!eglMakeCurrent(state.display,
                      EGL_NO_SURFACE,
                      EGL_NO_SURFACE,
                      EGL_NO_CONTEXT)) {
    status = fail_egl("thread test could not clear deferred current objects");
  }

  if (contextDestroyed)
    state.context = EGL_NO_CONTEXT;
  if (surfaceDestroyed)
    state.surface = EGL_NO_SURFACE;

  if (state.context != EGL_NO_CONTEXT &&
      !eglDestroyContext(state.display, state.context))
    status = fail_egl("thread test could not destroy deferred-test context");

  if (state.surface != EGL_NO_SURFACE &&
      !eglDestroySurface(state.display, state.surface))
    status = fail_egl("thread test could not destroy deferred-test surface");

  if (!eglTerminate(state.display))
    status = fail_egl("thread test could not terminate deferred-test display");

  return status;
}

static int run_release_thread_clears_current(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  if (!eglReleaseThread())
    status = fail_egl("thread test could not release current thread");

  if (!status && eglGetCurrentContext() != EGL_NO_CONTEXT) {
    fprintf(stderr, "eglReleaseThread left a current context\n");
    status = 1;
  }

  if (!status && eglGetCurrentSurface(EGL_DRAW) != EGL_NO_SURFACE) {
    fprintf(stderr, "eglReleaseThread left a current draw surface\n");
    status = 1;
  }

  if (!status && eglGetCurrentSurface(EGL_READ) != EGL_NO_SURFACE) {
    fprintf(stderr, "eglReleaseThread left a current read surface\n");
    status = 1;
  }

  if (!status && eglGetCurrentDisplay() != EGL_NO_DISPLAY) {
    fprintf(stderr, "eglReleaseThread left a current display\n");
    status = 1;
  }

  if (state.context != EGL_NO_CONTEXT &&
      !eglDestroyContext(state.display, state.context))
    status = fail_egl("thread test could not destroy released context");

  if (state.surface != EGL_NO_SURFACE &&
      !eglDestroySurface(state.display, state.surface))
    status = fail_egl("thread test could not destroy released surface");

  if (!eglTerminate(state.display))
    status = fail_egl("thread test could not terminate released display");

  return status;
}

static int run_release_thread_finalizes_deferred_destroy(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  EGLBoolean contextDestroyed = EGL_FALSE;
  EGLBoolean surfaceDestroyed = EGL_FALSE;
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  if (!eglDestroyContext(state.display, state.context))
    status = fail_egl("thread test could not defer context before release");
  else
    contextDestroyed = EGL_TRUE;

  if (!status) {
    if (!eglDestroySurface(state.display, state.surface))
      status = fail_egl("thread test could not defer surface before release");
    else
      surfaceDestroyed = EGL_TRUE;
  }

  if (!eglReleaseThread())
    status = fail_egl("thread test could not release deferred objects");

  if (contextDestroyed)
    state.context = EGL_NO_CONTEXT;
  if (surfaceDestroyed)
    state.surface = EGL_NO_SURFACE;

  if (!status && eglGetCurrentContext() != EGL_NO_CONTEXT) {
    fprintf(stderr, "eglReleaseThread left a deferred context current\n");
    status = 1;
  }

  if (!status && eglGetCurrentSurface(EGL_DRAW) != EGL_NO_SURFACE) {
    fprintf(stderr, "eglReleaseThread left a deferred draw surface current\n");
    status = 1;
  }

  if (!status && eglGetCurrentSurface(EGL_READ) != EGL_NO_SURFACE) {
    fprintf(stderr, "eglReleaseThread left a deferred read surface current\n");
    status = 1;
  }

  if (state.context != EGL_NO_CONTEXT &&
      !eglDestroyContext(state.display, state.context))
    status = fail_egl("thread test could not destroy release-test context");

  if (state.surface != EGL_NO_SURFACE &&
      !eglDestroySurface(state.display, state.surface))
    status = fail_egl("thread test could not destroy release-test surface");

  if (!eglTerminate(state.display))
    status = fail_egl("thread test could not terminate release-test display");

  return status;
}

static int run_teardown_conflict_case(TestEGL *state, int operation)
{
  ThreadArgs args;
  pthread_t thread;

  args.status = 1;
  args.display = state->display;
  args.surface = state->surface;
  args.context = state->context;
  args.operation = operation;

  if (pthread_create(&thread, NULL,
                     teardown_conflict_worker, &args) != 0) {
    fprintf(stderr, "thread test could not create teardown conflict thread\n");
    return 1;
  }

  pthread_join(thread, NULL);
  return args.status;
}

static int run_cross_thread_teardown_conflicts(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  if (run_teardown_conflict_case(&state, THREAD_TEARDOWN_DESTROY_CONTEXT))
    status = 1;

  if (run_teardown_conflict_case(&state, THREAD_TEARDOWN_DESTROY_SURFACE))
    status = 1;

  if (run_teardown_conflict_case(&state, THREAD_TEARDOWN_TERMINATE_DISPLAY))
    status = 1;

  if (cleanup_egl(&state))
    status = 1;

  return status;
}

static int run_display_canonicalization(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  EGLDisplay duplicate;
  EGLint major, minor;
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  duplicate = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (duplicate == EGL_NO_DISPLAY)
    status = fail_egl("thread test could not get duplicate display");
  else if (duplicate != state.display) {
    fprintf(stderr, "EGL display handles were not canonicalized\n");
    status = 1;
  }

  if (!status && eglTerminate(duplicate)) {
    fprintf(stderr, "EGL terminated a display with a current context\n");
    status = 1;
  } else if (!status &&
             expect_egl_error("EGL reported wrong current-display terminate error",
                              EGL_BAD_ACCESS)) {
    status = 1;
  }

  if (cleanup_egl(&state))
    status = 1;

  if (!status) {
    duplicate = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (duplicate == EGL_NO_DISPLAY) {
      status = fail_egl("thread test could not reacquire display");
    } else if (!eglInitialize(duplicate, &major, &minor)) {
      status = fail_egl("thread test could not reinitialize display");
    } else if (!eglTerminate(duplicate)) {
      status = fail_egl("thread test could not terminate reinitialized display");
    }
  }

  return status;
}

static int run_display_stale_generation(void)
{
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  EGLSurface staleSurface;
  EGLContext staleContext;
  EGLint major, minor;
  int status = 0;

  if (init_egl(&state, EGL_NO_CONTEXT))
    return 1;

  staleSurface = state.surface;
  staleContext = state.context;

  if (!eglMakeCurrent(state.display,
                      EGL_NO_SURFACE,
                      EGL_NO_SURFACE,
                      EGL_NO_CONTEXT))
    return fail_egl("thread test could not clear current context");

  if (!eglTerminate(state.display))
    return fail_egl("thread test could not terminate display with stale objects");

  if (!eglInitialize(state.display, &major, &minor))
    return fail_egl("thread test could not reinitialize stale-object display");

  if (eglMakeCurrent(state.display,
                     staleSurface,
                     staleSurface,
                     staleContext)) {
    fprintf(stderr, "EGL accepted stale context/surface handles after terminate\n");
    status = 1;
  } else if (expect_egl_error("EGL reported wrong stale surface current error",
                              EGL_BAD_SURFACE)) {
    status = 1;
  }

  if (!status && eglDestroyContext(state.display, staleContext)) {
    fprintf(stderr, "EGL destroyed a stale context handle\n");
    status = 1;
  } else if (!status &&
             expect_egl_error("EGL reported wrong stale context destroy error",
                              EGL_BAD_CONTEXT)) {
    status = 1;
  }

  if (!status && eglDestroySurface(state.display, staleSurface)) {
    fprintf(stderr, "EGL destroyed a stale surface handle\n");
    status = 1;
  } else if (!status &&
             expect_egl_error("EGL reported wrong stale surface destroy error",
                              EGL_BAD_SURFACE)) {
    status = 1;
  }

  if (!eglTerminate(state.display))
    status = fail_egl("thread test could not terminate stale-object display");

  return status;
}

static int run_thread_safety_suite(const ThreadTestOptions *options)
{
  if (run_concurrent_first_egl())
    return 1;
  if (run_display_canonicalization())
    return 1;
  if (run_display_stale_generation())
    return 1;
  if (run_same_thread_deferred_destroy())
    return 1;
  if (run_release_thread_clears_current())
    return 1;
  if (run_release_thread_finalizes_deferred_destroy())
    return 1;
  if (run_cross_thread_teardown_conflicts())
    return 1;
  if (warm_up_egl())
    return 1;
  if (run_independent_contexts())
    return 1;
  if (run_shared_context_stress())
    return 1;
  if (run_shared_resource_churn(options))
    return 1;
  if (run_cross_thread_retained_resource_lifetime())
    return 1;
  if (run_image_pbuffer_ref_race())
    return 1;
  if (run_make_current_conflict())
    return 1;

  return 0;
}

int main(void)
{
  ThreadTestOptions options;
  int i;

  XInitThreads();
  select_test_platform();

  if (parse_thread_test_options(&options))
    return EXIT_FAILURE;

  for (i = 0; i < options.repeat; ++i) {
    if (options.repeat > 1)
      printf("EGL/OpenVG thread safety iteration %d/%d\n",
             i + 1,
             options.repeat);

    if (run_thread_safety_suite(&options))
      return EXIT_FAILURE;
  }

  printf("EGL/OpenVG thread safety smoke test passed\n");
  return EXIT_SUCCESS;
}
