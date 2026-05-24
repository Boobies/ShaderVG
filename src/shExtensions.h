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

#ifndef __SHEXTENSIONS_H
#define __SHEXTENSIONS_H

#if defined(_WIN32)
extern PFNGLACTIVETEXTUREPROC              glActiveTexture;
extern PFNGLATTACHSHADERPROC               glAttachShader;
extern PFNGLBINDBUFFERPROC                 glBindBuffer;
extern PFNGLBINDFRAMEBUFFERPROC            glBindFramebuffer;
extern PFNGLBINDRENDERBUFFERPROC           glBindRenderbuffer;
extern PFNGLBINDVERTEXARRAYPROC            glBindVertexArray;
extern PFNGLBLENDEQUATIONPROC              glBlendEquation;
extern PFNGLBLENDEQUATIONSEPARATEPROC      glBlendEquationSeparate;
extern PFNGLBLENDFUNCSEPARATEPROC          glBlendFuncSeparate;
extern PFNGLBUFFERDATAPROC                 glBufferData;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC     glCheckFramebufferStatus;
extern PFNGLCOMPILESHADERPROC              glCompileShader;
extern PFNGLCREATEPROGRAMPROC              glCreateProgram;
extern PFNGLCREATESHADERPROC               glCreateShader;
extern PFNGLDELETEBUFFERSPROC              glDeleteBuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC         glDeleteFramebuffers;
extern PFNGLDELETEPROGRAMPROC              glDeleteProgram;
extern PFNGLDELETERENDERBUFFERSPROC        glDeleteRenderbuffers;
extern PFNGLDELETESHADERPROC               glDeleteShader;
extern PFNGLDELETEVERTEXARRAYSPROC         glDeleteVertexArrays;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC   glDisableVertexAttribArray;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC    glEnableVertexAttribArray;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC    glFramebufferRenderbuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC       glFramebufferTexture2D;
extern PFNGLGENBUFFERSPROC                 glGenBuffers;
extern PFNGLGENFRAMEBUFFERSPROC            glGenFramebuffers;
extern PFNGLGENRENDERBUFFERSPROC           glGenRenderbuffers;
extern PFNGLGENVERTEXARRAYSPROC            glGenVertexArrays;
extern PFNGLGETATTRIBLOCATIONPROC          glGetAttribLocation;
extern PFNGLGETSHADERINFOLOGPROC           glGetShaderInfoLog;
extern PFNGLGETSHADERIVPROC                glGetShaderiv;
extern PFNGLGETUNIFORMFVPROC               glGetUniformfv;
extern PFNGLGETUNIFORMLOCATIONPROC         glGetUniformLocation;
extern PFNGLLINKPROGRAMPROC                glLinkProgram;
extern PFNGLRENDERBUFFERSTORAGEPROC        glRenderbufferStorage;
extern PFNGLSHADERSOURCEPROC               glShaderSource;
extern PFNGLUNIFORM1FPROC                  glUniform1f;
extern PFNGLUNIFORM1FVPROC                 glUniform1fv;
extern PFNGLUNIFORM1IPROC                  glUniform1i;
extern PFNGLUNIFORM1IVPROC                 glUniform1iv;
extern PFNGLUNIFORM2FPROC                  glUniform2f;
extern PFNGLUNIFORM2FVPROC                 glUniform2fv;
extern PFNGLUNIFORM2IPROC                  glUniform2i;
extern PFNGLUNIFORM2IVPROC                 glUniform2iv;
extern PFNGLUNIFORM3FPROC                  glUniform3f;
extern PFNGLUNIFORM3FVPROC                 glUniform3fv;
extern PFNGLUNIFORM3IPROC                  glUniform3i;
extern PFNGLUNIFORM3IVPROC                 glUniform3iv;
extern PFNGLUNIFORM4FPROC                  glUniform4f;
extern PFNGLUNIFORM4FVPROC                 glUniform4fv;
extern PFNGLUNIFORM4IPROC                  glUniform4i;
extern PFNGLUNIFORM4IVPROC                 glUniform4iv;
extern PFNGLUNIFORMMATRIX2FVPROC           glUniformMatrix2fv;
extern PFNGLUNIFORMMATRIX3FVPROC           glUniformMatrix3fv;
extern PFNGLUNIFORMMATRIX4FVPROC           glUniformMatrix4fv;
extern PFNGLUSEPROGRAMPROC                 glUseProgram;
extern PFNGLVERTEXATTRIBPOINTERPROC        glVertexAttribPointer;
#endif

void shLoadExtensions(void *c);
void shCheckShaderCompile(GLuint shader,
                          const char *label,
                          const char *file,
                          int line);

#define SH_CHECK_SHADER_COMPILE(shader, label) \
  shCheckShaderCompile((shader), (label), __FILE__, __LINE__)

#endif
