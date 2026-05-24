/*
 * Copyright (c) 2007 Ivan Leben
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library in the file COPYING;
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define VG_API_EXPORT
#include "shDefs.h"

/*-----------------------------------------------------
 * OpenGL core profile
 *-----------------------------------------------------*/
#if defined(_WIN32)
PFNGLACTIVETEXTUREPROC              glActiveTexture;
PFNGLATTACHSHADERPROC               glAttachShader;
PFNGLBINDBUFFERPROC                 glBindBuffer;
PFNGLBINDFRAMEBUFFERPROC            glBindFramebuffer;
PFNGLBINDRENDERBUFFERPROC           glBindRenderbuffer;
PFNGLBINDVERTEXARRAYPROC            glBindVertexArray;
PFNGLBLENDEQUATIONPROC              glBlendEquation;
PFNGLBLENDEQUATIONSEPARATEPROC      glBlendEquationSeparate;
PFNGLBLENDFUNCSEPARATEPROC          glBlendFuncSeparate;
PFNGLBUFFERDATAPROC                 glBufferData;
PFNGLCHECKFRAMEBUFFERSTATUSPROC     glCheckFramebufferStatus;
PFNGLCOMPILESHADERPROC              glCompileShader;
PFNGLCREATEPROGRAMPROC              glCreateProgram;
PFNGLCREATESHADERPROC               glCreateShader;
PFNGLDELETEBUFFERSPROC              glDeleteBuffers;
PFNGLDELETEFRAMEBUFFERSPROC         glDeleteFramebuffers;
PFNGLDELETEPROGRAMPROC              glDeleteProgram;
PFNGLDELETERENDERBUFFERSPROC        glDeleteRenderbuffers;
PFNGLDELETESHADERPROC               glDeleteShader;
PFNGLDELETEVERTEXARRAYSPROC         glDeleteVertexArrays;
PFNGLDISABLEVERTEXATTRIBARRAYPROC   glDisableVertexAttribArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC    glEnableVertexAttribArray;
PFNGLFRAMEBUFFERRENDERBUFFERPROC    glFramebufferRenderbuffer;
PFNGLFRAMEBUFFERTEXTURE2DPROC       glFramebufferTexture2D;
PFNGLGENBUFFERSPROC                 glGenBuffers;
PFNGLGENFRAMEBUFFERSPROC            glGenFramebuffers;
PFNGLGENRENDERBUFFERSPROC           glGenRenderbuffers;
PFNGLGENVERTEXARRAYSPROC            glGenVertexArrays;
PFNGLGETATTRIBLOCATIONPROC          glGetAttribLocation;
PFNGLGETSHADERINFOLOGPROC           glGetShaderInfoLog;
PFNGLGETSHADERIVPROC                glGetShaderiv;
PFNGLGETUNIFORMFVPROC               glGetUniformfv;
PFNGLGETUNIFORMLOCATIONPROC         glGetUniformLocation;
PFNGLISVERTEXARRAYPROC              glIsVertexArray;
PFNGLLINKPROGRAMPROC                glLinkProgram;
PFNGLRENDERBUFFERSTORAGEPROC        glRenderbufferStorage;
PFNGLSHADERSOURCEPROC               glShaderSource;
PFNGLUNIFORM1FPROC                  glUniform1f;
PFNGLUNIFORM1FVPROC                 glUniform1fv;
PFNGLUNIFORM1IPROC                  glUniform1i;
PFNGLUNIFORM1IVPROC                 glUniform1iv;
PFNGLUNIFORM2FPROC                  glUniform2f;
PFNGLUNIFORM2FVPROC                 glUniform2fv;
PFNGLUNIFORM2IPROC                  glUniform2i;
PFNGLUNIFORM2IVPROC                 glUniform2iv;
PFNGLUNIFORM3FPROC                  glUniform3f;
PFNGLUNIFORM3FVPROC                 glUniform3fv;
PFNGLUNIFORM3IPROC                  glUniform3i;
PFNGLUNIFORM3IVPROC                 glUniform3iv;
PFNGLUNIFORM4FPROC                  glUniform4f;
PFNGLUNIFORM4FVPROC                 glUniform4fv;
PFNGLUNIFORM4IPROC                  glUniform4i;
PFNGLUNIFORM4IVPROC                 glUniform4iv;
PFNGLUNIFORMMATRIX2FVPROC           glUniformMatrix2fv;
PFNGLUNIFORMMATRIX3FVPROC           glUniformMatrix3fv;
PFNGLUNIFORMMATRIX4FVPROC           glUniformMatrix4fv;
PFNGLUSEPROGRAMPROC                 glUseProgram;
PFNGLVERTEXATTRIBPOINTERPROC        glVertexAttribPointer;

static void *shGetProcAddress(const char *name)
{
  void *proc = (void*)wglGetProcAddress(name);

  if (proc == NULL ||
      proc == (void*)0x1 ||
      proc == (void*)0x2 ||
      proc == (void*)0x3 ||
      proc == (void*)-1) {
    static HMODULE opengl32 = NULL;
    if (opengl32 == NULL)
      opengl32 = LoadLibraryA("opengl32.dll");
    proc = opengl32 ? (void*)GetProcAddress(opengl32, name) : NULL;
  }

  return proc;
}

#define SH_LOAD_GL(type, name) name = (type)shGetProcAddress(#name)
#endif

void shLoadExtensions(void *c)
{
  (void)c;

#if defined(_WIN32)
  SH_LOAD_GL(PFNGLACTIVETEXTUREPROC, glActiveTexture);
  SH_LOAD_GL(PFNGLATTACHSHADERPROC, glAttachShader);
  SH_LOAD_GL(PFNGLBINDBUFFERPROC, glBindBuffer);
  SH_LOAD_GL(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
  SH_LOAD_GL(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer);
  SH_LOAD_GL(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray);
  SH_LOAD_GL(PFNGLBLENDEQUATIONPROC, glBlendEquation);
  SH_LOAD_GL(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate);
  SH_LOAD_GL(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate);
  SH_LOAD_GL(PFNGLBUFFERDATAPROC, glBufferData);
  SH_LOAD_GL(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus);
  SH_LOAD_GL(PFNGLCOMPILESHADERPROC, glCompileShader);
  SH_LOAD_GL(PFNGLCREATEPROGRAMPROC, glCreateProgram);
  SH_LOAD_GL(PFNGLCREATESHADERPROC, glCreateShader);
  SH_LOAD_GL(PFNGLDELETEBUFFERSPROC, glDeleteBuffers);
  SH_LOAD_GL(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);
  SH_LOAD_GL(PFNGLDELETEPROGRAMPROC, glDeleteProgram);
  SH_LOAD_GL(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers);
  SH_LOAD_GL(PFNGLDELETESHADERPROC, glDeleteShader);
  SH_LOAD_GL(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays);
  SH_LOAD_GL(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray);
  SH_LOAD_GL(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
  SH_LOAD_GL(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer);
  SH_LOAD_GL(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D);
  SH_LOAD_GL(PFNGLGENBUFFERSPROC, glGenBuffers);
  SH_LOAD_GL(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
  SH_LOAD_GL(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers);
  SH_LOAD_GL(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays);
  SH_LOAD_GL(PFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation);
  SH_LOAD_GL(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
  SH_LOAD_GL(PFNGLGETSHADERIVPROC, glGetShaderiv);
  SH_LOAD_GL(PFNGLGETUNIFORMFVPROC, glGetUniformfv);
  SH_LOAD_GL(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
  SH_LOAD_GL(PFNGLISVERTEXARRAYPROC, glIsVertexArray);
  SH_LOAD_GL(PFNGLLINKPROGRAMPROC, glLinkProgram);
  SH_LOAD_GL(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage);
  SH_LOAD_GL(PFNGLSHADERSOURCEPROC, glShaderSource);
  SH_LOAD_GL(PFNGLUNIFORM1FPROC, glUniform1f);
  SH_LOAD_GL(PFNGLUNIFORM1FVPROC, glUniform1fv);
  SH_LOAD_GL(PFNGLUNIFORM1IPROC, glUniform1i);
  SH_LOAD_GL(PFNGLUNIFORM1IVPROC, glUniform1iv);
  SH_LOAD_GL(PFNGLUNIFORM2FPROC, glUniform2f);
  SH_LOAD_GL(PFNGLUNIFORM2FVPROC, glUniform2fv);
  SH_LOAD_GL(PFNGLUNIFORM2IPROC, glUniform2i);
  SH_LOAD_GL(PFNGLUNIFORM2IVPROC, glUniform2iv);
  SH_LOAD_GL(PFNGLUNIFORM3FPROC, glUniform3f);
  SH_LOAD_GL(PFNGLUNIFORM3FVPROC, glUniform3fv);
  SH_LOAD_GL(PFNGLUNIFORM3IPROC, glUniform3i);
  SH_LOAD_GL(PFNGLUNIFORM3IVPROC, glUniform3iv);
  SH_LOAD_GL(PFNGLUNIFORM4FPROC, glUniform4f);
  SH_LOAD_GL(PFNGLUNIFORM4FVPROC, glUniform4fv);
  SH_LOAD_GL(PFNGLUNIFORM4IPROC, glUniform4i);
  SH_LOAD_GL(PFNGLUNIFORM4IVPROC, glUniform4iv);
  SH_LOAD_GL(PFNGLUNIFORMMATRIX2FVPROC, glUniformMatrix2fv);
  SH_LOAD_GL(PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv);
  SH_LOAD_GL(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);
  SH_LOAD_GL(PFNGLUSEPROGRAMPROC, glUseProgram);
  SH_LOAD_GL(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
#endif
}

void shCheckShaderCompile(GLuint shader,
                          const char *label,
                          const char *file,
                          int line)
{
  GLint status = GL_FALSE;
  GLint logLength = 0;
  char log[2048];
  GLsizei written = 0;

#if defined(_WIN32)
  if (!glGetShaderiv)
    return;
#endif

  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_TRUE)
    return;

  fprintf(stderr,
          "Shader compile failed for %s at %s:%d\n",
          label ? label : "shader",
          file ? file : "<unknown>",
          line);

#if defined(_WIN32)
  if (!glGetShaderInfoLog)
    return;
#endif

  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
  if (logLength <= 1)
    return;

  glGetShaderInfoLog(shader, (GLsizei)sizeof(log) - 1, &written, log);
  if (written < 0)
    written = 0;
  if (written >= (GLsizei)sizeof(log))
    written = (GLsizei)sizeof(log) - 1;
  log[written] = '\0';

  if (written > 0)
    fprintf(stderr, "%s\n", log);
}
