/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**
 * @file tr_gl_emscripten.c
 * @brief Emscripten/WebAssembly OpenGL compatibility shims.
 *
 * When targeting the browser the game links against Emscripten's own GLEW port
 * (`-lGLEW`) and its WebGL-backed GL implementation. That implementation does
 * not export a number of legacy desktop-GL entry points that the OpenGL 1.x
 * renderer references:
 *
 *  - ARB multitexture aliases (glActiveTextureARB / glClientActiveTextureARB)
 *  - the fixed-function display-list / attribute-stack calls
 *  - the EXT framebuffer object family
 *  - the ARB shader-object (assembly/GLSL) API
 *  - a handful of misc. entry points (glGetStringi, glGetQueryivARB,
 *    glDebugMessageCallback, glLockArraysEXT, ...)
 *
 * On the desktop these symbols are provided by the bundled GLEW library as
 * loadable function pointers; in the browser most of them either map onto a
 * core WebGL entry point or belong to a feature that simply does not exist.
 *
 * This translation unit provides those symbols for the Emscripten build only.
 * The multitexture aliases forward to the real core entry points; everything
 * else is a safe no-op. The renderer only ever invokes the no-op paths behind
 * an extension-availability check, so the stubs are never reached during normal
 * rendering - they exist purely to satisfy the linker.
 *
 * The whole file compiles to nothing on every other platform.
 */

#ifdef __EMSCRIPTEN__

#include <GL/glew.h>
#include <GL/gl.h>
#include <GL/glext.h>

/* ---- ARB multitexture: forward to the core WebGL entry points ---- */

void glActiveTextureARB(GLenum texture)
{
	glActiveTexture(texture);
}

void glClientActiveTextureARB(GLenum texture)
{
	glClientActiveTexture(texture);
}

/* ---- EXT compiled vertex arrays: no-op perf hints ---- */

void glLockArraysEXT(GLint first, GLsizei count)
{
	(void)first;
	(void)count;
}

void glUnlockArraysEXT(void)
{
}

/* ---- Fixed-function display lists / attribute stack (unsupported) ---- */

void glCallList(GLuint list)
{
	(void)list;
}

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
	(void)n;
	(void)type;
	(void)lists;
}

void glListBase(GLuint base)
{
	(void)base;
}

void glPushAttrib(GLbitfield mask)
{
	(void)mask;
}

void glPopAttrib(void)
{
}

void glRasterPos3fv(const GLfloat *v)
{
	(void)v;
}

/* ---- EXT framebuffer object family (unsupported in this renderer path) ---- */

void glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
	(void)target;
	(void)framebuffer;
}

void glBindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
	(void)target;
	(void)renderbuffer;
}

GLenum glCheckFramebufferStatusEXT(GLenum target)
{
	(void)target;
	return 0;
}

void glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers)
{
	(void)n;
	(void)framebuffers;
}

void glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers)
{
	(void)n;
	(void)renderbuffers;
}

void glFramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)
{
	(void)target;
	(void)attachment;
	(void)renderbuffertarget;
	(void)renderbuffer;
}

void glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers)
{
	(void)n;
	(void)framebuffers;
}

void glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers)
{
	(void)n;
	(void)renderbuffers;
}

void glRenderbufferStorageEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
	(void)target;
	(void)internalformat;
	(void)width;
	(void)height;
}

void glRenderbufferStorageMultisampleEXT(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
	(void)target;
	(void)samples;
	(void)internalformat;
	(void)width;
	(void)height;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter)
{
	(void)srcX0;
	(void)srcY0;
	(void)srcX1;
	(void)srcY1;
	(void)dstX0;
	(void)dstY0;
	(void)dstX1;
	(void)dstY1;
	(void)mask;
	(void)filter;
}

void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height)
{
	(void)target;
	(void)samples;
	(void)internalformat;
	(void)width;
	(void)height;
}

/* ---- ARB shader objects (assembly / early GLSL API, unsupported) ---- */

GLhandleARB glCreateProgramObjectARB(void)
{
	return 0;
}

GLhandleARB glCreateShaderObjectARB(GLenum shaderType)
{
	(void)shaderType;
	return 0;
}

void glShaderSourceARB(GLhandleARB shaderObj, GLsizei count, const GLcharARB **string, const GLint *length)
{
	(void)shaderObj;
	(void)count;
	(void)string;
	(void)length;
}

void glCompileShaderARB(GLhandleARB shaderObj)
{
	(void)shaderObj;
}

void glAttachObjectARB(GLhandleARB containerObj, GLhandleARB obj)
{
	(void)containerObj;
	(void)obj;
}

void glDetachObjectARB(GLhandleARB containerObj, GLhandleARB attachedObj)
{
	(void)containerObj;
	(void)attachedObj;
}

void glLinkProgramARB(GLhandleARB programObj)
{
	(void)programObj;
}

void glUseProgramObjectARB(GLhandleARB programObj)
{
	(void)programObj;
}

/* ---- Misc. entry points not exported by the Emscripten GL implementation ---- */

const GLubyte *glGetStringi(GLenum name, GLuint index)
{
	(void)name;
	(void)index;
	return (const GLubyte *)"";
}

void glGetQueryivARB(GLenum target, GLenum pname, GLint *params)
{
	(void)target;
	(void)pname;
	if (params)
	{
		*params = 0;
	}
}

void glDebugMessageCallback(GLDEBUGPROC callback, const void *userParam)
{
	(void)callback;
	(void)userParam;
}

void glDebugMessageCallbackARB(GLDEBUGPROCARB callback, const void *userParam)
{
	(void)callback;
	(void)userParam;
}

#else

/* Keep this a non-empty translation unit on non-Emscripten targets. */
typedef int tr_gl_emscripten_translation_unit_not_empty;

#endif /* __EMSCRIPTEN__ */
