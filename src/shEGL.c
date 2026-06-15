/*
 * ShaderVG EGL frontend.
 *
 * This module exposes the EGL calls used by OpenVG clients. It delegates
 * native display, surface, and OpenGL context creation to the platform EGL
 * provider, while associating ShaderVG VGContext state with contexts that
 * clients create after eglBindAPI(EGL_OPENVG_API).
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <VG/openvg.h>
#include "shContext.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
#  ifdef EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR
#    define EGL_CONTEXT_OPENGL_PROFILE_MASK EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR
#  endif
#endif

#ifndef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
#  ifdef EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR
#    define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR
#  endif
#endif

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

#if defined(_MSC_VER)
#  define SH_TLS __declspec(thread)
#else
#  define SH_TLS __thread
#endif

#define SH_EGL_DISPLAY_MAGIC 0x53454744u
#define SH_EGL_CONTEXT_MAGIC 0x53454743u
#define SH_EGL_SURFACE_MAGIC 0x53454753u
#define SH_EGL_ALPHA_MASK_SIZE 8

typedef enum {
  SH_EGL_SURFACE_PLATFORM = 0,
  SH_EGL_SURFACE_OPENVG_IMAGE = 1
} SHEGLSurfaceType;

typedef struct SHEGLDisplay SHEGLDisplay;

struct SHEGLDisplay {
  unsigned int magic;
  EGLNativeDisplayType nativeDisplay;
  EGLDisplay realDisplay;
  EGLBoolean initialized;
  SHuint generation;
  SHint currentCount;
  SHEGLDisplay *next;
};

typedef struct {
  unsigned int magic;
  SHEGLDisplay *display;
  EGLSurface realSurface;
  SHEGLSurfaceType type;
  EGLConfig config;
  SHImage *vgImage;
  VGContext *vgContext;
  GLuint framebuffer;
  GLuint stencil;
  EGLint width;
  EGLint height;
  EGLint textureFormat;
  EGLint textureTarget;
  EGLint mipmapTexture;
  EGLint vgColorspace;
  EGLint vgAlphaFormat;
  EGLBoolean destroyRequested;
  EGLBoolean realDestroyed;
  SHint currentCount;
  SHThreadId currentThread;
  SHuint displayGeneration;
} SHEGLSurface;

typedef struct {
  unsigned int magic;
  SHEGLDisplay *display;
  EGLContext realContext;
  VGContext *vgContext;
  EGLenum api;
  EGLBoolean destroyRequested;
  EGLBoolean realDestroyed;
  SHint currentCount;
  SHThreadId currentThread;
  SHuint displayGeneration;
} SHEGLContext;

typedef struct {
  PFNEGLGETDISPLAYPROC GetDisplay;
  PFNEGLINITIALIZEPROC Initialize;
  PFNEGLTERMINATEPROC Terminate;
  PFNEGLGETCONFIGSPROC GetConfigs;
  PFNEGLCHOOSECONFIGPROC ChooseConfig;
  PFNEGLGETCONFIGATTRIBPROC GetConfigAttrib;
  PFNEGLCREATEWINDOWSURFACEPROC CreateWindowSurface;
  PFNEGLCREATEPBUFFERSURFACEPROC CreatePbufferSurface;
  PFNEGLDESTROYSURFACEPROC DestroySurface;
  PFNEGLQUERYSURFACEPROC QuerySurface;
  PFNEGLBINDAPIPROC BindAPI;
  PFNEGLQUERYAPIPROC QueryAPI;
  PFNEGLCREATECONTEXTPROC CreateContext;
  PFNEGLDESTROYCONTEXTPROC DestroyContext;
  PFNEGLMAKECURRENTPROC MakeCurrent;
  PFNEGLGETCURRENTCONTEXTPROC GetCurrentContext;
  PFNEGLGETCURRENTSURFACEPROC GetCurrentSurface;
  PFNEGLGETCURRENTDISPLAYPROC GetCurrentDisplay;
  PFNEGLSWAPBUFFERSPROC SwapBuffers;
  PFNEGLSWAPINTERVALPROC SwapInterval;
  PFNEGLGETERRORPROC GetError;
  PFNEGLQUERYSTRINGPROC QueryString;
  PFNEGLGETPROCADDRESSPROC GetProcAddress;
  PFNEGLRELEASETHREADPROC ReleaseThread;
} SHEGLDispatch;

static SHEGLDispatch g_egl;
static SHOnce g_eglLoadOnce = SH_ONCE_INIT;
static int g_eglLoadSuccess = 0;
static EGLint g_eglLoadError = EGL_SUCCESS;
static SHOnce g_eglMutexOnce = SH_ONCE_INIT;
static SHRecursiveMutex g_eglMutex;
static SHEGLDisplay *g_displays = NULL;

#if defined(_WIN32)
static HMODULE g_eglLibrary = NULL;
#else
static void *g_eglLibrary = NULL;
#endif

static SH_TLS EGLint t_error = EGL_SUCCESS;
static SH_TLS EGLenum t_boundApi = EGL_OPENGL_ES_API;
static SH_TLS SHEGLDisplay *t_currentDisplay = NULL;
static SH_TLS SHEGLSurface *t_currentDraw = NULL;
static SH_TLS SHEGLSurface *t_currentRead = NULL;
static SH_TLS SHEGLContext *t_currentContext = NULL;
static SH_TLS char t_clientApis[256];

typedef struct {
  EGLBoolean locked;
} SHEGLLockGuard;

static void shInitEGLMutex(void)
{
  shRecursiveMutexInit(&g_eglMutex);
}

static void shLockEGL(void)
{
  shOnce(&g_eglMutexOnce, shInitEGLMutex);
  shRecursiveMutexLock(&g_eglMutex);
}

static void shUnlockEGL(void)
{
  shRecursiveMutexUnlock(&g_eglMutex);
}

static void shEGLCallLock(SHEGLLockGuard *guard)
{
  guard->locked = EGL_FALSE;
  shLockEGL();
  guard->locked = EGL_TRUE;
}

static void shEGLCallCleanup(SHEGLLockGuard *guard)
{
  if (guard && guard->locked) {
    guard->locked = EGL_FALSE;
    shUnlockEGL();
  }
}

#define SH_EGL_LOCK_GUARD() \
  SHEGLLockGuard shEGLCallGuard; \
  shEGLCallLock(&shEGLCallGuard)

#define SH_EGL_RETURN(value) \
  do { \
    shEGLCallCleanup(&shEGLCallGuard); \
    return (value); \
  } while (0)

static void shSetEGLError(EGLint error)
{
  t_error = error;
}

static void *shLoadSymbol(const char *name)
{
#if defined(_WIN32)
  return (void*)GetProcAddress(g_eglLibrary, name);
#else
  return dlsym(g_eglLibrary, name);
#endif
}

static void shLoadRealEGLOnce(void)
{
#if defined(_WIN32)
  g_eglLibrary = LoadLibraryA("libEGL.dll");
  if (!g_eglLibrary)
    g_eglLibrary = LoadLibraryA("EGL.dll");
#else
  g_eglLibrary = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
  if (!g_eglLibrary)
    g_eglLibrary = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
#endif

  if (!g_eglLibrary) {
    g_eglLoadError = EGL_NOT_INITIALIZED;
    return;
  }

#define LOAD_EGL(field, type, symbol) \
  do { \
    union { void *object; type function; } proc; \
    proc.object = shLoadSymbol(symbol); \
    if (!proc.object) { \
      g_eglLoadError = EGL_NOT_INITIALIZED; \
      return; \
    } \
    g_egl.field = proc.function; \
  } while (0)

  LOAD_EGL(GetDisplay, PFNEGLGETDISPLAYPROC, "eglGetDisplay");
  LOAD_EGL(Initialize, PFNEGLINITIALIZEPROC, "eglInitialize");
  LOAD_EGL(Terminate, PFNEGLTERMINATEPROC, "eglTerminate");
  LOAD_EGL(GetConfigs, PFNEGLGETCONFIGSPROC, "eglGetConfigs");
  LOAD_EGL(ChooseConfig, PFNEGLCHOOSECONFIGPROC, "eglChooseConfig");
  LOAD_EGL(GetConfigAttrib, PFNEGLGETCONFIGATTRIBPROC, "eglGetConfigAttrib");
  LOAD_EGL(CreateWindowSurface, PFNEGLCREATEWINDOWSURFACEPROC, "eglCreateWindowSurface");
  LOAD_EGL(CreatePbufferSurface, PFNEGLCREATEPBUFFERSURFACEPROC, "eglCreatePbufferSurface");
  LOAD_EGL(DestroySurface, PFNEGLDESTROYSURFACEPROC, "eglDestroySurface");
  LOAD_EGL(QuerySurface, PFNEGLQUERYSURFACEPROC, "eglQuerySurface");
  LOAD_EGL(BindAPI, PFNEGLBINDAPIPROC, "eglBindAPI");
  LOAD_EGL(QueryAPI, PFNEGLQUERYAPIPROC, "eglQueryAPI");
  LOAD_EGL(CreateContext, PFNEGLCREATECONTEXTPROC, "eglCreateContext");
  LOAD_EGL(DestroyContext, PFNEGLDESTROYCONTEXTPROC, "eglDestroyContext");
  LOAD_EGL(MakeCurrent, PFNEGLMAKECURRENTPROC, "eglMakeCurrent");
  LOAD_EGL(GetCurrentContext, PFNEGLGETCURRENTCONTEXTPROC, "eglGetCurrentContext");
  LOAD_EGL(GetCurrentSurface, PFNEGLGETCURRENTSURFACEPROC, "eglGetCurrentSurface");
  LOAD_EGL(GetCurrentDisplay, PFNEGLGETCURRENTDISPLAYPROC, "eglGetCurrentDisplay");
  LOAD_EGL(SwapBuffers, PFNEGLSWAPBUFFERSPROC, "eglSwapBuffers");
  LOAD_EGL(GetError, PFNEGLGETERRORPROC, "eglGetError");
  LOAD_EGL(QueryString, PFNEGLQUERYSTRINGPROC, "eglQueryString");
  LOAD_EGL(GetProcAddress, PFNEGLGETPROCADDRESSPROC, "eglGetProcAddress");
  LOAD_EGL(ReleaseThread, PFNEGLRELEASETHREADPROC, "eglReleaseThread");

  {
    union { void *object; PFNEGLSWAPINTERVALPROC function; } proc;
    proc.object = shLoadSymbol("eglSwapInterval");
    g_egl.SwapInterval = proc.function;
  }

#undef LOAD_EGL

  g_eglLoadSuccess = 1;
  g_eglLoadError = EGL_SUCCESS;
}

static int shLoadRealEGL(void)
{
  shOnce(&g_eglLoadOnce, shLoadRealEGLOnce);
  if (!g_eglLoadSuccess) {
    shSetEGLError(g_eglLoadError != EGL_SUCCESS ?
                  g_eglLoadError : EGL_NOT_INITIALIZED);
    return 0;
  }
  return 1;
}

static SHEGLDisplay *shDisplay(EGLDisplay dpy)
{
  SHEGLDisplay *display = (SHEGLDisplay*)dpy;
  if (!display || display->magic != SH_EGL_DISPLAY_MAGIC) {
    shSetEGLError(EGL_BAD_DISPLAY);
    return NULL;
  }
  return display;
}

static SHuint shNextDisplayGeneration(SHuint generation)
{
  ++generation;
  if (generation == 0)
    ++generation;
  return generation;
}

static SHEGLDisplay *shFindDisplay(EGLDisplay realDisplay)
{
  SHEGLDisplay *display;

  for (display = g_displays; display; display = display->next) {
    if (display->realDisplay == realDisplay)
      return display;
  }

  return NULL;
}

static SHEGLSurface *shSurface(EGLSurface surface)
{
  SHEGLSurface *s = (SHEGLSurface*)surface;
  if (!s || s->magic != SH_EGL_SURFACE_MAGIC) {
    shSetEGLError(EGL_BAD_SURFACE);
    return NULL;
  }

  if (!s->display ||
      s->display->magic != SH_EGL_DISPLAY_MAGIC ||
      s->display->generation != s->displayGeneration) {
    shSetEGLError(EGL_BAD_SURFACE);
    return NULL;
  }

  return s;
}

static SHEGLContext *shContext(EGLContext context)
{
  SHEGLContext *c = (SHEGLContext*)context;
  if (!c || c->magic != SH_EGL_CONTEXT_MAGIC) {
    shSetEGLError(EGL_BAD_CONTEXT);
    return NULL;
  }

  if (!c->display ||
      c->display->magic != SH_EGL_DISPLAY_MAGIC ||
      c->display->generation != c->displayGeneration) {
    shSetEGLError(EGL_BAD_CONTEXT);
    return NULL;
  }

  return c;
}

static void shDestroyImageSurfaceResources(SHEGLSurface *surface);

static EGLBoolean shThreadOwnsCurrent(SHint currentCount,
                                      SHThreadId currentThread)
{
  return currentCount > 0 &&
         shThreadIdEqual(currentThread, shThreadCurrentId());
}

static EGLBoolean shCurrentInOtherThread(SHint currentCount,
                                         SHThreadId currentThread)
{
  return currentCount > 0 &&
         !shThreadIdEqual(currentThread, shThreadCurrentId());
}

static EGLBoolean shContextCanBeMadeCurrent(SHEGLContext *context)
{
  if (!context)
    return EGL_TRUE;

  if (context->destroyRequested) {
    shSetEGLError(EGL_BAD_CONTEXT);
    return EGL_FALSE;
  }

  if (shCurrentInOtherThread(context->currentCount,
                             context->currentThread)) {
    shSetEGLError(EGL_BAD_ACCESS);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

static EGLBoolean shSurfaceCanBeMadeCurrent(SHEGLSurface *surface)
{
  if (!surface)
    return EGL_TRUE;

  if (surface->destroyRequested) {
    shSetEGLError(EGL_BAD_SURFACE);
    return EGL_FALSE;
  }

  if (shCurrentInOtherThread(surface->currentCount,
                             surface->currentThread)) {
    shSetEGLError(EGL_BAD_ACCESS);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

static void shMarkContextCurrent(SHEGLContext *context)
{
  if (!context)
    return;

  context->currentCount = 1;
  context->currentThread = shThreadCurrentId();
}

static void shUnmarkContextCurrent(SHEGLContext *context)
{
  if (!context)
    return;

  context->currentCount = 0;
  context->currentThread = shThreadInvalidId();
}

static void shMarkSurfaceCurrent(SHEGLSurface *surface)
{
  if (!surface)
    return;

  surface->currentCount = 1;
  surface->currentThread = shThreadCurrentId();
}

static void shMarkDisplayCurrent(SHEGLDisplay *display)
{
  if (display)
    ++display->currentCount;
}

static void shUnmarkDisplayCurrent(SHEGLDisplay *display)
{
  if (display && display->currentCount > 0)
    --display->currentCount;
}

static void shUnmarkSurfaceCurrent(SHEGLSurface *surface)
{
  if (!surface)
    return;

  surface->currentCount = 0;
  surface->currentThread = shThreadInvalidId();
}

static void shFinalizeDestroyedContext(SHEGLContext *context)
{
  if (!context ||
      !context->destroyRequested ||
      context->currentCount > 0)
    return;

  if (context->vgContext) {
    shDestroyContext(context->vgContext);
    context->vgContext = NULL;
  }

  context->magic = 0;
  free(context);
}

static void shFinalizeDestroyedSurface(SHEGLSurface *surface)
{
  if (!surface ||
      !surface->destroyRequested ||
      surface->currentCount > 0)
    return;

  if (surface->type == SH_EGL_SURFACE_OPENVG_IMAGE)
    shDestroyImageSurfaceResources(surface);

  surface->magic = 0;
  free(surface);
}

static EGLint shChannelBits(SHuint32 mask, SHuint8 max)
{
  EGLint bits = 0;

  if (mask == 0)
    return 0;

  while (max != 0) {
    ++bits;
    max >>= 1;
  }

  return bits;
}

static EGLint shImageVGColorspace(SHImage *image)
{
  EGLint base = image->fd.vgformat & 0x1F;

  return (base == VG_lRGBX_8888 ||
          base == VG_lRGBA_8888 ||
          base == VG_lRGBA_8888_PRE ||
          base == VG_lL_8) ?
         EGL_VG_COLORSPACE_LINEAR :
         EGL_VG_COLORSPACE_sRGB;
}

static EGLint shImageVGAlphaFormat(SHImage *image)
{
  EGLint base = image->fd.vgformat & 0x1F;

  return (base == VG_sRGBA_8888_PRE ||
          base == VG_lRGBA_8888_PRE) ?
         EGL_VG_ALPHA_FORMAT_PRE :
         EGL_VG_ALPHA_FORMAT_NONPRE;
}

static EGLBoolean shQueryConfigInt(SHEGLDisplay *display,
                                   EGLConfig config,
                                   EGLint attribute,
                                   EGLint *value)
{
  if (!g_egl.GetConfigAttrib(display->realDisplay, config, attribute, value)) {
    shSetEGLError(EGL_BAD_CONFIG);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

static EGLBoolean shConfigMatchesImage(SHEGLDisplay *display,
                                       EGLConfig config,
                                       SHImage *image)
{
  EGLint configRed, configGreen, configBlue, configAlpha;
  EGLint imageRed, imageGreen, imageBlue, imageAlpha;
  EGLint baseFormat = image->fd.vgformat & 0x1F;

  if (baseFormat == VG_sL_8 ||
      baseFormat == VG_lL_8 ||
      baseFormat == VG_A_8 ||
      baseFormat == VG_A_1 ||
      baseFormat == VG_A_4 ||
      baseFormat == VG_BW_1) {
    shSetEGLError(EGL_BAD_MATCH);
    return EGL_FALSE;
  }

  if (!shQueryConfigInt(display, config, EGL_RED_SIZE, &configRed) ||
      !shQueryConfigInt(display, config, EGL_GREEN_SIZE, &configGreen) ||
      !shQueryConfigInt(display, config, EGL_BLUE_SIZE, &configBlue) ||
      !shQueryConfigInt(display, config, EGL_ALPHA_SIZE, &configAlpha))
    return EGL_FALSE;

  imageRed = shChannelBits(image->fd.rmask, image->fd.rmax);
  imageGreen = shChannelBits(image->fd.gmask, image->fd.gmax);
  imageBlue = shChannelBits(image->fd.bmask, image->fd.bmax);
  imageAlpha = shChannelBits(image->fd.amask, image->fd.amax);

  if (configRed != imageRed ||
      configGreen != imageGreen ||
      configBlue != imageBlue ||
      configAlpha != imageAlpha) {
    shSetEGLError(EGL_BAD_MATCH);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

static EGLBoolean shParseClientPbufferAttribs(const EGLint *attribs,
                                              EGLint *textureFormat,
                                              EGLint *textureTarget,
                                              EGLint *mipmapTexture)
{
  int i;

  *textureFormat = EGL_NO_TEXTURE;
  *textureTarget = EGL_NO_TEXTURE;
  *mipmapTexture = EGL_FALSE;

  if (!attribs)
    return EGL_TRUE;

  for (i = 0; attribs[i] != EGL_NONE; i += 2) {
    EGLint attribute = attribs[i];
    EGLint value = attribs[i + 1];

    switch (attribute) {
    case EGL_TEXTURE_FORMAT:
      if (value != EGL_NO_TEXTURE &&
          value != EGL_TEXTURE_RGB &&
          value != EGL_TEXTURE_RGBA) {
        shSetEGLError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
      }
      *textureFormat = value;
      break;
    case EGL_TEXTURE_TARGET:
      if (value != EGL_NO_TEXTURE &&
          value != EGL_TEXTURE_2D) {
        shSetEGLError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
      }
      *textureTarget = value;
      break;
    case EGL_MIPMAP_TEXTURE:
      if (value != EGL_FALSE && value != EGL_TRUE) {
        shSetEGLError(EGL_BAD_ATTRIBUTE);
        return EGL_FALSE;
      }
      *mipmapTexture = value;
      break;
    default:
      shSetEGLError(EGL_BAD_ATTRIBUTE);
      return EGL_FALSE;
    }
  }

  if (*textureFormat != EGL_NO_TEXTURE ||
      *textureTarget != EGL_NO_TEXTURE ||
      *mipmapTexture != EGL_FALSE) {
    shSetEGLError(EGL_BAD_MATCH);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

static EGLBoolean shCreateImageFramebuffer(SHImage *image,
                                           GLuint *framebuffer,
                                           GLuint *stencil)
{
  GLint previousFramebuffer;
  GLint previousRenderbuffer;
  GLint previousTexture;
  GLint previousDrawBuffer;
  GLint previousReadBuffer;
  GLenum status;

  *framebuffer = 0;
  *stencil = 0;

  if (!image || image->texture == 0)
    return EGL_FALSE;

  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);
  glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);

  glGenFramebuffers(1, framebuffer);
  glGenRenderbuffers(1, stencil);
  if (*framebuffer == 0 || *stencil == 0)
    goto fail;

  glBindFramebuffer(GL_FRAMEBUFFER, *framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, image->texture, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, *stencil);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                        image->width, image->height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, *stencil);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE)
    goto fail;

  glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, previousRenderbuffer);
  glBindTexture(GL_TEXTURE_2D, previousTexture);
  glDrawBuffer(previousDrawBuffer);
  glReadBuffer(previousReadBuffer);

  return EGL_TRUE;

fail:
  if (*framebuffer != 0) {
    glDeleteFramebuffers(1, framebuffer);
    *framebuffer = 0;
  }
  if (*stencil != 0) {
    glDeleteRenderbuffers(1, stencil);
    *stencil = 0;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, previousFramebuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, previousRenderbuffer);
  glBindTexture(GL_TEXTURE_2D, previousTexture);
  glDrawBuffer(previousDrawBuffer);
  glReadBuffer(previousReadBuffer);
  glGetError();
  return EGL_FALSE;
}

static void shDestroyImageSurfaceResources(SHEGLSurface *surface)
{
  if (surface->framebuffer != 0) {
    glDeleteFramebuffers(1, &surface->framebuffer);
    surface->framebuffer = 0;
  }

  if (surface->stencil != 0) {
    glDeleteRenderbuffers(1, &surface->stencil);
    surface->stencil = 0;
  }

  if (surface->vgImage) {
    shImageReleaseEGLPbufferRef(surface->vgImage);
    surface->vgImage = NULL;
  }
}

static EGLBoolean shNormalizeImageSurface(SHEGLSurface *surface)
{
  if (!surface || surface->type != SH_EGL_SURFACE_OPENVG_IMAGE)
    return EGL_TRUE;

  if (!shImageNormalizeSurfaceData(surface->vgContext,
                                   surface->vgImage)) {
    shSetEGLError(EGL_BAD_ALLOC);
    return EGL_FALSE;
  }

  return EGL_TRUE;
}

static EGLint *shTranslateConfigAttribs(const EGLint *attribs,
                                        EGLBoolean *emptyResult)
{
  EGLint *out;
  int pairs = 0;
  int foundRenderable = 0;
  int i;
  int j = 0;

  if (emptyResult)
    *emptyResult = EGL_FALSE;

  if (attribs) {
    while (attribs[pairs * 2] != EGL_NONE)
      ++pairs;
  }

  out = (EGLint*)malloc(sizeof(EGLint) * (pairs * 2 + 3));
  if (!out)
    return NULL;

  for (i = 0; i < pairs; ++i) {
    EGLint key = attribs[i * 2];
    EGLint value = attribs[i * 2 + 1];

    if (key == EGL_ALPHA_MASK_SIZE) {
      if (emptyResult &&
          value != EGL_DONT_CARE &&
          value > SH_EGL_ALPHA_MASK_SIZE)
        *emptyResult = EGL_TRUE;
      continue;
    }

    out[j++] = key;
    if (key == EGL_RENDERABLE_TYPE) {
      foundRenderable = 1;
      value = (value & ~EGL_OPENVG_BIT) | EGL_OPENGL_BIT;
    } else if (key == EGL_CONFORMANT) {
      value = (value & ~EGL_OPENVG_BIT) | EGL_OPENGL_BIT;
    }
    out[j++] = value;
  }

  if (!foundRenderable) {
    out[j++] = EGL_RENDERABLE_TYPE;
    out[j++] = EGL_OPENGL_BIT;
  }

  out[j] = EGL_NONE;
  return out;
}

static const EGLint *shContextAttribsForAPI(EGLenum api, const EGLint *attribs)
{
  static const EGLint openGL33Attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3,
    EGL_CONTEXT_MINOR_VERSION, 3,
#if defined(EGL_CONTEXT_OPENGL_PROFILE_MASK) && defined(EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
#endif
    EGL_NONE
  };

  if (api == EGL_OPENVG_API)
    return openGL33Attribs;

  return attribs;
}

static EGLBoolean shQuerySurfaceSize(SHEGLDisplay *display, SHEGLSurface *surface,
                                     EGLint *width, EGLint *height)
{
  if (!surface) {
    *width = 0;
    *height = 0;
    return EGL_TRUE;
  }

  if (!g_egl.QuerySurface(display->realDisplay, surface->realSurface, EGL_WIDTH, width))
    return EGL_FALSE;

  if (!g_egl.QuerySurface(display->realDisplay, surface->realSurface, EGL_HEIGHT, height))
    return EGL_FALSE;

  return EGL_TRUE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id)
{
  SHEGLDisplay *display;
  EGLDisplay realDisplay;

  if (!shLoadRealEGL())
    return EGL_NO_DISPLAY;
  SH_EGL_LOCK_GUARD();

  realDisplay = g_egl.GetDisplay(display_id);
  if (realDisplay == EGL_NO_DISPLAY)
    SH_EGL_RETURN(EGL_NO_DISPLAY);

  display = shFindDisplay(realDisplay);
  if (display)
    SH_EGL_RETURN((EGLDisplay)display);

  display = (SHEGLDisplay*)calloc(1, sizeof(SHEGLDisplay));
  if (!display) {
    shSetEGLError(EGL_BAD_ALLOC);
    SH_EGL_RETURN(EGL_NO_DISPLAY);
  }

  display->magic = SH_EGL_DISPLAY_MAGIC;
  display->nativeDisplay = display_id;
  display->realDisplay = realDisplay;
  display->generation = 1;
  display->next = g_displays;
  g_displays = display;
  SH_EGL_RETURN((EGLDisplay)display);
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
  SHEGLDisplay *display;
  EGLBoolean result;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_FALSE);

  result = g_egl.Initialize(display->realDisplay, major, minor);
  if (result)
    display->initialized = EGL_TRUE;
  SH_EGL_RETURN(result);
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
  SHEGLDisplay *display;
  EGLBoolean result;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_FALSE);
  if (!display->initialized) {
    shSetEGLError(EGL_NOT_INITIALIZED);
    SH_EGL_RETURN(EGL_FALSE);
  }
  if (display->currentCount > 0) {
    shSetEGLError(EGL_BAD_ACCESS);
    SH_EGL_RETURN(EGL_FALSE);
  }
  result = g_egl.Terminate(display->realDisplay);
  if (result) {
    display->initialized = EGL_FALSE;
    display->generation = shNextDisplayGeneration(display->generation);
  }
  SH_EGL_RETURN(result);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                            EGLint config_size, EGLint *num_config)
{
  SHEGLDisplay *display;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  SH_EGL_RETURN(display ? g_egl.GetConfigs(display->realDisplay, configs, config_size, num_config) : EGL_FALSE);
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                              EGLConfig *configs, EGLint config_size,
                                              EGLint *num_config)
{
  SHEGLDisplay *display;
  EGLint *translated;
  EGLBoolean emptyResult = EGL_FALSE;
  EGLBoolean result;
  EGLint realCount = 0;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_FALSE);

  if (!num_config) {
    shSetEGLError(EGL_BAD_PARAMETER);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (config_size < 0) {
    shSetEGLError(EGL_BAD_PARAMETER);
    SH_EGL_RETURN(EGL_FALSE);
  }

  translated = shTranslateConfigAttribs(attrib_list, &emptyResult);
  if (!translated) {
    shSetEGLError(EGL_BAD_ALLOC);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (emptyResult) {
    result = g_egl.ChooseConfig(display->realDisplay, translated,
                                NULL, 0, &realCount);
    if (result)
      *num_config = 0;
  } else {
    result = g_egl.ChooseConfig(display->realDisplay, translated,
                                configs, config_size, num_config);
  }

  free(translated);
  SH_EGL_RETURN(result);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                 EGLint attribute, EGLint *value)
{
  SHEGLDisplay *display;
  EGLBoolean result;
  EGLint renderable;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_FALSE);

  if (attribute == EGL_ALPHA_MASK_SIZE) {
    if (!value) {
      shSetEGLError(EGL_BAD_PARAMETER);
      SH_EGL_RETURN(EGL_FALSE);
    }

    result = g_egl.GetConfigAttrib(display->realDisplay, config,
                                   EGL_RENDERABLE_TYPE, &renderable);
    if (!result)
      SH_EGL_RETURN(EGL_FALSE);

    if (renderable & EGL_OPENGL_BIT) {
      *value = SH_EGL_ALPHA_MASK_SIZE;
      SH_EGL_RETURN(EGL_TRUE);
    }
  }

  result = g_egl.GetConfigAttrib(display->realDisplay, config, attribute, value);
  if (result &&
      (attribute == EGL_RENDERABLE_TYPE ||
       attribute == EGL_CONFORMANT) &&
      value &&
      (*value & EGL_OPENGL_BIT))
    *value |= EGL_OPENVG_BIT;
  SH_EGL_RETURN(result);
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
  EGLenum realApi = api;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();

  if (api == EGL_OPENVG_API)
    realApi = EGL_OPENGL_API;

  if (!g_egl.BindAPI(realApi))
    SH_EGL_RETURN(EGL_FALSE);

  t_boundApi = api;
  SH_EGL_RETURN(EGL_TRUE);
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
  if (t_boundApi == EGL_OPENVG_API)
    return EGL_OPENVG_API;

  if (!shLoadRealEGL())
    return EGL_NONE;
  SH_EGL_LOCK_GUARD();

  SH_EGL_RETURN(g_egl.QueryAPI());
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativeWindowType win,
                                                     const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLSurface *surface;
  EGLSurface realSurface;

  if (!shLoadRealEGL())
    return EGL_NO_SURFACE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_NO_SURFACE);

  realSurface = g_egl.CreateWindowSurface(display->realDisplay, config, win, attrib_list);
  if (realSurface == EGL_NO_SURFACE)
    SH_EGL_RETURN(EGL_NO_SURFACE);

  surface = (SHEGLSurface*)calloc(1, sizeof(SHEGLSurface));
  if (!surface) {
    g_egl.DestroySurface(display->realDisplay, realSurface);
    shSetEGLError(EGL_BAD_ALLOC);
    SH_EGL_RETURN(EGL_NO_SURFACE);
  }

  surface->magic = SH_EGL_SURFACE_MAGIC;
  surface->display = display;
  surface->displayGeneration = display->generation;
  surface->realSurface = realSurface;
  surface->type = SH_EGL_SURFACE_PLATFORM;
  surface->config = config;
  SH_EGL_RETURN((EGLSurface)surface);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                      const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLSurface *surface;
  EGLSurface realSurface;

  if (!shLoadRealEGL())
    return EGL_NO_SURFACE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_NO_SURFACE);

  realSurface = g_egl.CreatePbufferSurface(display->realDisplay, config, attrib_list);
  if (realSurface == EGL_NO_SURFACE)
    SH_EGL_RETURN(EGL_NO_SURFACE);

  surface = (SHEGLSurface*)calloc(1, sizeof(SHEGLSurface));
  if (!surface) {
    g_egl.DestroySurface(display->realDisplay, realSurface);
    shSetEGLError(EGL_BAD_ALLOC);
    SH_EGL_RETURN(EGL_NO_SURFACE);
  }

  surface->magic = SH_EGL_SURFACE_MAGIC;
  surface->display = display;
  surface->displayGeneration = display->generation;
  surface->realSurface = realSurface;
  surface->type = SH_EGL_SURFACE_PLATFORM;
  surface->config = config;
  SH_EGL_RETURN((EGLSurface)surface);
}

EGLAPI EGLSurface EGLAPIENTRY
eglCreatePbufferFromClientBuffer(EGLDisplay dpy,
                                 EGLenum buftype,
                                 EGLClientBuffer buffer,
                                 EGLConfig config,
                                 const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLSurface *surface = NULL;
  SHImage *image;
  VGHandle imageHandle;
  EGLSurface realSurface;
  EGLint textureFormat;
  EGLint textureTarget;
  EGLint mipmapTexture;
  EGLint hiddenAttribs[5];
  GLuint framebuffer = 0;
  GLuint stencil = 0;

  if (!shLoadRealEGL())
    return EGL_NO_SURFACE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_NO_SURFACE);

  if (buftype != EGL_OPENVG_IMAGE) {
    shSetEGLError(EGL_BAD_PARAMETER);
    SH_EGL_RETURN(EGL_NO_SURFACE);
  }

  if (!t_currentContext ||
      t_currentContext->api != EGL_OPENVG_API ||
      !t_currentContext->vgContext) {
    shSetEGLError(EGL_BAD_ACCESS);
    SH_EGL_RETURN(EGL_NO_SURFACE);
  }

  if (t_currentContext->display != display) {
    shSetEGLError(EGL_BAD_ACCESS);
    SH_EGL_RETURN(EGL_NO_SURFACE);
  }

  {
    SHContextLock vgLock;
    VGContext *vgContext = shAcquireCurrentContext(&vgLock);

    if (!vgContext || vgContext != t_currentContext->vgContext) {
      shSetEGLError(EGL_BAD_ACCESS);
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    imageHandle = (VGHandle)(uintptr_t)buffer;
    image = shGetImage(vgContext, (VGImage)imageHandle);
    if (!image) {
      shSetEGLError(EGL_BAD_PARAMETER);
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    if (shImageIsEGLPbufferBound(image) ||
        shImageIsRenderTarget(image)) {
      shSetEGLError(EGL_BAD_ACCESS);
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    if (!shImageIsRenderTargetEligible(image)) {
      shSetEGLError(EGL_BAD_MATCH);
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    if (!shParseClientPbufferAttribs(attrib_list,
                                     &textureFormat,
                                     &textureTarget,
                                     &mipmapTexture))
    {
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    if (!shConfigMatchesImage(display, config, image))
    {
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    hiddenAttribs[0] = EGL_WIDTH;
    hiddenAttribs[1] = image->width;
    hiddenAttribs[2] = EGL_HEIGHT;
    hiddenAttribs[3] = image->height;
    hiddenAttribs[4] = EGL_NONE;

    realSurface = g_egl.CreatePbufferSurface(display->realDisplay,
                                             config,
                                             hiddenAttribs);
    if (realSurface == EGL_NO_SURFACE)
    {
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    if (!shCreateImageFramebuffer(image, &framebuffer, &stencil)) {
      g_egl.DestroySurface(display->realDisplay, realSurface);
      shSetEGLError(EGL_BAD_ALLOC);
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    surface = (SHEGLSurface*)calloc(1, sizeof(SHEGLSurface));
    if (!surface) {
      if (framebuffer != 0)
        glDeleteFramebuffers(1, &framebuffer);
      if (stencil != 0)
        glDeleteRenderbuffers(1, &stencil);
      g_egl.DestroySurface(display->realDisplay, realSurface);
      shSetEGLError(EGL_BAD_ALLOC);
      shContextLockCleanup(&vgLock);
      SH_EGL_RETURN(EGL_NO_SURFACE);
    }

    shImageAddEGLPbufferRef(image);

    surface->magic = SH_EGL_SURFACE_MAGIC;
    surface->display = display;
    surface->displayGeneration = display->generation;
    surface->realSurface = realSurface;
    surface->type = SH_EGL_SURFACE_OPENVG_IMAGE;
    surface->config = config;
    surface->vgImage = image;
    surface->vgContext = t_currentContext->vgContext;
    surface->framebuffer = framebuffer;
    surface->stencil = stencil;
    surface->width = image->width;
    surface->height = image->height;
    surface->textureFormat = textureFormat;
    surface->textureTarget = textureTarget;
    surface->mipmapTexture = mipmapTexture;
    surface->vgColorspace = shImageVGColorspace(image);
    surface->vgAlphaFormat = shImageVGAlphaFormat(image);
    shContextLockCleanup(&vgLock);
  }

  SH_EGL_RETURN((EGLSurface)surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
  SHEGLDisplay *display;
  SHEGLSurface *s;
  EGLBoolean result;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  s = shSurface(surface);
  if (!display || !s)
    SH_EGL_RETURN(EGL_FALSE);

  if (s->display != display) {
    shSetEGLError(EGL_BAD_MATCH);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (s->destroyRequested) {
    shSetEGLError(EGL_BAD_SURFACE);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (s->currentCount > 0) {
    if (!shThreadOwnsCurrent(s->currentCount,
                             s->currentThread)) {
      shSetEGLError(EGL_BAD_ACCESS);
      SH_EGL_RETURN(EGL_FALSE);
    }

    result = g_egl.DestroySurface(display->realDisplay,
                                  s->realSurface);
    if (result) {
      s->destroyRequested = EGL_TRUE;
      s->realDestroyed = EGL_TRUE;
    }
    SH_EGL_RETURN(result);
  }

  result = g_egl.DestroySurface(display->realDisplay, s->realSurface);
  if (result) {
    if (s->type == SH_EGL_SURFACE_OPENVG_IMAGE)
      shDestroyImageSurfaceResources(s);
    s->magic = 0;
    free(s);
  }

  SH_EGL_RETURN(result);
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint *value)
{
  SHEGLDisplay *display;
  SHEGLSurface *s;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  s = shSurface(surface);
  if (!display || !s)
    SH_EGL_RETURN(EGL_FALSE);
  if (!value) {
    shSetEGLError(EGL_BAD_PARAMETER);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (s->type == SH_EGL_SURFACE_OPENVG_IMAGE) {
    switch (attribute) {
    case EGL_WIDTH:
      *value = s->width;
      SH_EGL_RETURN(EGL_TRUE);
    case EGL_HEIGHT:
      *value = s->height;
      SH_EGL_RETURN(EGL_TRUE);
    case EGL_TEXTURE_FORMAT:
      *value = s->textureFormat;
      SH_EGL_RETURN(EGL_TRUE);
    case EGL_TEXTURE_TARGET:
      *value = s->textureTarget;
      SH_EGL_RETURN(EGL_TRUE);
    case EGL_MIPMAP_TEXTURE:
      *value = s->mipmapTexture;
      SH_EGL_RETURN(EGL_TRUE);
    case EGL_VG_COLORSPACE:
      *value = s->vgColorspace;
      SH_EGL_RETURN(EGL_TRUE);
    case EGL_VG_ALPHA_FORMAT:
      *value = s->vgAlphaFormat;
      SH_EGL_RETURN(EGL_TRUE);
    default:
      break;
    }
  }

  SH_EGL_RETURN(g_egl.QuerySurface(display->realDisplay, s->realSurface,
                                   attribute, value));
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                               EGLContext share_context,
                                               const EGLint *attrib_list)
{
  SHEGLDisplay *display;
  SHEGLContext *share = NULL;
  SHEGLContext *context;
  EGLContext realShare = EGL_NO_CONTEXT;
  EGLContext realContext;
  EGLenum api = t_boundApi;

  if (!shLoadRealEGL())
    return EGL_NO_CONTEXT;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_NO_CONTEXT);

  if (share_context != EGL_NO_CONTEXT) {
    share = shContext(share_context);
    if (!share)
      SH_EGL_RETURN(EGL_NO_CONTEXT);
    if (share->destroyRequested) {
      shSetEGLError(EGL_BAD_CONTEXT);
      SH_EGL_RETURN(EGL_NO_CONTEXT);
    }
    if (share->display != display) {
      shSetEGLError(EGL_BAD_MATCH);
      SH_EGL_RETURN(EGL_NO_CONTEXT);
    }
    if (share->api != api) {
      shSetEGLError(EGL_BAD_MATCH);
      SH_EGL_RETURN(EGL_NO_CONTEXT);
    }
    realShare = share->realContext;
  }

  if (api == EGL_OPENVG_API && !g_egl.BindAPI(EGL_OPENGL_API))
    SH_EGL_RETURN(EGL_NO_CONTEXT);

  realContext = g_egl.CreateContext(display->realDisplay, config, realShare,
                                    shContextAttribsForAPI(api, attrib_list));
  if (realContext == EGL_NO_CONTEXT)
    SH_EGL_RETURN(EGL_NO_CONTEXT);

  context = (SHEGLContext*)calloc(1, sizeof(SHEGLContext));
  if (!context) {
    g_egl.DestroyContext(display->realDisplay, realContext);
    shSetEGLError(EGL_BAD_ALLOC);
    SH_EGL_RETURN(EGL_NO_CONTEXT);
  }

  context->magic = SH_EGL_CONTEXT_MAGIC;
  context->display = display;
  context->displayGeneration = display->generation;
  context->realContext = realContext;
  context->api = api;

  if (api == EGL_OPENVG_API) {
    context->vgContext = shCreateContextShared(share ? share->vgContext : NULL);
    if (!context->vgContext) {
      g_egl.DestroyContext(display->realDisplay, realContext);
      free(context);
      shSetEGLError(EGL_BAD_ALLOC);
      SH_EGL_RETURN(EGL_NO_CONTEXT);
    }
  }

  SH_EGL_RETURN((EGLContext)context);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
  SHEGLDisplay *display;
  SHEGLContext *context;
  EGLBoolean result;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  context = shContext(ctx);
  if (!display || !context)
    SH_EGL_RETURN(EGL_FALSE);

  if (context->display != display) {
    shSetEGLError(EGL_BAD_MATCH);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (context->destroyRequested) {
    shSetEGLError(EGL_BAD_CONTEXT);
    SH_EGL_RETURN(EGL_FALSE);
  }

  if (context->currentCount > 0) {
    if (!shThreadOwnsCurrent(context->currentCount,
                               context->currentThread)) {
      shSetEGLError(EGL_BAD_ACCESS);
      SH_EGL_RETURN(EGL_FALSE);
    }

    result = g_egl.DestroyContext(display->realDisplay,
                                  context->realContext);
    if (result) {
      context->destroyRequested = EGL_TRUE;
      context->realDestroyed = EGL_TRUE;
    }
    SH_EGL_RETURN(result);
  }

  result = g_egl.DestroyContext(display->realDisplay, context->realContext);
  if (result) {
    if (context->vgContext) {
      shDestroyContext(context->vgContext);
      context->vgContext = NULL;
    }
    context->magic = 0;
    free(context);
  }

  SH_EGL_RETURN(result);
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                             EGLSurface read, EGLContext ctx)
{
  SHEGLDisplay *display;
  SHEGLSurface *drawSurface = NULL;
  SHEGLSurface *readSurface = NULL;
  SHEGLContext *context = NULL;
  SHEGLDisplay *oldDisplay = t_currentDisplay;
  SHEGLDisplay *newDisplay = NULL;
  SHEGLSurface *oldDraw = t_currentDraw;
  SHEGLSurface *oldRead = t_currentRead;
  SHEGLContext *oldContext = t_currentContext;
  EGLSurface realDraw = EGL_NO_SURFACE;
  EGLSurface realRead = EGL_NO_SURFACE;
  EGLContext realContext = EGL_NO_CONTEXT;
  EGLint oldWidth = 0;
  EGLint oldHeight = 0;
  EGLint width = 0;
  EGLint height = 0;
  SHImage *oldRenderTargetImage = NULL;
  SHImage *renderTargetImage = NULL;
  EGLBoolean usesImageSurface = EGL_FALSE;
  EGLBoolean usesOpenVGContext = EGL_FALSE;

  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();

  display = shDisplay(dpy);
  if (!display)
    SH_EGL_RETURN(EGL_FALSE);

  if (draw != EGL_NO_SURFACE) {
    drawSurface = shSurface(draw);
    if (!drawSurface)
      SH_EGL_RETURN(EGL_FALSE);
    realDraw = drawSurface->realSurface;
  }

  if (read != EGL_NO_SURFACE) {
    readSurface = shSurface(read);
    if (!readSurface)
      SH_EGL_RETURN(EGL_FALSE);
    realRead = readSurface->realSurface;
  }

  if (ctx != EGL_NO_CONTEXT) {
    context = shContext(ctx);
    if (!context)
      SH_EGL_RETURN(EGL_FALSE);
    realContext = context->realContext;
  }

  if (!shContextCanBeMadeCurrent(context) ||
      !shSurfaceCanBeMadeCurrent(drawSurface) ||
      !shSurfaceCanBeMadeCurrent(readSurface))
    SH_EGL_RETURN(EGL_FALSE);

  if (oldContext &&
      oldContext->api == EGL_OPENVG_API &&
      oldContext->vgContext) {
    oldWidth = oldContext->vgContext->surfaceWidth;
    oldHeight = oldContext->vgContext->surfaceHeight;
    oldRenderTargetImage = oldContext->vgContext->renderTargetImage;
  }

  usesOpenVGContext = (context && context->api == EGL_OPENVG_API) ?
    EGL_TRUE : EGL_FALSE;
  newDisplay = context ? display : NULL;

  usesImageSurface =
    (drawSurface && drawSurface->type == SH_EGL_SURFACE_OPENVG_IMAGE) ||
    (readSurface && readSurface->type == SH_EGL_SURFACE_OPENVG_IMAGE);

  if (usesImageSurface) {
    if (!context || context->api != EGL_OPENVG_API) {
      shSetEGLError(EGL_BAD_MATCH);
      SH_EGL_RETURN(EGL_FALSE);
    }

    if (drawSurface != readSurface) {
      shSetEGLError(EGL_BAD_MATCH);
      SH_EGL_RETURN(EGL_FALSE);
    }

    if (!drawSurface ||
        drawSurface->vgContext != context->vgContext) {
      shSetEGLError(EGL_BAD_MATCH);
      SH_EGL_RETURN(EGL_FALSE);
    }

    if (shImageIsRenderTarget(drawSurface->vgImage) &&
        t_currentDraw != drawSurface) {
      shSetEGLError(EGL_BAD_ACCESS);
      SH_EGL_RETURN(EGL_FALSE);
    }
  }

  if (usesOpenVGContext) {
    if (drawSurface &&
        drawSurface->type == SH_EGL_SURFACE_OPENVG_IMAGE) {
      width = drawSurface->width;
      height = drawSurface->height;
      renderTargetImage = drawSurface->vgImage;
    } else if (!shQuerySurfaceSize(display, drawSurface, &width, &height)) {
      SH_EGL_RETURN(EGL_FALSE);
    }
  }

  if (context && context->api == EGL_OPENVG_API && !g_egl.BindAPI(EGL_OPENGL_API))
    SH_EGL_RETURN(EGL_FALSE);

  if (oldDraw != drawSurface && !shNormalizeImageSurface(oldDraw))
    SH_EGL_RETURN(EGL_FALSE);

  if (!g_egl.MakeCurrent(display->realDisplay, realDraw, realRead, realContext))
    SH_EGL_RETURN(EGL_FALSE);

  if (usesOpenVGContext) {
    if (drawSurface &&
        drawSurface->type == SH_EGL_SURFACE_OPENVG_IMAGE)
      glBindFramebuffer(GL_FRAMEBUFFER, drawSurface->framebuffer);
    else
      glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!shSetCurrentContextForImage(context->vgContext,
                                     width,
                                     height,
                                     renderTargetImage)) {
      shClearCurrentContext();
      if (oldDisplay) {
        g_egl.MakeCurrent(oldDisplay->realDisplay,
                          oldDraw ? oldDraw->realSurface : EGL_NO_SURFACE,
                          oldRead ? oldRead->realSurface : EGL_NO_SURFACE,
                          oldContext ? oldContext->realContext : EGL_NO_CONTEXT);
      } else {
        g_egl.MakeCurrent(display->realDisplay,
                          EGL_NO_SURFACE,
                          EGL_NO_SURFACE,
                          EGL_NO_CONTEXT);
      }

      if (oldContext &&
          oldContext->api == EGL_OPENVG_API &&
          oldContext->vgContext) {
        shSetCurrentContextForImage(oldContext->vgContext,
                                    oldWidth,
                                    oldHeight,
                                    oldRenderTargetImage);
      } else {
        shClearCurrentContext();
      }

      shSetEGLError(EGL_BAD_ALLOC);
      SH_EGL_RETURN(EGL_FALSE);
    }
  } else {
    shClearCurrentContext();
  }

  if (oldDraw != drawSurface &&
      oldDraw &&
      oldDraw->type == SH_EGL_SURFACE_OPENVG_IMAGE)
    shImageEndRenderTarget(oldDraw->vgImage);

  if (oldDraw != drawSurface &&
      drawSurface &&
      drawSurface->type == SH_EGL_SURFACE_OPENVG_IMAGE)
    shImageBeginRenderTarget(drawSurface->vgImage);

  if (oldContext != context)
    shUnmarkContextCurrent(oldContext);
  if (oldDisplay != newDisplay)
    shUnmarkDisplayCurrent(oldDisplay);
  if (oldDraw &&
      oldDraw != drawSurface &&
      oldDraw != readSurface)
    shUnmarkSurfaceCurrent(oldDraw);
  if (oldRead &&
      oldRead != oldDraw &&
      oldRead != drawSurface &&
      oldRead != readSurface)
    shUnmarkSurfaceCurrent(oldRead);

  shMarkContextCurrent(context);
  if (newDisplay && oldDisplay != newDisplay)
    shMarkDisplayCurrent(newDisplay);
  shMarkSurfaceCurrent(drawSurface);
  if (readSurface != drawSurface)
    shMarkSurfaceCurrent(readSurface);

  t_currentDisplay = newDisplay;
  t_currentDraw = drawSurface;
  t_currentRead = readSurface;
  t_currentContext = context;

  if (oldContext != context)
    shFinalizeDestroyedContext(oldContext);
  if (oldDraw &&
      oldDraw != drawSurface &&
      oldDraw != readSurface)
    shFinalizeDestroyedSurface(oldDraw);
  if (oldRead &&
      oldRead != oldDraw &&
      oldRead != drawSurface &&
      oldRead != readSurface)
    shFinalizeDestroyedSurface(oldRead);

  SH_EGL_RETURN(EGL_TRUE);
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
  return t_currentContext ? (EGLContext)t_currentContext : EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
  if (readdraw == EGL_READ)
    return t_currentRead ? (EGLSurface)t_currentRead : EGL_NO_SURFACE;
  if (readdraw == EGL_DRAW)
    return t_currentDraw ? (EGLSurface)t_currentDraw : EGL_NO_SURFACE;
  shSetEGLError(EGL_BAD_PARAMETER);
  return EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
  return t_currentDisplay ? (EGLDisplay)t_currentDisplay : EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
  SHEGLDisplay *display;
  SHEGLSurface *s;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  s = shSurface(surface);
  SH_EGL_RETURN((display && s) ? g_egl.SwapBuffers(display->realDisplay, s->realSurface) : EGL_FALSE);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
  SHEGLDisplay *display;
  if (!shLoadRealEGL())
    return EGL_FALSE;
  SH_EGL_LOCK_GUARD();
  display = shDisplay(dpy);
  if (!display || !g_egl.SwapInterval)
    SH_EGL_RETURN(EGL_FALSE);
  SH_EGL_RETURN(g_egl.SwapInterval(display->realDisplay, interval));
}

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
  EGLint error = t_error;
  if (error != EGL_SUCCESS) {
    t_error = EGL_SUCCESS;
    return error;
  }

  if (!shLoadRealEGL())
    return t_error;

  SH_EGL_LOCK_GUARD();
  SH_EGL_RETURN(g_egl.GetError());
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
  SHEGLDisplay *display = NULL;
  const char *real;

  if (!shLoadRealEGL())
    return NULL;
  SH_EGL_LOCK_GUARD();

  if (dpy != EGL_NO_DISPLAY) {
    display = shDisplay(dpy);
    if (!display)
      SH_EGL_RETURN(NULL);
  }

  real = g_egl.QueryString(display ? display->realDisplay : EGL_NO_DISPLAY, name);

  if (name == EGL_CLIENT_APIS) {
    if (!real || !strstr(real, "OpenVG")) {
      snprintf(t_clientApis, sizeof(t_clientApis), "OpenVG%s%s",
               real ? " " : "", real ? real : "");
      SH_EGL_RETURN(t_clientApis);
    }
  }

  SH_EGL_RETURN(real);
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
  SHEGLSurface *oldDraw;
  SHEGLSurface *oldRead;
  SHEGLContext *oldContext;
  SHEGLDisplay *oldDisplay;

  if (!shLoadRealEGL())
    return EGL_FALSE;

  SH_EGL_LOCK_GUARD();

  oldDraw = t_currentDraw;
  oldRead = t_currentRead;
  oldContext = t_currentContext;
  oldDisplay = t_currentDisplay;

  if (t_currentDraw &&
      t_currentDraw->type == SH_EGL_SURFACE_OPENVG_IMAGE)
    shImageEndRenderTarget(t_currentDraw->vgImage);
  shClearCurrentContext();
  t_currentDisplay = NULL;
  t_currentDraw = NULL;
  t_currentRead = NULL;
  t_currentContext = NULL;

  shUnmarkContextCurrent(oldContext);
  shUnmarkDisplayCurrent(oldDisplay);
  shUnmarkSurfaceCurrent(oldDraw);
  if (oldRead != oldDraw)
    shUnmarkSurfaceCurrent(oldRead);

  shFinalizeDestroyedContext(oldContext);
  shFinalizeDestroyedSurface(oldDraw);
  if (oldRead != oldDraw)
    shFinalizeDestroyedSurface(oldRead);

  SH_EGL_RETURN(g_egl.ReleaseThread());
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
eglGetProcAddress(const char *procname)
{
#define SH_PROC(name) \
  if (strcmp(procname, "egl" #name) == 0) \
    return (__eglMustCastToProperFunctionPointerType)egl##name

  if (!procname)
    return NULL;

  SH_PROC(GetDisplay);
  SH_PROC(Initialize);
  SH_PROC(Terminate);
  SH_PROC(GetConfigs);
  SH_PROC(ChooseConfig);
  SH_PROC(GetConfigAttrib);
  SH_PROC(CreateWindowSurface);
  SH_PROC(CreatePbufferSurface);
  SH_PROC(CreatePbufferFromClientBuffer);
  SH_PROC(DestroySurface);
  SH_PROC(QuerySurface);
  SH_PROC(BindAPI);
  SH_PROC(QueryAPI);
  SH_PROC(CreateContext);
  SH_PROC(DestroyContext);
  SH_PROC(MakeCurrent);
  SH_PROC(GetCurrentContext);
  SH_PROC(GetCurrentSurface);
  SH_PROC(GetCurrentDisplay);
  SH_PROC(SwapBuffers);
  SH_PROC(SwapInterval);
  SH_PROC(GetError);
  SH_PROC(QueryString);
  SH_PROC(GetProcAddress);
  SH_PROC(ReleaseThread);

#undef SH_PROC

  if (!shLoadRealEGL())
    return NULL;

  SH_EGL_LOCK_GUARD();
  SH_EGL_RETURN(g_egl.GetProcAddress(procname));
}
