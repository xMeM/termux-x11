#include <epoxy/egl.h>
#include <glamor_priv.h>

static void
glamor_egl_make_current(struct glamor_context *glamor_ctx)
{
   /* There's only a single global dispatch table in Mesa.  EGL, GLX,
    * and AIGLX's direct dispatch table manipulation don't talk to
    * each other.  We need to set the context to NULL first to avoid
    * EGL's no-op context change fast path when switching back to
    * EGL.
    */
   eglMakeCurrent(
      glamor_ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   if (!eglMakeCurrent(glamor_ctx->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
          glamor_ctx->ctx)) {
      FatalError("Failed to make EGL context current\n");
   }
}

Bool glamor_glx_screen_init(struct glamor_context *glamor_ctx) {
   return FALSE;
}

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
   glamor_enable_dri3(screen);
   glamor_ctx->make_current = glamor_egl_make_current;
}

int
glamor_egl_fd_name_from_pixmap(
   ScreenPtr screen, PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
   return -1;
}

int
glamor_egl_fds_from_pixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds,
   uint32_t *offsets, uint32_t *strides, uint64_t *modifier)
{
   return 0;
}

int
glamor_egl_fd_from_pixmap(
   ScreenPtr screen, PixmapPtr pixmap, CARD16 *stride, CARD32 *size)
{
   return -1;
}
