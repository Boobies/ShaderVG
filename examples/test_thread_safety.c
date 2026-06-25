#include <EGL/egl.h>
#include <VG/openvg.h>
#include <VG/vgext.h>

#include <X11/Xlib.h>

#include <errno.h>
#include <stdarg.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

extern int shGetThreadDrawTrace(void);
extern const char *shDrawTraceName(int phase);

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
  int churnLane;
  int paintProfile;
  int paintDrawMode;
  int verbose;
  const char *workerName;
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
  VGPaint paint;
  int destroyedPaint;
} DestroyPaintArgs;

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
  int testCase;
  int churnLane;
  int churnSchedule;
  int paintProfile;
  int paintDrawMode;
  int renderingQuality;
  int verbose;
} ThreadTestOptions;

typedef struct
{
  const char *name;
  int value;
} ThreadNamedValue;

typedef void *(*ThreadWorkerFunc)(void *arg);

enum {
  THREAD_TEARDOWN_DESTROY_CONTEXT,
  THREAD_TEARDOWN_DESTROY_SURFACE,
  THREAD_TEARDOWN_TERMINATE_DISPLAY
};

enum {
  THREAD_CASE_ALL,
  THREAD_CASE_CONCURRENT_FIRST_EGL,
  THREAD_CASE_DISPLAY_CANONICALIZATION,
  THREAD_CASE_DISPLAY_STALE_GENERATION,
  THREAD_CASE_SAME_THREAD_DEFERRED_DESTROY,
  THREAD_CASE_RELEASE_THREAD_CLEARS_CURRENT,
  THREAD_CASE_RELEASE_THREAD_FINALIZES_DEFERRED_DESTROY,
  THREAD_CASE_CROSS_THREAD_TEARDOWN_CONFLICTS,
  THREAD_CASE_WARM_UP_EGL,
  THREAD_CASE_INDEPENDENT_CONTEXTS,
  THREAD_CASE_SHARED_CONTEXT_STRESS,
  THREAD_CASE_SHARED_RESOURCE_CHURN,
  THREAD_CASE_RETAINED_RESOURCE_LIFETIME,
  THREAD_CASE_SELECTED_PAINT_LIFETIME,
  THREAD_CASE_IMAGE_PBUFFER_REF_RACE,
  THREAD_CASE_MAKE_CURRENT_CONFLICT
};

enum {
  THREAD_CHURN_ALL,
  THREAD_CHURN_SHARED_RESOURCE,
  THREAD_CHURN_PATH,
  THREAD_CHURN_PAINT,
  THREAD_CHURN_IMAGE,
  THREAD_CHURN_FONT,
  THREAD_CHURN_MASK_LAYER,
  THREAD_CHURN_FILTER,
  THREAD_CHURN_CONTEXT_ONLY
};

enum {
  THREAD_CHURN_SCHEDULE_SEQUENTIAL,
  THREAD_CHURN_SCHEDULE_COMBINED
};

enum {
  THREAD_PAINT_PROFILE_FULL,
  THREAD_PAINT_PROFILE_COLOR_ONLY,
  THREAD_PAINT_PROFILE_RAMP_THEN_COLOR,
  THREAD_PAINT_PROFILE_RAMP_THEN_COLOR_NO_IMAGE,
  THREAD_PAINT_PROFILE_IMAGE_THEN_COLOR
};

enum {
  THREAD_PAINT_DRAW_FILL_STROKE,
  THREAD_PAINT_DRAW_FILL,
  THREAD_PAINT_DRAW_STROKE,
  THREAD_PAINT_DRAW_SPLIT
};

enum {
  THREAD_RENDERING_QUALITY_DEFAULT,
  THREAD_RENDERING_QUALITY_NONANTIALIASED,
  THREAD_RENDERING_QUALITY_FASTER,
  THREAD_RENDERING_QUALITY_BETTER
};

enum {
  THREAD_PHASE_NONE,
  THREAD_PHASE_WORKER_START,
  THREAD_PHASE_WORKER_END,
  THREAD_PHASE_CHURN_EGL_INIT,
  THREAD_PHASE_CHURN_BODY,
  THREAD_PHASE_CHURN_EGL_CLEANUP,
  THREAD_PHASE_SELECTED_PAINT_SELECT,
  THREAD_PHASE_SELECTED_PAINT_DESTROY,
  THREAD_PHASE_SELECTED_PAINT_QUERY,
  THREAD_PHASE_SELECTED_PAINT_DRAW,
  THREAD_PHASE_SELECTED_PAINT_FINISH,
  THREAD_PHASE_SELECTED_PAINT_RELEASE,
  THREAD_PHASE_PAINT_RESOURCE_CREATE,
  THREAD_PHASE_PAINT_RESOURCE_VALIDATE,
  THREAD_PHASE_PAINT_SELECT,
  THREAD_PHASE_PAINT_RESET,
  THREAD_PHASE_PAINT_COLOR_SET,
  THREAD_PHASE_PAINT_COLOR_GET,
  THREAD_PHASE_PAINT_COLOR_PARAMETER_SET,
  THREAD_PHASE_PAINT_COLOR_PARAMETER_GET,
  THREAD_PHASE_PAINT_RAMP_SPREAD_MODE,
  THREAD_PHASE_PAINT_RAMP_PREMULTIPLIED,
  THREAD_PHASE_PAINT_LINEAR_GRADIENT,
  THREAD_PHASE_PAINT_RADIAL_GRADIENT,
  THREAD_PHASE_PAINT_RAMP_STOPS,
  THREAD_PHASE_PAINT_TYPE_SWITCH,
  THREAD_PHASE_PAINT_PATTERN_ATTACH,
  THREAD_PHASE_PAINT_TYPE_QUERY,
  THREAD_PHASE_PAINT_RAMP_QUERY,
  THREAD_PHASE_PAINT_DRAW_PATH,
  THREAD_PHASE_PAINT_DRAW_PATH_FILL,
  THREAD_PHASE_PAINT_DRAW_PATH_STROKE,
  THREAD_PHASE_PAINT_FINISH,
  THREAD_PHASE_PAINT_ERROR_CHECK,
  THREAD_PHASE_PAINT_RETAINED_CREATE,
  THREAD_PHASE_PAINT_RETAINED_COLOR,
  THREAD_PHASE_PAINT_CLEAR_SELECTION,
  THREAD_PHASE_PAINT_CLEANUP_RETAINED,
  THREAD_PHASE_PAINT_CLEANUP_PAINT,
  THREAD_PHASE_PAINT_CLEANUP_PATTERN,
  THREAD_PHASE_PAINT_CLEANUP_PATH,
  THREAD_PHASE_PAINT_DONE,
  THREAD_PHASE_PAINT_EGL_INIT,
  THREAD_PHASE_PAINT_CHURN,
  THREAD_PHASE_PAINT_EGL_CLEANUP,
  THREAD_PHASE_MASK_RESOURCE_CREATE,
  THREAD_PHASE_MASK_RESOURCE_VALIDATE,
  THREAD_PHASE_MASK_PAINT_INIT,
  THREAD_PHASE_MASK_LAYER_CREATE,
  THREAD_PHASE_MASK_RESET,
  THREAD_PHASE_MASK_ENABLE,
  THREAD_PHASE_MASK_FILL_SURFACE,
  THREAD_PHASE_MASK_FILL_LAYER_CLEAR,
  THREAD_PHASE_MASK_FILL_LAYER_STRIPE,
  THREAD_PHASE_MASK_SET_FROM_LAYER,
  THREAD_PHASE_MASK_COPY,
  THREAD_PHASE_MASK_CLEAR_SURFACE,
  THREAD_PHASE_MASK_UNION_COPY,
  THREAD_PHASE_MASK_SELECT_PAINT,
  THREAD_PHASE_MASK_DRAW_PATH,
  THREAD_PHASE_MASK_FINISH,
  THREAD_PHASE_MASK_ERROR_CHECK,
  THREAD_PHASE_MASK_DISABLE,
  THREAD_PHASE_MASK_CLEANUP_COPY,
  THREAD_PHASE_MASK_CLEANUP_LAYER,
  THREAD_PHASE_MASK_RESET_STATE,
  THREAD_PHASE_MASK_CLEANUP_PAINT,
  THREAD_PHASE_MASK_CLEANUP_PATH,
  THREAD_PHASE_MASK_DONE
};

enum {
  THREAD_IMAGE_SIZE = 8,
  THREAD_PATH_CAPS =
    VG_PATH_CAPABILITY_APPEND_TO |
    VG_PATH_CAPABILITY_APPEND_FROM |
    VG_PATH_CAPABILITY_MODIFY |
    VG_PATH_CAPABILITY_TRANSFORM_TO |
    VG_PATH_CAPABILITY_TRANSFORM_FROM |
    VG_PATH_CAPABILITY_INTERPOLATE_TO |
    VG_PATH_CAPABILITY_INTERPOLATE_FROM |
    VG_PATH_CAPABILITY_PATH_BOUNDS |
    VG_PATH_CAPABILITY_PATH_LENGTH |
    VG_PATH_CAPABILITY_POINT_ALONG_PATH |
    VG_PATH_CAPABILITY_TANGENT_ALONG_PATH,
  THREAD_DEFAULT_REPEAT = 1,
  THREAD_DEFAULT_SHARED_WORKERS = 2,
  THREAD_DEFAULT_SHARED_ITERATIONS = 64,
  THREAD_MAX_REPEAT = 1000,
  THREAD_MAX_SHARED_WORKERS = 16,
  THREAD_MAX_SHARED_ITERATIONS = 100000
};

static const ThreadNamedValue threadTestCaseValues[] = {
  {"all", THREAD_CASE_ALL},
  {"concurrent-first-egl", THREAD_CASE_CONCURRENT_FIRST_EGL},
  {"display-canonicalization", THREAD_CASE_DISPLAY_CANONICALIZATION},
  {"display-stale-generation", THREAD_CASE_DISPLAY_STALE_GENERATION},
  {"same-thread-deferred-destroy", THREAD_CASE_SAME_THREAD_DEFERRED_DESTROY},
  {"release-thread-clears-current", THREAD_CASE_RELEASE_THREAD_CLEARS_CURRENT},
  {"release-thread-finalizes-deferred-destroy",
   THREAD_CASE_RELEASE_THREAD_FINALIZES_DEFERRED_DESTROY},
  {"cross-thread-teardown-conflicts",
   THREAD_CASE_CROSS_THREAD_TEARDOWN_CONFLICTS},
  {"warm-up-egl", THREAD_CASE_WARM_UP_EGL},
  {"independent-contexts", THREAD_CASE_INDEPENDENT_CONTEXTS},
  {"shared-context-stress", THREAD_CASE_SHARED_CONTEXT_STRESS},
  {"shared-resource-churn", THREAD_CASE_SHARED_RESOURCE_CHURN},
  {"retained-resource-lifetime", THREAD_CASE_RETAINED_RESOURCE_LIFETIME},
  {"selected-paint-lifetime", THREAD_CASE_SELECTED_PAINT_LIFETIME},
  {"image-pbuffer-ref-race", THREAD_CASE_IMAGE_PBUFFER_REF_RACE},
  {"make-current-conflict", THREAD_CASE_MAKE_CURRENT_CONFLICT},
  {NULL, 0}
};

static const ThreadNamedValue threadChurnLaneValues[] = {
  {"all", THREAD_CHURN_ALL},
  {"shared-resource", THREAD_CHURN_SHARED_RESOURCE},
  {"path", THREAD_CHURN_PATH},
  {"paint", THREAD_CHURN_PAINT},
  {"image", THREAD_CHURN_IMAGE},
  {"font", THREAD_CHURN_FONT},
  {"mask-layer", THREAD_CHURN_MASK_LAYER},
  {"filter", THREAD_CHURN_FILTER},
  {"context-only", THREAD_CHURN_CONTEXT_ONLY},
  {NULL, 0}
};

static const ThreadNamedValue threadChurnScheduleValues[] = {
  {"sequential", THREAD_CHURN_SCHEDULE_SEQUENTIAL},
  {"combined", THREAD_CHURN_SCHEDULE_COMBINED},
  {NULL, 0}
};

static const ThreadNamedValue threadPaintProfileValues[] = {
  {"full", THREAD_PAINT_PROFILE_FULL},
  {"color-only", THREAD_PAINT_PROFILE_COLOR_ONLY},
  {"ramp-then-color", THREAD_PAINT_PROFILE_RAMP_THEN_COLOR},
  {"ramp-then-color-no-image", THREAD_PAINT_PROFILE_RAMP_THEN_COLOR_NO_IMAGE},
  {"image-then-color", THREAD_PAINT_PROFILE_IMAGE_THEN_COLOR},
  {NULL, 0}
};

static const ThreadNamedValue threadPaintDrawModeValues[] = {
  {"fill-stroke", THREAD_PAINT_DRAW_FILL_STROKE},
  {"fill", THREAD_PAINT_DRAW_FILL},
  {"stroke", THREAD_PAINT_DRAW_STROKE},
  {"split", THREAD_PAINT_DRAW_SPLIT},
  {NULL, 0}
};

static const ThreadNamedValue threadRenderingQualityValues[] = {
  {"default", THREAD_RENDERING_QUALITY_DEFAULT},
  {"nonantialiased", THREAD_RENDERING_QUALITY_NONANTIALIASED},
  {"faster", THREAD_RENDERING_QUALITY_FASTER},
  {"better", THREAD_RENDERING_QUALITY_BETTER},
  {NULL, 0}
};

static int g_threadTestRenderingQuality = THREAD_RENDERING_QUALITY_DEFAULT;

#if !defined(_WIN32)
static volatile sig_atomic_t g_activeTestCase = THREAD_CASE_ALL;
static volatile sig_atomic_t g_activeChurnLane = THREAD_CHURN_ALL;
static volatile sig_atomic_t g_activePaintProfile = THREAD_PAINT_PROFILE_FULL;
static volatile sig_atomic_t g_activePaintDrawMode =
  THREAD_PAINT_DRAW_FILL_STROKE;
#if defined(_MSC_VER)
static __declspec(thread) volatile sig_atomic_t t_activeChurnLane =
  THREAD_CHURN_ALL;
static __declspec(thread) volatile sig_atomic_t t_activeWorkerId = -1;
static __declspec(thread) volatile sig_atomic_t t_activeIteration = -1;
static __declspec(thread) volatile sig_atomic_t t_activePaintProfile =
  THREAD_PAINT_PROFILE_FULL;
static __declspec(thread) volatile sig_atomic_t t_activePaintDrawMode =
  THREAD_PAINT_DRAW_FILL_STROKE;
static __declspec(thread) volatile sig_atomic_t t_activePhase =
  THREAD_PHASE_NONE;
#else
static __thread volatile sig_atomic_t t_activeChurnLane = THREAD_CHURN_ALL;
static __thread volatile sig_atomic_t t_activeWorkerId = -1;
static __thread volatile sig_atomic_t t_activeIteration = -1;
static __thread volatile sig_atomic_t t_activePaintProfile =
  THREAD_PAINT_PROFILE_FULL;
static __thread volatile sig_atomic_t t_activePaintDrawMode =
  THREAD_PAINT_DRAW_FILL_STROKE;
static __thread volatile sig_atomic_t t_activePhase = THREAD_PHASE_NONE;
#endif

static const char *thread_signal_test_case_name(sig_atomic_t testCase)
{
  switch (testCase) {
  case THREAD_CASE_CONCURRENT_FIRST_EGL:
    return "concurrent-first-egl";
  case THREAD_CASE_DISPLAY_CANONICALIZATION:
    return "display-canonicalization";
  case THREAD_CASE_DISPLAY_STALE_GENERATION:
    return "display-stale-generation";
  case THREAD_CASE_SAME_THREAD_DEFERRED_DESTROY:
    return "same-thread-deferred-destroy";
  case THREAD_CASE_RELEASE_THREAD_CLEARS_CURRENT:
    return "release-thread-clears-current";
  case THREAD_CASE_RELEASE_THREAD_FINALIZES_DEFERRED_DESTROY:
    return "release-thread-finalizes-deferred-destroy";
  case THREAD_CASE_CROSS_THREAD_TEARDOWN_CONFLICTS:
    return "cross-thread-teardown-conflicts";
  case THREAD_CASE_WARM_UP_EGL:
    return "warm-up-egl";
  case THREAD_CASE_INDEPENDENT_CONTEXTS:
    return "independent-contexts";
  case THREAD_CASE_SHARED_CONTEXT_STRESS:
    return "shared-context-stress";
  case THREAD_CASE_SHARED_RESOURCE_CHURN:
    return "shared-resource-churn";
  case THREAD_CASE_RETAINED_RESOURCE_LIFETIME:
    return "retained-resource-lifetime";
  case THREAD_CASE_SELECTED_PAINT_LIFETIME:
    return "selected-paint-lifetime";
  case THREAD_CASE_IMAGE_PBUFFER_REF_RACE:
    return "image-pbuffer-ref-race";
  case THREAD_CASE_MAKE_CURRENT_CONFLICT:
    return "make-current-conflict";
  default:
    return "none";
  }
}

static const char *thread_signal_churn_lane_name(sig_atomic_t lane)
{
  switch (lane) {
  case THREAD_CHURN_SHARED_RESOURCE:
    return "shared-resource";
  case THREAD_CHURN_PATH:
    return "path";
  case THREAD_CHURN_PAINT:
    return "paint";
  case THREAD_CHURN_IMAGE:
    return "image";
  case THREAD_CHURN_FONT:
    return "font";
  case THREAD_CHURN_MASK_LAYER:
    return "mask-layer";
  case THREAD_CHURN_FILTER:
    return "filter";
  case THREAD_CHURN_CONTEXT_ONLY:
    return "context-only";
  default:
    return "none";
  }
}

static const char *thread_signal_paint_profile_name(sig_atomic_t profile)
{
  switch (profile) {
  case THREAD_PAINT_PROFILE_FULL:
    return "full";
  case THREAD_PAINT_PROFILE_COLOR_ONLY:
    return "color-only";
  case THREAD_PAINT_PROFILE_RAMP_THEN_COLOR:
    return "ramp-then-color";
  case THREAD_PAINT_PROFILE_RAMP_THEN_COLOR_NO_IMAGE:
    return "ramp-then-color-no-image";
  case THREAD_PAINT_PROFILE_IMAGE_THEN_COLOR:
    return "image-then-color";
  default:
    return "unknown";
  }
}

static const char *thread_signal_paint_draw_mode_name(sig_atomic_t drawMode)
{
  switch (drawMode) {
  case THREAD_PAINT_DRAW_FILL_STROKE:
    return "fill-stroke";
  case THREAD_PAINT_DRAW_FILL:
    return "fill";
  case THREAD_PAINT_DRAW_STROKE:
    return "stroke";
  case THREAD_PAINT_DRAW_SPLIT:
    return "split";
  default:
    return "unknown";
  }
}

static const char *thread_signal_phase_name(sig_atomic_t phase)
{
  switch (phase) {
  case THREAD_PHASE_WORKER_START:
    return "worker-start";
  case THREAD_PHASE_WORKER_END:
    return "worker-end";
  case THREAD_PHASE_CHURN_EGL_INIT:
    return "churn-egl-init";
  case THREAD_PHASE_CHURN_BODY:
    return "churn-body";
  case THREAD_PHASE_CHURN_EGL_CLEANUP:
    return "churn-egl-cleanup";
  case THREAD_PHASE_SELECTED_PAINT_SELECT:
    return "selected-paint-select";
  case THREAD_PHASE_SELECTED_PAINT_DESTROY:
    return "selected-paint-destroy";
  case THREAD_PHASE_SELECTED_PAINT_QUERY:
    return "selected-paint-query";
  case THREAD_PHASE_SELECTED_PAINT_DRAW:
    return "selected-paint-draw";
  case THREAD_PHASE_SELECTED_PAINT_FINISH:
    return "selected-paint-finish";
  case THREAD_PHASE_SELECTED_PAINT_RELEASE:
    return "selected-paint-release";
  case THREAD_PHASE_PAINT_RESOURCE_CREATE:
    return "paint-resource-create";
  case THREAD_PHASE_PAINT_RESOURCE_VALIDATE:
    return "paint-resource-validate";
  case THREAD_PHASE_PAINT_SELECT:
    return "paint-select";
  case THREAD_PHASE_PAINT_RESET:
    return "paint-reset";
  case THREAD_PHASE_PAINT_COLOR_SET:
    return "paint-color-set";
  case THREAD_PHASE_PAINT_COLOR_GET:
    return "paint-color-get";
  case THREAD_PHASE_PAINT_COLOR_PARAMETER_SET:
    return "paint-color-parameter-set";
  case THREAD_PHASE_PAINT_COLOR_PARAMETER_GET:
    return "paint-color-parameter-get";
  case THREAD_PHASE_PAINT_RAMP_SPREAD_MODE:
    return "paint-ramp-spread-mode";
  case THREAD_PHASE_PAINT_RAMP_PREMULTIPLIED:
    return "paint-ramp-premultiplied";
  case THREAD_PHASE_PAINT_LINEAR_GRADIENT:
    return "paint-linear-gradient";
  case THREAD_PHASE_PAINT_RADIAL_GRADIENT:
    return "paint-radial-gradient";
  case THREAD_PHASE_PAINT_RAMP_STOPS:
    return "paint-ramp-stops";
  case THREAD_PHASE_PAINT_TYPE_SWITCH:
    return "paint-type-switch";
  case THREAD_PHASE_PAINT_PATTERN_ATTACH:
    return "paint-pattern-attach";
  case THREAD_PHASE_PAINT_TYPE_QUERY:
    return "paint-type-query";
  case THREAD_PHASE_PAINT_RAMP_QUERY:
    return "paint-ramp-query";
  case THREAD_PHASE_PAINT_DRAW_PATH:
    return "paint-draw-path";
  case THREAD_PHASE_PAINT_DRAW_PATH_FILL:
    return "paint-draw-path-fill";
  case THREAD_PHASE_PAINT_DRAW_PATH_STROKE:
    return "paint-draw-path-stroke";
  case THREAD_PHASE_PAINT_FINISH:
    return "paint-finish";
  case THREAD_PHASE_PAINT_ERROR_CHECK:
    return "paint-error-check";
  case THREAD_PHASE_PAINT_RETAINED_CREATE:
    return "paint-retained-create";
  case THREAD_PHASE_PAINT_RETAINED_COLOR:
    return "paint-retained-color";
  case THREAD_PHASE_PAINT_CLEAR_SELECTION:
    return "paint-clear-selection";
  case THREAD_PHASE_PAINT_CLEANUP_RETAINED:
    return "paint-cleanup-retained";
  case THREAD_PHASE_PAINT_CLEANUP_PAINT:
    return "paint-cleanup-paint";
  case THREAD_PHASE_PAINT_CLEANUP_PATTERN:
    return "paint-cleanup-pattern";
  case THREAD_PHASE_PAINT_CLEANUP_PATH:
    return "paint-cleanup-path";
  case THREAD_PHASE_PAINT_DONE:
    return "paint-done";
  case THREAD_PHASE_PAINT_EGL_INIT:
    return "paint-egl-init";
  case THREAD_PHASE_PAINT_CHURN:
    return "paint-churn";
  case THREAD_PHASE_PAINT_EGL_CLEANUP:
    return "paint-egl-cleanup";
  case THREAD_PHASE_MASK_RESOURCE_CREATE:
    return "mask-resource-create";
  case THREAD_PHASE_MASK_RESOURCE_VALIDATE:
    return "mask-resource-validate";
  case THREAD_PHASE_MASK_PAINT_INIT:
    return "mask-paint-init";
  case THREAD_PHASE_MASK_LAYER_CREATE:
    return "mask-layer-create";
  case THREAD_PHASE_MASK_RESET:
    return "mask-reset";
  case THREAD_PHASE_MASK_ENABLE:
    return "mask-enable";
  case THREAD_PHASE_MASK_FILL_SURFACE:
    return "mask-fill-surface";
  case THREAD_PHASE_MASK_FILL_LAYER_CLEAR:
    return "mask-fill-layer-clear";
  case THREAD_PHASE_MASK_FILL_LAYER_STRIPE:
    return "mask-fill-layer-stripe";
  case THREAD_PHASE_MASK_SET_FROM_LAYER:
    return "mask-set-from-layer";
  case THREAD_PHASE_MASK_COPY:
    return "mask-copy";
  case THREAD_PHASE_MASK_CLEAR_SURFACE:
    return "mask-clear-surface";
  case THREAD_PHASE_MASK_UNION_COPY:
    return "mask-union-copy";
  case THREAD_PHASE_MASK_SELECT_PAINT:
    return "mask-select-paint";
  case THREAD_PHASE_MASK_DRAW_PATH:
    return "mask-draw-path";
  case THREAD_PHASE_MASK_FINISH:
    return "mask-finish";
  case THREAD_PHASE_MASK_ERROR_CHECK:
    return "mask-error-check";
  case THREAD_PHASE_MASK_DISABLE:
    return "mask-disable";
  case THREAD_PHASE_MASK_CLEANUP_COPY:
    return "mask-cleanup-copy";
  case THREAD_PHASE_MASK_CLEANUP_LAYER:
    return "mask-cleanup-layer";
  case THREAD_PHASE_MASK_RESET_STATE:
    return "mask-reset-state";
  case THREAD_PHASE_MASK_CLEANUP_PAINT:
    return "mask-cleanup-paint";
  case THREAD_PHASE_MASK_CLEANUP_PATH:
    return "mask-cleanup-path";
  case THREAD_PHASE_MASK_DONE:
    return "mask-done";
  default:
    return "none";
  }
}

static void thread_signal_write(const char *text)
{
  size_t length = 0;

  if (!text)
    return;

  while (text[length] != '\0')
    ++length;

  if (length > 0)
    (void)write(STDERR_FILENO, text, length);
}

static void thread_signal_write_int(sig_atomic_t value)
{
  char buffer[32];
  size_t pos = sizeof(buffer);
  sig_atomic_t digit;

  if (value < 0) {
    thread_signal_write("none");
    return;
  }

  buffer[--pos] = '\0';
  do {
    digit = value % 10;
    value /= 10;
    buffer[--pos] = (char)('0' + digit);
  } while (value > 0 && pos > 0);

  (void)write(STDERR_FILENO, &buffer[pos],
              sizeof(buffer) - pos - 1);
}

static void thread_test_crash_handler(int signo)
{
  sig_atomic_t lane = t_activeChurnLane;
  sig_atomic_t paintProfile = t_activePaintProfile;
  sig_atomic_t paintDrawMode = t_activePaintDrawMode;
  sig_atomic_t phase = t_activePhase;
  int drawTrace = shGetThreadDrawTrace();

  if (lane == THREAD_CHURN_ALL)
    lane = g_activeChurnLane;

  if (paintProfile == THREAD_PAINT_PROFILE_FULL)
    paintProfile = g_activePaintProfile;
  if (paintDrawMode == THREAD_PAINT_DRAW_FILL_STROKE)
    paintDrawMode = g_activePaintDrawMode;

  thread_signal_write("\nthread test crashed while running case ");
  thread_signal_write(thread_signal_test_case_name(g_activeTestCase));
  thread_signal_write(", churn lane ");
  thread_signal_write(thread_signal_churn_lane_name(lane));
  if (lane == THREAD_CHURN_PAINT) {
    thread_signal_write(", paint profile ");
    thread_signal_write(thread_signal_paint_profile_name(paintProfile));
    thread_signal_write(", paint draw mode ");
    thread_signal_write(thread_signal_paint_draw_mode_name(paintDrawMode));
  }
  thread_signal_write(", worker ");
  thread_signal_write_int(t_activeWorkerId);
  thread_signal_write(", iteration ");
  thread_signal_write_int(t_activeIteration);
  thread_signal_write(", phase ");
  thread_signal_write(thread_signal_phase_name(phase));
  if (drawTrace > 0) {
    thread_signal_write(", draw trace ");
    thread_signal_write(shDrawTraceName(drawTrace));
  }
  thread_signal_write("\n");

  signal(signo, SIG_DFL);
  raise(signo);
}

static void install_thread_test_crash_handler(void)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = thread_test_crash_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESETHAND;

  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGABRT, &action, NULL);
#ifdef SIGBUS
  sigaction(SIGBUS, &action, NULL);
#endif
}

static void thread_test_set_active_case(int testCase)
{
  g_activeTestCase = testCase;
}

static void thread_test_set_active_churn_lane(int lane)
{
  g_activeChurnLane = lane;
  t_activeChurnLane = lane;
}

static void thread_test_set_active_paint_options(int profile, int drawMode)
{
  g_activePaintProfile = profile;
  g_activePaintDrawMode = drawMode;
  t_activePaintProfile = profile;
  t_activePaintDrawMode = drawMode;
}

static void thread_test_set_active_worker(int workerId)
{
  t_activeWorkerId = workerId;
}

static void thread_test_set_active_iteration(int iteration)
{
  t_activeIteration = iteration;
}

static void thread_test_set_active_phase(int phase)
{
  t_activePhase = phase;
}

static void thread_test_clear_active_detail(void)
{
  t_activeWorkerId = -1;
  t_activeIteration = -1;
  t_activePhase = THREAD_PHASE_NONE;
}
#else
static void install_thread_test_crash_handler(void)
{
}

static void thread_test_set_active_case(int testCase)
{
  (void)testCase;
}

static void thread_test_set_active_churn_lane(int lane)
{
  (void)lane;
}

static void thread_test_set_active_paint_options(int profile, int drawMode)
{
  (void)profile;
  (void)drawMode;
}

static void thread_test_set_active_worker(int workerId)
{
  (void)workerId;
}

static void thread_test_set_active_iteration(int iteration)
{
  (void)iteration;
}

static void thread_test_set_active_phase(int phase)
{
  (void)phase;
}

static void thread_test_clear_active_detail(void)
{
}
#endif

static void thread_test_log(int verbose, const char *format, ...)
{
  va_list args;

  if (!verbose)
    return;

  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fflush(stderr);
}

static const char *thread_named_value_name(const ThreadNamedValue *values,
                                           int value)
{
  int i;

  for (i = 0; values[i].name; ++i)
    if (values[i].value == value)
      return values[i].name;

  return "unknown";
}

static void print_named_values(const ThreadNamedValue *values)
{
  int i;

  for (i = 0; values[i].name; ++i)
    fprintf(stderr, "%s%s", i == 0 ? "" : ", ", values[i].name);
}

static int parse_named_env(const char *name,
                           const ThreadNamedValue *values,
                           int defaultValue,
                           int *out)
{
  const char *value = getenv(name);
  int i;

  if (!value || !*value) {
    *out = defaultValue;
    return 0;
  }

  for (i = 0; values[i].name; ++i) {
    if (strcmp(value, values[i].name) == 0) {
      *out = values[i].value;
      return 0;
    }
  }

  fprintf(stderr, "%s must be one of: ", name);
  print_named_values(values);
  fprintf(stderr, "\n");
  return 1;
}

static int parse_bool_env(const char *name, int defaultValue, int *out)
{
  const char *value = getenv(name);

  if (!value || !*value) {
    *out = defaultValue;
    return 0;
  }

  if (strcmp(value, "1") == 0 ||
      strcmp(value, "true") == 0 ||
      strcmp(value, "yes") == 0 ||
      strcmp(value, "on") == 0) {
    *out = 1;
    return 0;
  }

  if (strcmp(value, "0") == 0 ||
      strcmp(value, "false") == 0 ||
      strcmp(value, "no") == 0 ||
      strcmp(value, "off") == 0) {
    *out = 0;
    return 0;
  }

  fprintf(stderr,
          "%s must be 1/0, true/false, yes/no, or on/off\n",
          name);
  return 1;
}

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

  if (parse_named_env("SHADERVG_THREAD_TEST_CASE",
                      threadTestCaseValues,
                      THREAD_CASE_ALL,
                      &options->testCase))
    return 1;

  if (parse_named_env("SHADERVG_THREAD_TEST_CHURN_LANE",
                      threadChurnLaneValues,
                      THREAD_CHURN_ALL,
                      &options->churnLane))
    return 1;

  if (parse_named_env("SHADERVG_THREAD_TEST_CHURN_SCHEDULE",
                      threadChurnScheduleValues,
                      THREAD_CHURN_SCHEDULE_SEQUENTIAL,
                      &options->churnSchedule))
    return 1;

  if (parse_named_env("SHADERVG_THREAD_TEST_PAINT_PROFILE",
                      threadPaintProfileValues,
                      THREAD_PAINT_PROFILE_FULL,
                      &options->paintProfile))
    return 1;

  if (parse_named_env("SHADERVG_THREAD_TEST_PAINT_DRAW_MODE",
                      threadPaintDrawModeValues,
                      THREAD_PAINT_DRAW_FILL_STROKE,
                      &options->paintDrawMode))
    return 1;

  if (parse_named_env("SHADERVG_THREAD_TEST_RENDERING_QUALITY",
                      threadRenderingQualityValues,
                      THREAD_RENDERING_QUALITY_DEFAULT,
                      &options->renderingQuality))
    return 1;

  if (parse_bool_env("SHADERVG_THREAD_TEST_VERBOSE",
                     0,
                     &options->verbose))
    return 1;

  g_threadTestRenderingQuality = options->renderingQuality;
  thread_test_set_active_paint_options(options->paintProfile,
                                       options->paintDrawMode);

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

static VGPath create_churn_path(int workerId, int variant)
{
  VGubyte commands[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat offset = (VGfloat)((workerId * 3 + variant * 5) % 11);
  VGfloat coords[] = {
    2.0f + offset, 2.0f,
    14.0f + offset, 3.0f + (VGfloat)variant,
    16.0f + offset, 15.0f,
    3.0f + offset, 14.0f
  };
  VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD,
                             VG_PATH_DATATYPE_F,
                             1.0f,
                             0.0f,
                             5,
                             8,
                             THREAD_PATH_CAPS);

  if (path == VG_INVALID_HANDLE)
    return path;

  vgAppendPathData(path, 5, commands, coords);
  if (vgGetError() != VG_NO_ERROR) {
    vgDestroyPath(path);
    return VG_INVALID_HANDLE;
  }

  return path;
}

static int reset_churn_path(VGPath path, int workerId, int iteration)
{
  VGubyte commands[] = {
    VG_MOVE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_LINE_TO_ABS,
    VG_CLOSE_PATH
  };
  VGfloat offset = (VGfloat)((workerId + iteration) % 13);
  VGfloat coords[] = {
    1.0f + offset, 1.0f,
    13.0f + offset, 2.0f,
    14.0f + offset, 14.0f,
    2.0f + offset, 13.0f
  };

  vgClearPath(path, THREAD_PATH_CAPS);
  vgAppendPathData(path, 5, commands, coords);

  return expect_vg_no_error("thread test could not reset churn path");
}

static int reset_test_state(void)
{
  VGfloat origin[2] = { 0.0f, 0.0f };

  switch (g_threadTestRenderingQuality) {
  case THREAD_RENDERING_QUALITY_NONANTIALIASED:
    vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_NONANTIALIASED);
    break;
  case THREAD_RENDERING_QUALITY_FASTER:
    vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_FASTER);
    break;
  case THREAD_RENDERING_QUALITY_BETTER:
    vgSeti(VG_RENDERING_QUALITY, VG_RENDERING_QUALITY_BETTER);
    break;
  default:
    break;
  }

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

static int exercise_path_object_churn(int workerId, int iterations)
{
  VGPath path = VG_INVALID_HANDLE;
  VGPath src = VG_INVALID_HANDLE;
  VGPath end = VG_INVALID_HANDLE;
  int i;
  int status = 0;

  path = create_churn_path(workerId, 0);
  src = create_churn_path(workerId, 1);
  end = create_churn_path(workerId, 2);
  if (path == VG_INVALID_HANDLE ||
      src == VG_INVALID_HANDLE ||
      end == VG_INVALID_HANDLE)
    status = fail_vg("thread test could not create churn paths");

  for (i = 0; !status && i < iterations; ++i) {
    VGfloat minX = 0.0f;
    VGfloat minY = 0.0f;
    VGfloat width = 0.0f;
    VGfloat height = 0.0f;
    VGfloat x = 0.0f;
    VGfloat y = 0.0f;
    VGfloat tx = 0.0f;
    VGfloat ty = 0.0f;
    VGfloat modify[] = {
      12.0f + (VGfloat)((workerId + i) % 5),
      4.0f + (VGfloat)(i % 7)
    };

    if (reset_test_state() || reset_churn_path(path, workerId, i)) {
      status = 1;
      break;
    }

    vgAppendPath(path, src);
    vgModifyPathCoords(path, 1, 1, modify);
    vgPathBounds(path, &minX, &minY, &width, &height);
    vgPathTransformedBounds(path, &minX, &minY, &width, &height);
    (void)vgPathLength(path, 0, 5);
    vgPointAlongPath(path, 0, 5, 2.0f, &x, &y, &tx, &ty);
    vgRotate((VGfloat)((workerId + i) % 45));
    vgTransformPath(path, src);
    if (!vgInterpolatePath(path, src, end, 0.25f)) {
      status = fail_vg("thread test could not interpolate churn path");
      break;
    }

    if (expect_vg_no_error("thread test path churn failed")) {
      status = 1;
      break;
    }
  }

  if (path != VG_INVALID_HANDLE) {
    vgDestroyPath(path);
    if (expect_vg_no_error("thread test could not destroy churn path"))
      status = 1;
  }

  if (src != VG_INVALID_HANDLE) {
    vgDestroyPath(src);
    if (expect_vg_no_error("thread test could not destroy churn source path"))
      status = 1;
  }

  if (end != VG_INVALID_HANDLE) {
    vgDestroyPath(end);
    if (expect_vg_no_error("thread test could not destroy churn end path"))
      status = 1;
  }

  return status;
}

static int verify_selected_paint_survives_destroy(VGPath path, VGPaint paint)
{
  VGPaint selected;

  thread_test_set_active_phase(THREAD_PHASE_SELECTED_PAINT_SELECT);
  vgSetPaint(paint, VG_FILL_PATH);
  if (expect_vg_no_error("thread test could not select retained paint"))
    return 1;

  thread_test_set_active_phase(THREAD_PHASE_SELECTED_PAINT_DESTROY);
  vgDestroyPaint(paint);
  if (expect_vg_no_error("thread test could not destroy selected paint handle"))
    return 1;

  thread_test_set_active_phase(THREAD_PHASE_SELECTED_PAINT_QUERY);
  selected = vgGetPaint(VG_FILL_PATH);
  if (expect_vg_no_error("thread test could not query destroyed selected paint"))
    return 1;
  if (selected != VG_INVALID_HANDLE) {
    fprintf(stderr, "thread test returned a live handle for destroyed selected paint\n");
    return 1;
  }

  thread_test_set_active_phase(THREAD_PHASE_SELECTED_PAINT_DRAW);
  vgDrawPath(path, VG_FILL_PATH);
  thread_test_set_active_phase(THREAD_PHASE_SELECTED_PAINT_FINISH);
  vgFinish();
  if (expect_vg_no_error("thread test could not draw with retained selected paint"))
    return 1;

  thread_test_set_active_phase(THREAD_PHASE_SELECTED_PAINT_RELEASE);
  vgSetPaint(VG_INVALID_HANDLE, VG_FILL_PATH);
  return expect_vg_no_error("thread test could not release retained selected paint");
}

static void draw_paint_churn_path(VGPath path, int paintDrawMode)
{
  switch (paintDrawMode) {
  case THREAD_PAINT_DRAW_FILL:
    thread_test_set_active_phase(THREAD_PHASE_PAINT_DRAW_PATH_FILL);
    vgDrawPath(path, VG_FILL_PATH);
    break;
  case THREAD_PAINT_DRAW_STROKE:
    thread_test_set_active_phase(THREAD_PHASE_PAINT_DRAW_PATH_STROKE);
    vgDrawPath(path, VG_STROKE_PATH);
    break;
  case THREAD_PAINT_DRAW_SPLIT:
    thread_test_set_active_phase(THREAD_PHASE_PAINT_DRAW_PATH_FILL);
    vgDrawPath(path, VG_FILL_PATH);
    thread_test_set_active_phase(THREAD_PHASE_PAINT_DRAW_PATH_STROKE);
    vgDrawPath(path, VG_STROKE_PATH);
    break;
  case THREAD_PAINT_DRAW_FILL_STROKE:
  default:
    thread_test_set_active_phase(THREAD_PHASE_PAINT_DRAW_PATH);
    vgDrawPath(path, VG_FILL_PATH | VG_STROKE_PATH);
    break;
  }
}

static int paint_profile_uses_pattern_resource(int paintProfile)
{
  return paintProfile == THREAD_PAINT_PROFILE_FULL ||
         paintProfile == THREAD_PAINT_PROFILE_RAMP_THEN_COLOR ||
         paintProfile == THREAD_PAINT_PROFILE_IMAGE_THEN_COLOR;
}

static int paint_profile_uploads_ramp(int paintProfile)
{
  return paintProfile == THREAD_PAINT_PROFILE_FULL ||
         paintProfile == THREAD_PAINT_PROFILE_RAMP_THEN_COLOR ||
         paintProfile == THREAD_PAINT_PROFILE_RAMP_THEN_COLOR_NO_IMAGE;
}

static int paint_profile_uses_iteration_zero(int paintProfile)
{
  return paintProfile == THREAD_PAINT_PROFILE_RAMP_THEN_COLOR ||
         paintProfile == THREAD_PAINT_PROFILE_RAMP_THEN_COLOR_NO_IMAGE ||
         paintProfile == THREAD_PAINT_PROFILE_IMAGE_THEN_COLOR;
}

static int exercise_paint_object_churn(int workerId,
                                       int iterations,
                                       int paintProfile,
                                       int paintDrawMode)
{
  VGPath path = VG_INVALID_HANDLE;
  VGImage pattern = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  VGPaint retained = VG_INVALID_HANDLE;
  int i;
  int status = 0;

  thread_test_set_active_iteration(-1);
  thread_test_set_active_phase(THREAD_PHASE_PAINT_RESOURCE_CREATE);
  path = create_rect_path();
  if (paint_profile_uses_pattern_resource(paintProfile))
    pattern = create_colored_image(workerId, 2000);
  paint = vgCreatePaint();
  thread_test_set_active_phase(THREAD_PHASE_PAINT_RESOURCE_VALIDATE);
  if (path == VG_INVALID_HANDLE ||
      (paint_profile_uses_pattern_resource(paintProfile) &&
       pattern == VG_INVALID_HANDLE) ||
      paint == VG_INVALID_HANDLE) {
    status = fail_vg("thread test could not create paint churn resources");
  } else if (expect_vg_no_error("thread test could not initialize paint churn resources")) {
    status = 1;
  }

  if (!status) {
    thread_test_set_active_phase(THREAD_PHASE_PAINT_SELECT);
    vgSetPaint(paint, VG_FILL_PATH | VG_STROKE_PATH);
    vgSetf(VG_STROKE_LINE_WIDTH, 1.0f);
    if (expect_vg_no_error("thread test could not select churn paint"))
      status = 1;
  }

  for (i = 0; !status && i < iterations; ++i) {
    int paintVariant =
      paint_profile_uses_iteration_zero(paintProfile) ? 0 : i;
    VGfloat color[4] = {
      ((VGfloat)((workerId + paintVariant) % 7)) / 6.0f,
      ((VGfloat)((workerId * 3 + paintVariant) % 11)) / 10.0f,
      ((VGfloat)((workerId * 5 + paintVariant) % 13)) / 12.0f,
      1.0f
    };
    VGfloat linear[4] = {
      0.0f,
      0.0f,
      24.0f + (VGfloat)(paintVariant % 5),
      24.0f
    };
    VGfloat radial[5] = {
      16.0f,
      16.0f,
      8.0f,
      8.0f,
      18.0f + (VGfloat)(paintVariant % 4)
    };
    VGfloat stops[10] = {
      0.0f,
      color[0],
      color[1],
      color[2],
      1.0f,
      1.0f,
      color[2],
      color[0],
      color[1],
      1.0f
    };
    VGfloat readColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    VGuint packed =
      (((VGuint)((workerId * 41 + paintVariant * 3) & 0xff)) << 24) |
      (((VGuint)((workerId * 17 + paintVariant * 5) & 0xff)) << 16) |
      (((VGuint)((workerId * 29 + paintVariant * 7) & 0xff)) << 8) |
      0xffu;

    thread_test_set_active_iteration(i);
    thread_test_set_active_phase(THREAD_PHASE_PAINT_RESET);
    if (reset_test_state()) {
      status = 1;
      break;
    }

    thread_test_set_active_phase(THREAD_PHASE_PAINT_COLOR_SET);
    vgSetColor(paint, packed);
    thread_test_set_active_phase(THREAD_PHASE_PAINT_COLOR_GET);
    (void)vgGetColor(paint);
    thread_test_set_active_phase(THREAD_PHASE_PAINT_COLOR_PARAMETER_SET);
    vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
    thread_test_set_active_phase(THREAD_PHASE_PAINT_COLOR_PARAMETER_GET);
    vgGetParameterfv(paint, VG_PAINT_COLOR, 4, readColor);

    if (paint_profile_uploads_ramp(paintProfile)) {
      thread_test_set_active_phase(THREAD_PHASE_PAINT_RAMP_SPREAD_MODE);
      vgSetParameteri(paint,
                      VG_PAINT_COLOR_RAMP_SPREAD_MODE,
                      (paintVariant & 1) ? VG_COLOR_RAMP_SPREAD_REPEAT :
                                           VG_COLOR_RAMP_SPREAD_PAD);
      thread_test_set_active_phase(THREAD_PHASE_PAINT_RAMP_PREMULTIPLIED);
      vgSetParameteri(paint,
                      VG_PAINT_COLOR_RAMP_PREMULTIPLIED,
                      (paintVariant & 1) ? VG_TRUE : VG_FALSE);
      thread_test_set_active_phase(THREAD_PHASE_PAINT_LINEAR_GRADIENT);
      vgSetParameterfv(paint, VG_PAINT_LINEAR_GRADIENT, 4, linear);
      thread_test_set_active_phase(THREAD_PHASE_PAINT_RADIAL_GRADIENT);
      vgSetParameterfv(paint, VG_PAINT_RADIAL_GRADIENT, 5, radial);
      thread_test_set_active_phase(THREAD_PHASE_PAINT_RAMP_STOPS);
      vgSetParameterfv(paint, VG_PAINT_COLOR_RAMP_STOPS, 10, stops);
    }

    thread_test_set_active_phase(THREAD_PHASE_PAINT_TYPE_SWITCH);
    if (paintProfile != THREAD_PAINT_PROFILE_FULL) {
      vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
    } else {
      switch (i & 3) {
      case 0:
        vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_COLOR);
        break;
      case 1:
        vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_LINEAR_GRADIENT);
        break;
      case 2:
        vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_RADIAL_GRADIENT);
        break;
      default:
        vgSetParameteri(paint, VG_PAINT_TYPE, VG_PAINT_TYPE_PATTERN);
        vgSetParameteri(paint, VG_PAINT_PATTERN_TILING_MODE, VG_TILE_REPEAT);
        thread_test_set_active_phase(THREAD_PHASE_PAINT_PATTERN_ATTACH);
        vgPaintPattern(paint, pattern);
        break;
      }
    }

    thread_test_set_active_phase(THREAD_PHASE_PAINT_TYPE_QUERY);
    if (vgGetParameteri(paint, VG_PAINT_TYPE) == 0) {
      fprintf(stderr, "thread test paint type query returned zero\n");
      status = 1;
      break;
    }

    if (paint_profile_uploads_ramp(paintProfile)) {
      thread_test_set_active_phase(THREAD_PHASE_PAINT_RAMP_QUERY);
      if (vgGetParameterVectorSize(paint, VG_PAINT_COLOR_RAMP_STOPS) < 10) {
        fprintf(stderr, "thread test paint color-ramp vector size was too small\n");
        status = 1;
        break;
      }
    }

    draw_paint_churn_path(path, paintDrawMode);
    if ((i & 15) == 0) {
      thread_test_set_active_phase(THREAD_PHASE_PAINT_FINISH);
      vgFinish();
    }

    thread_test_set_active_phase(THREAD_PHASE_PAINT_ERROR_CHECK);
    if (expect_vg_no_error("thread test paint churn failed")) {
      status = 1;
      break;
    }
  }

  thread_test_set_active_iteration(-1);
  if (!status) {
    thread_test_set_active_phase(THREAD_PHASE_PAINT_RETAINED_CREATE);
    retained = vgCreatePaint();
    if (retained == VG_INVALID_HANDLE ||
        expect_vg_no_error("thread test could not create retained selected paint")) {
      status = 1;
    } else {
      thread_test_set_active_phase(THREAD_PHASE_PAINT_RETAINED_COLOR);
      vgSetColor(retained, 0x4488ccffu);
      if (expect_vg_no_error("thread test could not color retained selected paint"))
        status = 1;
      else if (verify_selected_paint_survives_destroy(path, retained))
        status = 1;
      retained = VG_INVALID_HANDLE;
    }
  }

  thread_test_set_active_phase(THREAD_PHASE_PAINT_CLEAR_SELECTION);
  vgSetPaint(VG_INVALID_HANDLE, VG_FILL_PATH | VG_STROKE_PATH);
  if (expect_vg_no_error("thread test could not clear paint churn selection"))
    status = 1;

  if (retained != VG_INVALID_HANDLE) {
    thread_test_set_active_phase(THREAD_PHASE_PAINT_CLEANUP_RETAINED);
    vgDestroyPaint(retained);
    if (expect_vg_no_error("thread test could not clean up retained paint"))
      status = 1;
  }

  if (paint != VG_INVALID_HANDLE) {
    thread_test_set_active_phase(THREAD_PHASE_PAINT_CLEANUP_PAINT);
    vgDestroyPaint(paint);
    if (expect_vg_no_error("thread test could not destroy churn paint"))
      status = 1;
  }

  if (pattern != VG_INVALID_HANDLE) {
    thread_test_set_active_phase(THREAD_PHASE_PAINT_CLEANUP_PATTERN);
    vgDestroyImage(pattern);
    if (expect_vg_no_error("thread test could not destroy paint pattern image"))
      status = 1;
  }

  if (path != VG_INVALID_HANDLE) {
    thread_test_set_active_phase(THREAD_PHASE_PAINT_CLEANUP_PATH);
    vgDestroyPath(path);
    if (expect_vg_no_error("thread test could not destroy paint churn path"))
      status = 1;
  }

  thread_test_set_active_phase(THREAD_PHASE_PAINT_DONE);
  return status;
}

static void fill_image_churn_pixels(VGubyte *pixels,
                                    int width,
                                    int height,
                                    int workerId,
                                    int iteration)
{
  int x;
  int y;

  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      int offset = (y * width + x) * 4;
      pixels[offset + 0] = (VGubyte)(20 + (workerId * 37 + iteration * 5 + x * 11) % 220);
      pixels[offset + 1] = (VGubyte)(30 + (workerId * 19 + iteration * 7 + y * 13) % 210);
      pixels[offset + 2] = (VGubyte)(40 + (workerId * 23 + iteration * 3 + x * y) % 200);
      pixels[offset + 3] = 255;
    }
  }
}

static int exercise_image_object_churn(int workerId, int iterations)
{
  enum { CHILD_SIZE = 4 };
  VGubyte pixels[CHILD_SIZE * CHILD_SIZE * 4];
  VGubyte readback[CHILD_SIZE * CHILD_SIZE * 4];
  VGImage parent = VG_INVALID_HANDLE;
  VGImage child = VG_INVALID_HANDLE;
  VGImage copy = VG_INVALID_HANDLE;
  int i;
  int status = 0;

  parent = create_colored_image(workerId, 3000);
  if (parent == VG_INVALID_HANDLE ||
      expect_vg_no_error("thread test could not create image churn parent")) {
    status = 1;
  }

  if (!status) {
    child = vgChildImage(parent, 2, 2, CHILD_SIZE, CHILD_SIZE);
    if (child == VG_INVALID_HANDLE ||
        expect_vg_no_error("thread test could not create image churn child"))
      status = 1;
  }

  if (!status) {
    copy = vgCreateImage(VG_sRGBA_8888,
                         THREAD_IMAGE_SIZE,
                         THREAD_IMAGE_SIZE,
                         VG_IMAGE_QUALITY_BETTER);
    if (copy == VG_INVALID_HANDLE ||
        expect_vg_no_error("thread test could not create image churn copy"))
      status = 1;
  }

  if (!status) {
    vgDestroyImage(parent);
    if (expect_vg_no_error("thread test could not destroy image churn parent"))
      status = 1;
    else
      parent = VG_INVALID_HANDLE;
  }

  if (!status && vgGetParent(child) != child) {
    fprintf(stderr, "thread test child image kept a destroyed public parent\n");
    status = 1;
  } else if (!status &&
             expect_vg_no_error("thread test could not query image churn parent")) {
    status = 1;
  }

  for (i = 0; !status && i < iterations; ++i) {
    fill_image_churn_pixels(pixels, CHILD_SIZE, CHILD_SIZE, workerId, i);

    vgImageSubData(child,
                   pixels,
                   CHILD_SIZE * 4,
                   VG_sRGBA_8888,
                   0,
                   0,
                   CHILD_SIZE,
                   CHILD_SIZE);
    vgGetImageSubData(child,
                      readback,
                      CHILD_SIZE * 4,
                      VG_sRGBA_8888,
                      0,
                      0,
                      CHILD_SIZE,
                      CHILD_SIZE);
    vgCopyImage(copy, 1, 1, child, 0, 0,
                CHILD_SIZE, CHILD_SIZE, VG_FALSE);
    vgClearImage(copy, (i % 3), (i % 3), 2, 2);

    if (reset_test_state()) {
      status = 1;
      break;
    }

    vgDrawImage(child);
    vgSetPixels(0, 0, child, 0, 0, CHILD_SIZE, CHILD_SIZE);
    vgGetPixels(copy, 0, 0, 0, 0, CHILD_SIZE, CHILD_SIZE);

    if ((i & 15) == 0)
      vgFinish();

    if (expect_vg_no_error("thread test image churn failed")) {
      status = 1;
      break;
    }
  }

  if (copy != VG_INVALID_HANDLE) {
    vgDestroyImage(copy);
    if (expect_vg_no_error("thread test could not destroy image churn copy"))
      status = 1;
  }

  if (child != VG_INVALID_HANDLE) {
    vgDestroyImage(child);
    if (expect_vg_no_error("thread test could not destroy image churn child"))
      status = 1;
  }

  if (parent != VG_INVALID_HANDLE) {
    vgDestroyImage(parent);
    if (expect_vg_no_error("thread test could not clean up image churn parent"))
      status = 1;
  }

  return status;
}

static void fill_filter_lookup_tables(VGubyte *redLut,
                                      VGubyte *greenLut,
                                      VGubyte *blueLut,
                                      VGubyte *alphaLut,
                                      VGuint *singleLut)
{
  int i;

  for (i = 0; i < 256; ++i) {
    redLut[i] = (VGubyte)(255 - i);
    greenLut[i] = (VGubyte)i;
    blueLut[i] = (VGubyte)((i * 3) & 0xff);
    alphaLut[i] = 255;
    singleLut[i] =
      (((VGuint)(255 - i)) << 24) |
      (((VGuint)i) << 16) |
      (((VGuint)((i * 5) & 0xff)) << 8) |
      255u;
  }
}

static int exercise_filter_object_churn(int workerId, int iterations)
{
  VGImage source = VG_INVALID_HANDLE;
  VGImage dest = VG_INVALID_HANDLE;
  VGImage blur = VG_INVALID_HANDLE;
  VGPaint highlight = VG_INVALID_HANDLE;
  VGPaint shadow = VG_INVALID_HANDLE;
  VGubyte redLut[256];
  VGubyte greenLut[256];
  VGubyte blueLut[256];
  VGubyte alphaLut[256];
  VGuint singleLut[256];
  VGfloat matrix[20] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 0.0f
  };
  VGshort convolveKernel[9] = {
    0, 0, 0,
    0, 1, 0,
    0, 0, 0
  };
  VGshort separableKernel[1] = { 1 };
  VGfloat highlightColor[4] = { 1.0f, 0.15f, 0.05f, 1.0f };
  VGfloat shadowColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  int i;
  int status = 0;

  fill_filter_lookup_tables(redLut, greenLut, blueLut, alphaLut, singleLut);

  source = create_colored_image(workerId, 5000);
  dest = create_colored_image(workerId, 5001);
  blur = create_colored_image(workerId, 5002);
  highlight = vgCreatePaint();
  shadow = vgCreatePaint();
  if (source == VG_INVALID_HANDLE ||
      dest == VG_INVALID_HANDLE ||
      blur == VG_INVALID_HANDLE ||
      highlight == VG_INVALID_HANDLE ||
      shadow == VG_INVALID_HANDLE ||
      expect_vg_no_error("thread test could not create filter churn resources")) {
    status = 1;
  }

  if (!status) {
    vgSetParameterfv(highlight, VG_PAINT_COLOR, 4, highlightColor);
    vgSetParameterfv(shadow, VG_PAINT_COLOR, 4, shadowColor);
    vgSeti(VG_FILTER_FORMAT_LINEAR, VG_TRUE);
    vgSeti(VG_FILTER_FORMAT_PREMULTIPLIED, VG_FALSE);
    vgSeti(VG_FILTER_CHANNEL_MASK, VG_RED | VG_GREEN | VG_BLUE | VG_ALPHA);
    if (expect_vg_no_error("thread test could not initialize filter churn state"))
      status = 1;
  }

  for (i = 0; !status && i < iterations; ++i) {
    switch (i & 7) {
    case 0:
      vgColorMatrix(dest, source, matrix);
      break;
    case 1:
      vgConvolve(dest, source, 3, 3, 1, 1,
                 convolveKernel, 1.0f, 0.0f, VG_TILE_PAD);
      break;
    case 2:
      vgSeparableConvolve(dest, source, 1, 1, 0, 0,
                          separableKernel, separableKernel,
                          1.0f, 0.0f, VG_TILE_PAD);
      break;
    case 3:
      vgGaussianBlur(dest, source, 1.0f, 1.0f, VG_TILE_PAD);
      break;
    case 4:
      vgLookup(dest, source, redLut, greenLut, blueLut, alphaLut,
               VG_TRUE, VG_FALSE);
      break;
    case 5:
      vgLookupSingle(dest, source, singleLut, VG_GREEN,
                     VG_TRUE, VG_FALSE);
      break;
    case 6:
      vgIterativeAverageBlurKHR(blur, source, 2.0f, 2.0f,
                                1, VG_TILE_PAD);
      break;
    default:
      vgParametricFilterKHR(dest, source, blur,
                            0.6f, 0.0f, 0.0f,
                            VG_PF_OBJECT_VISIBLE_FLAG_KHR |
                            VG_PF_OUTER_FLAG_KHR,
                            highlight, shadow);
      break;
    }

    if ((i & 15) == 0)
      vgFinish();

    if (expect_vg_no_error("thread test filter churn failed"))
      status = 1;
  }

  if (shadow != VG_INVALID_HANDLE) {
    vgDestroyPaint(shadow);
    if (expect_vg_no_error("thread test could not destroy filter shadow paint"))
      status = 1;
  }

  if (highlight != VG_INVALID_HANDLE) {
    vgDestroyPaint(highlight);
    if (expect_vg_no_error("thread test could not destroy filter highlight paint"))
      status = 1;
  }

  if (blur != VG_INVALID_HANDLE) {
    vgDestroyImage(blur);
    if (expect_vg_no_error("thread test could not destroy filter blur image"))
      status = 1;
  }

  if (dest != VG_INVALID_HANDLE) {
    vgDestroyImage(dest);
    if (expect_vg_no_error("thread test could not destroy filter destination image"))
      status = 1;
  }

  if (source != VG_INVALID_HANDLE) {
    vgDestroyImage(source);
    if (expect_vg_no_error("thread test could not destroy filter source image"))
      status = 1;
  }

  return status;
}

static int exercise_font_object_churn(int workerId, int iterations)
{
  VGFont font = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  int i;
  int status = 0;

  font = vgCreateFont(4);
  paint = vgCreatePaint();
  if (font == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      expect_vg_no_error("thread test could not create font churn resources")) {
    status = 1;
  }

  if (!status) {
    vgSetColor(paint, 0x66cc88ffu);
    vgSetPaint(paint, VG_FILL_PATH);
    if (expect_vg_no_error("thread test could not initialize font churn paint"))
      status = 1;
  }

  for (i = 0; !status && i < iterations; ++i) {
    VGPath path = VG_INVALID_HANDLE;
    VGImage image = VG_INVALID_HANDLE;
    VGfloat pathOrigin[2] = { 0.0f, 0.0f };
    VGfloat imageOrigin[2] = { 1.0f, 1.0f };
    VGfloat pathEscapement[2] = { 9.0f + (VGfloat)(i % 3), 0.0f };
    VGfloat imageEscapement[2] = { 7.0f, 0.0f };
    VGuint glyphs[2] = { 101u, 102u };
    VGfloat adjustments[2] = { 0.25f * (VGfloat)(i % 4), 0.0f };
    VGint glyphCount;

    path = create_churn_path(workerId, i);
    image = create_colored_image(workerId, 4000 + i);
    if (path == VG_INVALID_HANDLE ||
        image == VG_INVALID_HANDLE ||
        expect_vg_no_error("thread test could not create font churn glyph resources")) {
      status = 1;
    }

    if (!status) {
      vgSetGlyphToPath(font, glyphs[0], path, VG_FALSE,
                       pathOrigin, pathEscapement);
      vgSetGlyphToImage(font, glyphs[1], image,
                        imageOrigin, imageEscapement);
      if (expect_vg_no_error("thread test could not set font churn glyphs"))
        status = 1;
    }

    if (path != VG_INVALID_HANDLE) {
      vgDestroyPath(path);
      if (expect_vg_no_error("thread test could not destroy font glyph path handle"))
        status = 1;
      path = VG_INVALID_HANDLE;
    }

    if (image != VG_INVALID_HANDLE) {
      vgDestroyImage(image);
      if (expect_vg_no_error("thread test could not destroy font glyph image handle"))
        status = 1;
      image = VG_INVALID_HANDLE;
    }

    if (!status) {
      glyphCount = vgGetParameteri(font, VG_FONT_NUM_GLYPHS);
      if (glyphCount < 2) {
        fprintf(stderr, "thread test font glyph count was too small\n");
        status = 1;
      } else if (vgGetParameterVectorSize(font, VG_FONT_NUM_GLYPHS) != 1) {
        fprintf(stderr, "thread test font vector size was wrong\n");
        status = 1;
      } else if (expect_vg_no_error("thread test could not query font glyph count")) {
        status = 1;
      }
    }

    if (!status && reset_test_state())
      status = 1;

    if (!status) {
      vgSetPaint(paint, VG_FILL_PATH);
      vgDrawGlyph(font, glyphs[0], VG_FILL_PATH, VG_FALSE);
      vgDrawGlyph(font, glyphs[1], VG_FILL_PATH, VG_FALSE);
      if (expect_vg_no_error("thread test could not draw font churn glyphs"))
        status = 1;
    }

    if (!status && reset_test_state())
      status = 1;

    if (!status) {
      vgSetPaint(paint, VG_FILL_PATH);
      vgDrawGlyphs(font, 2, glyphs, adjustments, NULL,
                   VG_FILL_PATH, VG_FALSE);
      if ((i & 15) == 0)
        vgFinish();
      if (expect_vg_no_error("thread test could not draw font churn glyph run"))
        status = 1;
    }

    if (!status) {
      vgClearGlyph(font, glyphs[0]);
      vgClearGlyph(font, glyphs[1]);
      if (expect_vg_no_error("thread test could not clear font churn glyphs"))
        status = 1;
    }

    if (!status && vgGetParameteri(font, VG_FONT_NUM_GLYPHS) != 0) {
      fprintf(stderr, "thread test font glyph count did not return to zero\n");
      status = 1;
    } else if (!status &&
               expect_vg_no_error("thread test could not query cleared font")) {
      status = 1;
    }
  }

  vgSetPaint(VG_INVALID_HANDLE, VG_FILL_PATH);
  if (expect_vg_no_error("thread test could not clear font churn paint selection"))
    status = 1;

  if (font != VG_INVALID_HANDLE) {
    vgDestroyFont(font);
    if (expect_vg_no_error("thread test could not destroy churn font"))
      status = 1;
  }

  if (paint != VG_INVALID_HANDLE) {
    vgDestroyPaint(paint);
    if (expect_vg_no_error("thread test could not destroy font churn paint"))
      status = 1;
  }

  return status;
}

static int exercise_mask_layer_object_churn(int workerId, int iterations)
{
  enum { MASK_SIZE = 16 };
  VGPath path = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  int i;
  int status = 0;

  thread_test_set_active_iteration(-1);
  thread_test_set_active_phase(THREAD_PHASE_MASK_RESOURCE_CREATE);
  path = create_rect_path();
  paint = vgCreatePaint();
  thread_test_set_active_phase(THREAD_PHASE_MASK_RESOURCE_VALIDATE);
  if (path == VG_INVALID_HANDLE ||
      paint == VG_INVALID_HANDLE ||
      expect_vg_no_error("thread test could not create mask churn resources")) {
    status = 1;
  }

  if (!status) {
    thread_test_set_active_phase(THREAD_PHASE_MASK_PAINT_INIT);
    vgSetColor(paint, 0xaa6633ffu);
    vgSetPaint(paint, VG_FILL_PATH);
    if (expect_vg_no_error("thread test could not initialize mask churn paint"))
      status = 1;
  }

  for (i = 0; !status && i < iterations; ++i) {
    VGMaskLayer layer = VG_INVALID_HANDLE;
    VGMaskLayer copy = VG_INVALID_HANDLE;
    VGint stripe = 1 + ((workerId + i) % (MASK_SIZE - 1));

    thread_test_set_active_iteration(i);
    thread_test_set_active_phase(THREAD_PHASE_MASK_LAYER_CREATE);
    layer = vgCreateMaskLayer(MASK_SIZE, MASK_SIZE);
    copy = vgCreateMaskLayer(MASK_SIZE, MASK_SIZE);
    if (layer == VG_INVALID_HANDLE ||
        copy == VG_INVALID_HANDLE ||
        expect_vg_no_error("thread test could not create churn mask layers")) {
      status = 1;
    }

    if (!status) {
      thread_test_set_active_phase(THREAD_PHASE_MASK_RESET);
      if (reset_test_state())
        status = 1;
    }

    if (!status) {
      thread_test_set_active_phase(THREAD_PHASE_MASK_ENABLE);
      vgSeti(VG_MASKING, VG_TRUE);
      thread_test_set_active_phase(THREAD_PHASE_MASK_FILL_SURFACE);
      vgMask(VG_INVALID_HANDLE, VG_FILL_MASK, 0, 0,
             MASK_SIZE, MASK_SIZE);
      thread_test_set_active_phase(THREAD_PHASE_MASK_FILL_LAYER_CLEAR);
      vgFillMaskLayer(layer, 0, 0, MASK_SIZE, MASK_SIZE, 0.0f);
      thread_test_set_active_phase(THREAD_PHASE_MASK_FILL_LAYER_STRIPE);
      vgFillMaskLayer(layer, 0, 0, stripe, MASK_SIZE, 1.0f);
      thread_test_set_active_phase(THREAD_PHASE_MASK_SET_FROM_LAYER);
      vgMask(layer, VG_SET_MASK, 0, 0, MASK_SIZE, MASK_SIZE);
      thread_test_set_active_phase(THREAD_PHASE_MASK_COPY);
      vgCopyMask(copy, 0, 0, 0, 0, MASK_SIZE, MASK_SIZE);
      thread_test_set_active_phase(THREAD_PHASE_MASK_CLEAR_SURFACE);
      vgMask(VG_INVALID_HANDLE, VG_CLEAR_MASK, 0, 0,
             MASK_SIZE, MASK_SIZE);
      thread_test_set_active_phase(THREAD_PHASE_MASK_UNION_COPY);
      vgMask(copy, VG_UNION_MASK, 0, 0, MASK_SIZE, MASK_SIZE);
      thread_test_set_active_phase(THREAD_PHASE_MASK_SELECT_PAINT);
      vgSetPaint(paint, VG_FILL_PATH);
      thread_test_set_active_phase(THREAD_PHASE_MASK_DRAW_PATH);
      vgDrawPath(path, VG_FILL_PATH);

      if ((i & 15) == 0) {
        thread_test_set_active_phase(THREAD_PHASE_MASK_FINISH);
        vgFinish();
      }

      thread_test_set_active_phase(THREAD_PHASE_MASK_ERROR_CHECK);
      if (expect_vg_no_error("thread test mask-layer churn failed"))
        status = 1;
    }

    thread_test_set_active_phase(THREAD_PHASE_MASK_DISABLE);
    vgSeti(VG_MASKING, VG_FALSE);
    if (expect_vg_no_error("thread test could not disable mask-layer churn masking"))
      status = 1;

    if (copy != VG_INVALID_HANDLE) {
      thread_test_set_active_phase(THREAD_PHASE_MASK_CLEANUP_COPY);
      vgDestroyMaskLayer(copy);
      if (expect_vg_no_error("thread test could not destroy churn mask copy"))
        status = 1;
    }

    if (layer != VG_INVALID_HANDLE) {
      thread_test_set_active_phase(THREAD_PHASE_MASK_CLEANUP_LAYER);
      vgDestroyMaskLayer(layer);
      if (expect_vg_no_error("thread test could not destroy churn mask layer"))
        status = 1;
    }
  }

  thread_test_set_active_iteration(-1);
  thread_test_set_active_phase(THREAD_PHASE_MASK_RESET_STATE);
  vgSeti(VG_MASKING, VG_FALSE);
  if (expect_vg_no_error("thread test could not reset mask-layer churn state"))
    status = 1;

  if (paint != VG_INVALID_HANDLE) {
    thread_test_set_active_phase(THREAD_PHASE_MASK_CLEANUP_PAINT);
    vgDestroyPaint(paint);
    if (expect_vg_no_error("thread test could not destroy mask churn paint"))
      status = 1;
  }

  if (path != VG_INVALID_HANDLE) {
    thread_test_set_active_phase(THREAD_PHASE_MASK_CLEANUP_PATH);
    vgDestroyPath(path);
    if (expect_vg_no_error("thread test could not destroy mask churn path"))
      status = 1;
  }

  thread_test_set_active_phase(THREAD_PHASE_MASK_DONE);
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

static int exercise_context_only_state(int workerId, int iterations)
{
  int i;

  for (i = 0; i < iterations; ++i) {
    VGfloat clear[4] = {
      ((VGfloat)((workerId + i) % 7)) / 6.0f,
      ((VGfloat)((workerId * 3 + i) % 11)) / 10.0f,
      ((VGfloat)((workerId * 5 + i) % 13)) / 12.0f,
      1.0f
    };
    VGfloat readClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    VGfloat matrix[9];
    VGint scissor[4] = {
      (workerId + i) % 8,
      (workerId * 2 + i) % 8,
      8,
      8
    };
    VGFillRule fillRule = (i & 1) ? VG_NON_ZERO : VG_EVEN_ODD;
    VGErrorCode error;

    vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
    vgLoadIdentity();
    vgTranslate((VGfloat)(workerId % 3), (VGfloat)(i % 5));
    vgScale(1.0f + (VGfloat)(workerId % 2) * 0.1f, 1.0f);
    vgRotate((VGfloat)(i % 360));
    vgShear(0.01f * (VGfloat)(workerId % 4), 0.0f);
    vgGetMatrix(matrix);

    vgSetfv(VG_CLEAR_COLOR, 4, clear);
    vgGetfv(VG_CLEAR_COLOR, 4, readClear);
    vgSeti(VG_FILL_RULE, fillRule);
    if ((VGFillRule)vgGeti(VG_FILL_RULE) != fillRule) {
      fprintf(stderr, "thread test context-only fill rule mismatch\n");
      return 1;
    }

    vgSetf(VG_STROKE_LINE_WIDTH, 1.0f + (VGfloat)(i % 5));
    if (vgGetf(VG_STROKE_LINE_WIDTH) <= 0.0f) {
      fprintf(stderr, "thread test context-only stroke width mismatch\n");
      return 1;
    }

    vgSetiv(VG_SCISSOR_RECTS, 4, scissor);
    if (vgGetVectorSize(VG_SCISSOR_RECTS) != 4) {
      fprintf(stderr, "thread test context-only vector size mismatch\n");
      return 1;
    }

    if (!vgGetString(VG_VENDOR)) {
      fprintf(stderr, "thread test context-only string query failed\n");
      return 1;
    }

    (void)vgHardwareQuery(VG_IMAGE_FORMAT_QUERY, VG_sRGBA_8888);
    if ((i & 15) == 0)
      vgFlush();
    if ((i & 63) == 0)
      vgFinish();

    error = vgGetError();
    if (error != VG_NO_ERROR) {
      fprintf(stderr,
              "thread test context-only operation failed (VG error 0x%04x)\n",
              error);
      return 1;
    }
  }

  vgFinish();
  return expect_vg_no_error("thread test context-only finish failed");
}

static void thread_test_worker_begin(const ThreadArgs *args)
{
  thread_test_set_active_churn_lane(args->churnLane);
  thread_test_set_active_paint_options(args->paintProfile,
                                       args->paintDrawMode);
  thread_test_set_active_worker(args->workerId);
  thread_test_set_active_iteration(-1);
  thread_test_set_active_phase(THREAD_PHASE_WORKER_START);
  thread_test_log(args->verbose,
                  "thread test begin churn lane %s worker %d iterations %d\n",
                  args->workerName ? args->workerName : "unknown",
                  args->workerId,
                  args->iterations);
  if (args->churnLane == THREAD_CHURN_PAINT)
    thread_test_log(args->verbose,
                    "thread test paint profile %s draw mode %s\n",
                    thread_named_value_name(threadPaintProfileValues,
                                            args->paintProfile),
                    thread_named_value_name(threadPaintDrawModeValues,
                                            args->paintDrawMode));
}

static void thread_test_worker_end(const ThreadArgs *args)
{
  thread_test_set_active_phase(THREAD_PHASE_WORKER_END);
  thread_test_log(args->verbose,
                  "thread test end churn lane %s worker %d status %d\n",
                  args->workerName ? args->workerName : "unknown",
                  args->workerId,
                  args->status);
  thread_test_set_active_churn_lane(THREAD_CHURN_ALL);
  thread_test_clear_active_detail();
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

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status) {
    for (i = 0; i < args->iterations; ++i) {
      if (run_shared_resource_iteration(args->workerId, i)) {
        args->status = 1;
        break;
      }
    }
  }

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *path_object_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_path_object_churn(args->workerId,
                                              args->iterations);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *paint_object_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_paint_object_churn(args->workerId,
                                               args->iterations,
                                               args->paintProfile,
                                               args->paintDrawMode);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *image_object_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_image_object_churn(args->workerId,
                                               args->iterations);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *font_object_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_font_object_churn(args->workerId,
                                              args->iterations);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *mask_layer_object_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_mask_layer_object_churn(args->workerId,
                                                    args->iterations);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *filter_object_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_filter_object_churn(args->workerId,
                                                args->iterations);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *context_only_churn_worker(void *arg)
{
  ThreadArgs *args = (ThreadArgs*)arg;
  TestEGL state = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };

  thread_test_worker_begin(args);
  state.ownsDisplay = 0;
  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_INIT);
  args->status = init_shared_egl(&state,
                                 args->display,
                                 args->config,
                                 args->context);
  thread_test_set_active_phase(THREAD_PHASE_CHURN_BODY);
  if (!args->status)
    args->status = exercise_context_only_state(args->workerId,
                                               args->iterations);

  thread_test_set_active_phase(THREAD_PHASE_CHURN_EGL_CLEANUP);
  if (state.display != EGL_NO_DISPLAY && cleanup_egl(&state))
    args->status = 1;

  thread_test_worker_end(args);
  return NULL;
}

static void *destroy_selected_paint_worker(void *arg)
{
  DestroyPaintArgs *args = (DestroyPaintArgs*)arg;
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
    vgDestroyPaint(args->paint);
    if (expect_vg_no_error("thread test could not destroy selected paint in peer"))
      args->status = 1;
    else
      args->destroyedPaint = 1;
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

typedef struct
{
  int lane;
  const char *name;
  ThreadWorkerFunc worker;
} ThreadChurnLane;

static const ThreadChurnLane threadChurnLanes[] = {
  {THREAD_CHURN_SHARED_RESOURCE,
   "shared-resource",
   shared_resource_churn_worker},
  {THREAD_CHURN_PATH, "path", path_object_churn_worker},
  {THREAD_CHURN_PAINT, "paint", paint_object_churn_worker},
  {THREAD_CHURN_IMAGE, "image", image_object_churn_worker},
  {THREAD_CHURN_FONT, "font", font_object_churn_worker},
  {THREAD_CHURN_MASK_LAYER, "mask-layer", mask_layer_object_churn_worker},
  {THREAD_CHURN_FILTER, "filter", filter_object_churn_worker},
  {THREAD_CHURN_CONTEXT_ONLY, "context-only", context_only_churn_worker},
  {0, NULL, NULL}
};

static int thread_churn_lane_enabled(const ThreadTestOptions *options,
                                     int lane)
{
  return (options->churnLane == THREAD_CHURN_ALL ||
          options->churnLane == lane) ? 1 : 0;
}

static int thread_churn_enabled_lane_count(const ThreadTestOptions *options)
{
  int count = 0;
  int i;

  for (i = 0; threadChurnLanes[i].name; ++i)
    if (thread_churn_lane_enabled(options, threadChurnLanes[i].lane))
      ++count;

  return count;
}

static int start_shared_churn_lane(const ThreadTestOptions *options,
                                   const ThreadChurnLane *lane,
                                   TestEGL *base,
                                   pthread_t *threads,
                                   ThreadArgs *args,
                                   int *created)
{
  int i;

  if (!thread_churn_lane_enabled(options, lane->lane))
    return 0;

  thread_test_set_active_churn_lane(lane->lane);
  thread_test_log(options->verbose,
                  "thread test schedule churn lane %s workers %d iterations %d\n",
                  lane->name,
                  options->sharedWorkers,
                  options->sharedIterations);

  for (i = 0; i < options->sharedWorkers; ++i) {
    args[*created].status = 1;
    args[*created].display = base->display;
    args[*created].config = base->config;
    args[*created].context = base->context;
    args[*created].workerId =
      options->sharedWorkers * (lane->lane - 1) + i + 1;
    args[*created].iterations = options->sharedIterations;
    args[*created].churnLane = lane->lane;
    args[*created].paintProfile = options->paintProfile;
    args[*created].paintDrawMode = options->paintDrawMode;
    args[*created].verbose = options->verbose;
    args[*created].workerName = lane->name;

    if (pthread_create(&threads[*created], NULL,
                       lane->worker,
                       &args[*created]) != 0) {
      fprintf(stderr, "thread test could not create %s churn worker\n",
              lane->name);
      return 1;
    }

    ++(*created);
  }

  return 0;
}

static int join_shared_churn_threads(pthread_t *threads,
                                     ThreadArgs *args,
                                     int created)
{
  int i;
  int status = 0;

  for (i = 0; i < created; ++i) {
    pthread_join(threads[i], NULL);
    if (args[i].status)
      status = 1;
  }

  return status;
}

static int run_shared_churn_lane_threads(const ThreadTestOptions *options,
                                         const ThreadChurnLane *lane,
                                         TestEGL *base)
{
  pthread_t *threads = NULL;
  ThreadArgs *args = NULL;
  int created = 0;
  int status = 0;

  threads = (pthread_t*)calloc((size_t)options->sharedWorkers,
                               sizeof(pthread_t));
  args = (ThreadArgs*)calloc((size_t)options->sharedWorkers,
                             sizeof(ThreadArgs));
  if (!threads || !args) {
    fprintf(stderr, "thread test could not allocate shared churn workers\n");
    free(threads);
    free(args);
    return 1;
  }

  if (start_shared_churn_lane(options,
                              lane,
                              base,
                              threads,
                              args,
                              &created))
    status = 1;

  if (join_shared_churn_threads(threads, args, created))
    status = 1;

  free(threads);
  free(args);
  thread_test_set_active_churn_lane(THREAD_CHURN_ALL);
  return status;
}

static int run_shared_churn_sequential(const ThreadTestOptions *options,
                                       TestEGL *base)
{
  int i;

  for (i = 0; threadChurnLanes[i].name; ++i) {
    if (!thread_churn_lane_enabled(options, threadChurnLanes[i].lane))
      continue;

    if (run_shared_churn_lane_threads(options, &threadChurnLanes[i], base))
      return 1;
  }

  return 0;
}

static int run_shared_churn_combined(const ThreadTestOptions *options,
                                     TestEGL *base)
{
  int threadCount = options->sharedWorkers *
                    thread_churn_enabled_lane_count(options);
  pthread_t *threads = NULL;
  ThreadArgs *args = NULL;
  int created = 0;
  int i;
  int status = 0;

  threads = (pthread_t*)calloc((size_t)threadCount,
                               sizeof(pthread_t));
  args = (ThreadArgs*)calloc((size_t)threadCount,
                             sizeof(ThreadArgs));
  if (!threads || !args) {
    fprintf(stderr, "thread test could not allocate shared churn workers\n");
    free(threads);
    free(args);
    return 1;
  }

  for (i = 0; threadChurnLanes[i].name; ++i) {
    if (start_shared_churn_lane(options,
                                &threadChurnLanes[i],
                                base,
                                threads,
                                args,
                                &created)) {
      status = 1;
      break;
    }
  }

  if (join_shared_churn_threads(threads, args, created))
    status = 1;

  thread_test_set_active_churn_lane(THREAD_CHURN_ALL);

  free(threads);
  free(args);
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
  int status;

  if (init_egl(&base, EGL_NO_CONTEXT))
    return 1;

  if (options->churnSchedule == THREAD_CHURN_SCHEDULE_COMBINED)
    status = run_shared_churn_combined(options, &base);
  else
    status = run_shared_churn_sequential(options, &base);

  if (cleanup_egl(&base))
    status = 1;

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

static int run_cross_thread_selected_paint_lifetime(void)
{
  TestEGL base = {
    EGL_NO_DISPLAY,
    0,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT,
    0
  };
  DestroyPaintArgs args;
  pthread_t thread;
  VGPath path = VG_INVALID_HANDLE;
  VGPaint paint = VG_INVALID_HANDLE;
  int status = 0;

  if (init_egl(&base, EGL_NO_CONTEXT))
    return 1;

  path = create_rect_path();
  paint = vgCreatePaint();
  if (path == VG_INVALID_HANDLE || paint == VG_INVALID_HANDLE) {
    status = fail_vg("thread test could not create selected paint resources");
  } else {
    vgSetColor(paint, 0x66aa44ffu);
    vgSetPaint(paint, VG_FILL_PATH);
    if (expect_vg_no_error("thread test could not select shared paint"))
      status = 1;
  }

  if (!status) {
    args.status = 1;
    args.display = base.display;
    args.config = base.config;
    args.context = base.context;
    args.paint = paint;
    args.destroyedPaint = 0;

    if (pthread_create(&thread, NULL,
                       destroy_selected_paint_worker, &args) != 0) {
      fprintf(stderr, "thread test could not create selected paint destroy worker\n");
      status = 1;
    } else {
      pthread_join(thread, NULL);
      if (args.destroyedPaint)
        paint = VG_INVALID_HANDLE;
      if (args.status)
        status = 1;
    }
  }

  if (!status) {
    VGPaint selected = vgGetPaint(VG_FILL_PATH);
    if (expect_vg_no_error("thread test could not query peer-destroyed selected paint")) {
      status = 1;
    } else if (selected != VG_INVALID_HANDLE) {
      fprintf(stderr, "thread test kept a public handle for peer-destroyed paint\n");
      status = 1;
    }
  }

  if (!status) {
    vgDrawPath(path, VG_FILL_PATH);
    vgFinish();
    if (expect_vg_no_error("thread test could not draw peer-destroyed selected paint"))
      status = 1;
  }

  vgSetPaint(VG_INVALID_HANDLE, VG_FILL_PATH);
  if (expect_vg_no_error("thread test could not release peer-destroyed selected paint"))
    status = 1;

  if (paint != VG_INVALID_HANDLE) {
    vgDestroyPaint(paint);
    if (expect_vg_no_error("thread test could not clean up selected paint"))
      status = 1;
  }

  if (path != VG_INVALID_HANDLE) {
    vgDestroyPath(path);
    if (expect_vg_no_error("thread test could not clean up selected paint path"))
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

typedef int (*ThreadTestCaseFunc)(const ThreadTestOptions *options);

static int run_case_concurrent_first_egl(const ThreadTestOptions *options)
{
  (void)options;
  return run_concurrent_first_egl();
}

static int run_case_display_canonicalization(const ThreadTestOptions *options)
{
  (void)options;
  return run_display_canonicalization();
}

static int run_case_display_stale_generation(const ThreadTestOptions *options)
{
  (void)options;
  return run_display_stale_generation();
}

static int run_case_same_thread_deferred_destroy(const ThreadTestOptions *options)
{
  (void)options;
  return run_same_thread_deferred_destroy();
}

static int run_case_release_thread_clears_current(const ThreadTestOptions *options)
{
  (void)options;
  return run_release_thread_clears_current();
}

static int run_case_release_thread_finalizes_deferred_destroy(
  const ThreadTestOptions *options)
{
  (void)options;
  return run_release_thread_finalizes_deferred_destroy();
}

static int run_case_cross_thread_teardown_conflicts(
  const ThreadTestOptions *options)
{
  (void)options;
  return run_cross_thread_teardown_conflicts();
}

static int run_case_warm_up_egl(const ThreadTestOptions *options)
{
  (void)options;
  return warm_up_egl();
}

static int run_case_independent_contexts(const ThreadTestOptions *options)
{
  (void)options;
  return run_independent_contexts();
}

static int run_case_shared_context_stress(const ThreadTestOptions *options)
{
  (void)options;
  return run_shared_context_stress();
}

static int run_case_shared_resource_churn(const ThreadTestOptions *options)
{
  return run_shared_resource_churn(options);
}

static int run_case_retained_resource_lifetime(const ThreadTestOptions *options)
{
  (void)options;
  return run_cross_thread_retained_resource_lifetime();
}

static int run_case_selected_paint_lifetime(const ThreadTestOptions *options)
{
  (void)options;
  return run_cross_thread_selected_paint_lifetime();
}

static int run_case_image_pbuffer_ref_race(const ThreadTestOptions *options)
{
  (void)options;
  return run_image_pbuffer_ref_race();
}

static int run_case_make_current_conflict(const ThreadTestOptions *options)
{
  (void)options;
  return run_make_current_conflict();
}

static int run_thread_test_case(const ThreadTestOptions *options,
                                int testCase,
                                ThreadTestCaseFunc func)
{
  const char *name;
  int status;

  if (options->testCase != THREAD_CASE_ALL &&
      options->testCase != testCase)
    return 0;

  name = thread_named_value_name(threadTestCaseValues, testCase);
  thread_test_set_active_case(testCase);
  thread_test_set_active_churn_lane(THREAD_CHURN_ALL);
  thread_test_log(options->verbose, "thread test begin case %s\n", name);
  status = func(options);
  thread_test_log(options->verbose,
                  "thread test end case %s status %d\n",
                  name,
                  status);
  thread_test_set_active_churn_lane(THREAD_CHURN_ALL);
  thread_test_set_active_case(THREAD_CASE_ALL);

  return status;
}

static int run_thread_safety_suite(const ThreadTestOptions *options)
{
  if (run_thread_test_case(options, THREAD_CASE_CONCURRENT_FIRST_EGL,
                           run_case_concurrent_first_egl))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_DISPLAY_CANONICALIZATION,
                           run_case_display_canonicalization))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_DISPLAY_STALE_GENERATION,
                           run_case_display_stale_generation))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_SAME_THREAD_DEFERRED_DESTROY,
                           run_case_same_thread_deferred_destroy))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_RELEASE_THREAD_CLEARS_CURRENT,
                           run_case_release_thread_clears_current))
    return 1;
  if (run_thread_test_case(
        options, THREAD_CASE_RELEASE_THREAD_FINALIZES_DEFERRED_DESTROY,
        run_case_release_thread_finalizes_deferred_destroy))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_CROSS_THREAD_TEARDOWN_CONFLICTS,
                           run_case_cross_thread_teardown_conflicts))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_WARM_UP_EGL,
                           run_case_warm_up_egl))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_INDEPENDENT_CONTEXTS,
                           run_case_independent_contexts))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_SHARED_CONTEXT_STRESS,
                           run_case_shared_context_stress))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_SHARED_RESOURCE_CHURN,
                           run_case_shared_resource_churn))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_RETAINED_RESOURCE_LIFETIME,
                           run_case_retained_resource_lifetime))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_SELECTED_PAINT_LIFETIME,
                           run_case_selected_paint_lifetime))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_IMAGE_PBUFFER_REF_RACE,
                           run_case_image_pbuffer_ref_race))
    return 1;
  if (run_thread_test_case(options, THREAD_CASE_MAKE_CURRENT_CONFLICT,
                           run_case_make_current_conflict))
    return 1;

  return 0;
}

int main(void)
{
  ThreadTestOptions options;
  int i;

  install_thread_test_crash_handler();
  XInitThreads();
  select_test_platform();

  if (parse_thread_test_options(&options))
    return EXIT_FAILURE;

  for (i = 0; i < options.repeat; ++i) {
    if (options.repeat > 1) {
      if (options.verbose) {
        fprintf(stderr, "EGL/OpenVG thread safety iteration %d/%d\n",
                i + 1,
                options.repeat);
        fflush(stderr);
      } else {
        printf("EGL/OpenVG thread safety iteration %d/%d\n",
               i + 1,
               options.repeat);
        fflush(stdout);
      }
    }

    if (run_thread_safety_suite(&options))
      return EXIT_FAILURE;
  }

  printf("EGL/OpenVG thread safety smoke test passed\n");
  return EXIT_SUCCESS;
}
