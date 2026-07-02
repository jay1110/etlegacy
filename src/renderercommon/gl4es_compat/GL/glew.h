/*
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
 * @file GL/glew.h
 * @brief Minimal GLEW compatibility shim backed by gl4es.
 *
 * When ET:Legacy is built for Emscripten with FEATURE_GL4ES, the OpenGL1
 * renderer's desktop GL calls are translated to WebGL/GLES2 by gl4es
 * (https://github.com/ptitSeb/gl4es), which also provides the full desktop
 * GL entry points. GLEW itself (an X11/WGL extension loader) is neither
 * available nor needed in that setup, but the renderer sources include
 * <GL/glew.h> and use a handful of GLEW symbols. This shim satisfies those
 * uses by forwarding to gl4es' GL headers and by hard-coding the extension
 * availability booleans that gl4es emulates.
 *
 * This header is placed on the renderer include path (ahead of the gl4es
 * headers) only when FEATURE_GL4ES is enabled, so existing
 * `#include <GL/glew.h>` directives transparently resolve here without
 * touching the renderer sources.
 */

#ifndef INCLUDE_GL4ES_GLEW_COMPAT_H
#define INCLUDE_GL4ES_GLEW_COMPAT_H

/* gl4es provides the desktop GL 1.x/2.x API (including ARB multitexture,
 * framebuffer objects, texture compression, ...) and translates it to GLES2. */
#include <GL/gl.h>
#include <GL/glext.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GLEW return codes. */
#ifndef GLEW_OK
#define GLEW_OK       0
#endif
#ifndef GLEW_NO_ERROR
#define GLEW_NO_ERROR 0
#endif

/* Passed to glewGetString(); any non-zero value is fine here. */
#ifndef GLEW_VERSION
#define GLEW_VERSION  1
#endif

/*
 * glewExperimental is written to by the renderer before glewInit(). Provide a
 * private, per-translation-unit storage so the assignment compiles and is
 * harmlessly ignored. Marked unused to avoid -Wunused-variable warnings in
 * translation units that only read/write it once.
 */
#if defined(__GNUC__) || defined(__clang__)
static GLboolean glewExperimental __attribute__((unused));
#else
static GLboolean glewExperimental;
#endif

/* gl4es needs no explicit loader init: report success immediately. */
static inline GLenum glewInit(void)
{
	return GLEW_OK;
}

static inline const GLubyte *glewGetErrorString(GLenum error)
{
	(void)error;
	return (const GLubyte *)"gl4es (no GLEW)";
}

static inline const GLubyte *glewGetString(GLenum name)
{
	(void)name;
	return (const GLubyte *)"gl4es-glew-shim";
}

static inline GLboolean glewIsSupported(const char *name)
{
	(void)name;
	return GL_TRUE;
}

/*
 * Extension availability booleans used by the OpenGL1 renderer. gl4es emulates
 * these desktop features on top of GLES2/WebGL, so advertise them as present.
 * Multisample FBOs and native S3TC are not guaranteed under WebGL, so those are
 * reported as unavailable to keep the renderer on safe code paths.
 */
#ifndef GLEW_ARB_multitexture
#define GLEW_ARB_multitexture               GL_TRUE
#endif
#ifndef GLEW_ARB_fragment_program
#define GLEW_ARB_fragment_program           GL_TRUE
#endif
#ifndef GLEW_ARB_framebuffer_object
#define GLEW_ARB_framebuffer_object         GL_TRUE
#endif
#ifndef GLEW_ARB_texture_compression
#define GLEW_ARB_texture_compression        GL_TRUE
#endif
#ifndef GLEW_ARB_texture_non_power_of_two
#define GLEW_ARB_texture_non_power_of_two   GL_TRUE
#endif
#ifndef GLEW_EXT_texture_env_add
#define GLEW_EXT_texture_env_add            GL_TRUE
#endif
#ifndef GLEW_EXT_texture_filter_anisotropic
#define GLEW_EXT_texture_filter_anisotropic GL_TRUE
#endif
#ifndef GLEW_EXT_texture_compression_s3tc
#define GLEW_EXT_texture_compression_s3tc   GL_TRUE
#endif
#ifndef GLEW_EXT_framebuffer_multisample
#define GLEW_EXT_framebuffer_multisample    GL_FALSE
#endif
#ifndef GLEW_S3_s3tc
#define GLEW_S3_s3tc                        GL_FALSE
#endif

#ifdef __cplusplus
}
#endif

/*
 * gl4es exports only un-suffixed entry points and, on Emscripten, omits the
 * ARB/EXT alias symbols (see gl4es src/gl/attributes.h). Its gl_mangle.h still
 * rewrites the suffixed names the OpenGL1 renderer calls to gl4es_*ARB/*EXT
 * symbols that are never defined, breaking the link. Redirect the suffixed
 * names the renderer uses to gl4es' equivalent core entry points, which are
 * both declared (prototype available) and defined in libGL.a.
 */
#undef glActiveTextureARB
#define glActiveTextureARB glActiveTexture
#undef glClientActiveTextureARB
#define glClientActiveTextureARB glClientActiveTexture
#undef glBindFramebufferEXT
#define glBindFramebufferEXT glBindFramebuffer
#undef glBindRenderbufferEXT
#define glBindRenderbufferEXT glBindRenderbuffer
#undef glCheckFramebufferStatusEXT
#define glCheckFramebufferStatusEXT glCheckFramebufferStatus
#undef glDeleteFramebuffersEXT
#define glDeleteFramebuffersEXT glDeleteFramebuffers
#undef glDeleteRenderbuffersEXT
#define glDeleteRenderbuffersEXT glDeleteRenderbuffers
#undef glFramebufferRenderbufferEXT
#define glFramebufferRenderbufferEXT glFramebufferRenderbuffer
#undef glGenFramebuffersEXT
#define glGenFramebuffersEXT glGenFramebuffers
#undef glGenRenderbuffersEXT
#define glGenRenderbuffersEXT glGenRenderbuffers
#undef glRenderbufferStorageEXT
#define glRenderbufferStorageEXT glRenderbufferStorage
#undef glCreateShaderObjectARB
#define glCreateShaderObjectARB glCreateShader
#undef glCreateProgramObjectARB
#define glCreateProgramObjectARB glCreateProgram
#undef glShaderSourceARB
#define glShaderSourceARB glShaderSource
#undef glCompileShaderARB
#define glCompileShaderARB glCompileShader
#undef glAttachObjectARB
#define glAttachObjectARB glAttachShader
#undef glDetachObjectARB
#define glDetachObjectARB glDetachShader
#undef glLinkProgramARB
#define glLinkProgramARB glLinkProgram
#undef glUseProgramObjectARB
#define glUseProgramObjectARB glUseProgram

/*
 * These entry points have no un-suffixed core equivalent in the GL headers, so
 * gl4es keeps them under its own base names without public prototypes. Declare
 * the gl4es symbols explicitly and redirect the suffixed names to them.
 * Signatures match gl4es (src/gl/gl4es.c, src/gl/program.c).
 */
extern void gl4es_glLockArrays(GLint first, GLsizei count);
extern void gl4es_glUnlockArrays(void);
extern void gl4es_glDeleteObject(GLhandleARB obj);
extern void gl4es_glGetObjectParameteriv(GLhandleARB obj, GLenum pname, GLint *params);
extern void gl4es_glGetInfoLog(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog);
#undef glLockArraysEXT
#define glLockArraysEXT gl4es_glLockArrays
#undef glUnlockArraysEXT
#define glUnlockArraysEXT gl4es_glUnlockArrays
#undef glDeleteObjectARB
#define glDeleteObjectARB gl4es_glDeleteObject
#undef glGetObjectParameterivARB
#define glGetObjectParameterivARB gl4es_glGetObjectParameteriv
#undef glGetInfoLogARB
#define glGetInfoLogARB gl4es_glGetInfoLog

#endif /* INCLUDE_GL4ES_GLEW_COMPAT_H */
