/*
 * Copyright © 2024 xMeM
 *
 * Derived from ephyr_glamor_glx.c which is:
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "glamor_egl.h"
#include "glamor_priv.h"

#include "lorie_glamor_egl.h"

static struct {
   EGLDisplay dpy;
   EGLContext ctx;
   EGLSurface egl_win;
   EGLConfig cfg;

   GLuint tex;

   GLuint texture_shader;
   GLuint texture_shader_position_loc;
   GLuint texture_shader_texcoord_loc;

   ANativeWindow *window;
   unsigned width, height;

   GLuint vao, vbo;

   Bool initialized;
} glamor;

static struct {
   GLuint tex;
   GLint x, y, width, height, xhot, yhot;
} cursor;

static void glamor_egl_make_current(struct glamor_context *glamor_ctx) {
   /* There's only a single global dispatch table in Mesa.  EGL, GLX,
    * and AIGLX's direct dispatch table manipulation don't talk to
    * each other.  We need to set the context to NULL first to avoid
    * EGL's no-op context change fast path when switching back to
    * EGL.
    */
   eglMakeCurrent(
      glamor_ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

   if (!eglMakeCurrent(glamor_ctx->display,
                       EGL_NO_SURFACE,
                       EGL_NO_SURFACE,
                       glamor_ctx->ctx)) {
      FatalError("Failed to make EGL context current\n");
   }
}

void lorieGlamorDestroyTexture(uint32_t tex) {
   if (!eglMakeCurrent(glamor.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, glamor.ctx))
      return;

   glFlush();
   glDeleteTextures(1, &tex);
}

uint32_t lorieGlamorTextureFromImage(EGLImageKHR image) {
   GLuint texture;

   if (!eglMakeCurrent(glamor.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, glamor.ctx))
      return 0;

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
   glBindTexture(GL_TEXTURE_2D, 0);

   return texture;
}

EGLImage lorieGlamorEGLCreateImageFromAHardwareBuffer(AHardwareBuffer *bo) {
   const EGLint attrs[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};

   eglMakeCurrent(glamor.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, glamor.ctx);

   EGLClientBuffer native_bo = eglGetNativeClientBufferANDROID(bo);

   return eglCreateImageKHR(
      glamor.dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, native_bo, attrs);
}

void glamor_egl_screen_init(ScreenPtr screen,
                            struct glamor_context *glamor_ctx) {
   glamor_enable_dri3(screen);
   glamor_ctx->display = glamor.dpy;
   glamor_ctx->ctx = glamor.ctx;
   glamor_ctx->make_current = glamor_egl_make_current;
}

void lorieGlamorEGLDestroyImage(EGLImage image) {
   eglDestroyImage(glamor.dpy, image);
}

int glamor_egl_fd_name_from_pixmap(ScreenPtr screen,
                                   PixmapPtr pixmap,
                                   CARD16 *stride,
                                   CARD32 *size) {
   return -1;
}

int glamor_egl_fds_from_pixmap(ScreenPtr screen,
                               PixmapPtr pixmap,
                               int *fds,
                               uint32_t *offsets,
                               uint32_t *strides,
                               uint64_t *modifier) {
   return 0;
}

int glamor_egl_fd_from_pixmap(ScreenPtr screen,
                              PixmapPtr pixmap,
                              CARD16 *stride,
                              CARD32 *size) {
   return -1;
}

static GLint lorie_glamor_compile_glsl_prog(GLenum type, const char *source) {
   GLint ok;
   GLint prog;

   prog = glCreateShader(type);
   glShaderSource(prog, 1, (const GLchar **)&source, NULL);
   glCompileShader(prog);
   glGetShaderiv(prog, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      GLchar *info;
      GLint size;

      glGetShaderiv(prog, GL_INFO_LOG_LENGTH, &size);
      info = malloc(size);
      if (info) {
         glGetShaderInfoLog(prog, size, NULL, info);
         ErrorF("Failed to compile %s: %s\n",
                type == GL_FRAGMENT_SHADER ? "FS" : "VS",
                info);
         ErrorF("Program source:\n%s", source);
         free(info);
      } else
         ErrorF("Failed to get shader compilation info.\n");
      FatalError("GLSL compile failure\n");
   }

   return prog;
}

static GLuint lorie_glamor_build_glsl_prog(GLuint vs, GLuint fs) {
   GLint ok;
   GLuint prog;

   prog = glCreateProgram();
   glAttachShader(prog, vs);
   glAttachShader(prog, fs);

   glLinkProgram(prog);
   glGetProgramiv(prog, GL_LINK_STATUS, &ok);
   if (!ok) {
      GLchar *info;
      GLint size;

      glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
      info = malloc(size);

      glGetProgramInfoLog(prog, size, NULL, info);
      ErrorF("Failed to link: %s\n", info);
      FatalError("GLSL link failure\n");
   }

   return prog;
}

static void lorie_glamor_setup_texturing_shader(void) {
   const char *vs_source = "attribute vec2 texcoord;\n"
                           "attribute vec2 position;\n"
                           "varying vec2 t;\n"
                           "\n"
                           "void main()\n"
                           "{\n"
                           "    t = texcoord;\n"
                           "    gl_Position = vec4(position, 0, 1);\n"
                           "}\n";

   const char *fs_source = "#ifdef GL_ES\n"
                           "precision mediump float;\n"
                           "#endif\n"
                           "\n"
                           "varying vec2 t;\n"
                           "uniform sampler2D s; /* initially 0 */\n"
                           "\n"
                           "void main()\n"
                           "{\n"
                           "    gl_FragColor = texture2D(s, t);\n"
                           "}\n";

   GLuint fs, vs, prog;

   vs = lorie_glamor_compile_glsl_prog(GL_VERTEX_SHADER, vs_source);
   fs = lorie_glamor_compile_glsl_prog(GL_FRAGMENT_SHADER, fs_source);
   prog = lorie_glamor_build_glsl_prog(vs, fs);

   glamor.texture_shader = prog;
   glamor.texture_shader_position_loc = glGetAttribLocation(prog, "position");
   assert(glamor.texture_shader_position_loc != -1);
   glamor.texture_shader_texcoord_loc = glGetAttribLocation(prog, "texcoord");
   assert(glamor.texture_shader_texcoord_loc != -1);
}

bool lorieGlamorSetWindow(ANativeWindow *win) {
   EGLSurface egl_win;

   if (!eglMakeCurrent(
          glamor.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT))
      return false;

   if (glamor.egl_win) {
      eglDestroySurface(glamor.dpy, glamor.egl_win);
      ANativeWindow_release(glamor.window);
      glamor.window = glamor.egl_win = NULL;
   }

   if (!win)
      return false;

   egl_win = eglCreateWindowSurface(glamor.dpy, glamor.cfg, win, NULL);
   if (egl_win == EGL_NO_SURFACE)
      return false;

   eglSwapInterval(glamor.dpy, 0);

   glamor.window = win;
   glamor.egl_win = egl_win;
   glamor.width = ANativeWindow_getWidth(win);
   glamor.height = ANativeWindow_getHeight(win);

   return true;
}

void lorieGlamorSetTexture(uint32_t tex) { glamor.tex = tex; }

static void lorie_glamor_set_vertices(void) {
   glVertexAttribPointer(
      glamor.texture_shader_position_loc, 2, GL_FLOAT, FALSE, 0, (void *)0);
   glVertexAttribPointer(glamor.texture_shader_texcoord_loc,
                         2,
                         GL_FLOAT,
                         FALSE,
                         0,
                         (void *)(sizeof(float) * 8));

   glEnableVertexAttribArray(glamor.texture_shader_position_loc);
   glEnableVertexAttribArray(glamor.texture_shader_texcoord_loc);
}

void lorieGlamorUpdateCursor(int w, int h, int xhot, int yhot, void *data) {
   cursor.width = w;
   cursor.height = h;
   cursor.xhot = xhot;
   cursor.yhot = yhot;

   if (!eglMakeCurrent(glamor.dpy, glamor.egl_win, glamor.egl_win, glamor.ctx))
      return;

   if (w < 1 && h < 1)
      return;

   glBindTexture(GL_TEXTURE_2D, cursor.tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void lorieGlamorSetCursorCoordinates(int x, int y) {
   cursor.x = x - cursor.xhot;
   cursor.y = y - cursor.yhot;
}

void lorieGlamorDamageReDisplay(struct pixman_region16 *damage) {
   GLint old_vao;

   if (!eglMakeCurrent(glamor.dpy, glamor.egl_win, glamor.egl_win, glamor.ctx))
      return;

   if (!eglGetCurrentSurface(EGL_DRAW) || !glamor.tex)
      return;

   glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
   glBindVertexArray(glamor.vao);

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glUseProgram(glamor.texture_shader);
   glViewport(0, 0, glamor.width, glamor.height);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, glamor.tex);
   glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

   glViewport(cursor.x,
              (glamor.height - cursor.y) - cursor.height,
              cursor.width,
              cursor.height);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glBindTexture(GL_TEXTURE_2D, cursor.tex);
   glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
   glDisable(GL_BLEND);

   glBindVertexArray(old_vao);

   eglSwapBuffers(glamor.dpy, glamor.egl_win);
}

void lorieGlamorInit(void) {
   if (glamor.initialized) {
      return;
   }

   glamor.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (glamor.dpy == EGL_NO_DISPLAY)
      FatalError("eglGetDisplay failed\n");

   int major, minor;
   if (!eglInitialize(glamor.dpy, &major, &minor))
      FatalError("eglInitialize failed\n");

   eglBindAPI(EGL_OPENGL_ES_API);

   /* clang-format off */
   const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 0,
      EGL_NONE,
   };
   /* clang-format on */

   int num_configs = 0;
   if (!eglChooseConfig(
          glamor.dpy, config_attribs, &glamor.cfg, 1, &num_configs)) {
      FatalError("eglChooseConfig failed\n");
   }

   /* clang-format off */
   const GLint ctx_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE,
   };
   /* clang-format on */

   glamor.ctx =
      eglCreateContext(glamor.dpy, glamor.cfg, EGL_NO_CONTEXT, ctx_attribs);
   if (glamor.ctx == EGL_NO_CONTEXT)
      FatalError("eglCreateContext failed\n");

   if (!eglMakeCurrent(glamor.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, glamor.ctx))
      FatalError("eglMakeCurrent failed\n");

   /* clang-format off */
   static const float position[] = {
      -1, -1, 1, -1, 1, 1, -1, 1,
       0,  1, 1,  1, 1, 0,  0, 0,
   };
   /* clang-format on */
   GLint old_vao;

   lorie_glamor_setup_texturing_shader();

   glGenVertexArrays(1, &glamor.vao);
   glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
   glBindVertexArray(glamor.vao);

   glGenBuffers(1, &glamor.vbo);

   glBindBuffer(GL_ARRAY_BUFFER, glamor.vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

   lorie_glamor_set_vertices();
   glBindVertexArray(old_vao);

   glGenTextures(1, &cursor.tex);

   glamor.initialized = TRUE;
}
