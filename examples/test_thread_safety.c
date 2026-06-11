#include <EGL/egl.h>
#include <VG/openvg.h>

#include <X11/Xlib.h>

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
} ThreadArgs;

static int fail_egl(const char *message)
{
  fprintf(stderr, "%s (EGL error 0x%04x)\n", message, eglGetError());
  return 1;
}

static int fail_vg(const char *message)
{
  fprintf(stderr, "%s (VG error 0x%04x)\n", message, vgGetError());
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

int main(void)
{
  XInitThreads();
  select_test_platform();

  if (run_concurrent_first_egl())
    return EXIT_FAILURE;
  if (warm_up_egl())
    return EXIT_FAILURE;
  if (run_independent_contexts())
    return EXIT_FAILURE;
  if (run_shared_context_stress())
    return EXIT_FAILURE;
  if (run_make_current_conflict())
    return EXIT_FAILURE;

  printf("EGL/OpenVG thread safety smoke test passed\n");
  return EXIT_SUCCESS;
}
