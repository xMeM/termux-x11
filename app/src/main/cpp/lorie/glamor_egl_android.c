#include "glamor_egl_android.h"
#include "buffer.h"
#include "glamor.h"
#include "glamor_egl.h"
#include "glamor_priv.h"

struct glamor_egl_screen_private {
   EGLDisplay display;
   EGLContext context;

   CloseScreenProcPtr CloseScreen;
   CreatePixmapProcPtr CreatePixmap;
   DestroyPixmapProcPtr DestroyPixmap;
};

struct glamor_egl_pixmap_private {
   EGLImageKHR egl_image_khr;
   LorieBuffer *lorie_buffer;
   AHardwareBuffer *ahardware_buffer;
};

DevPrivateKeyRec glamor_egl_screen_private_key;
DevPrivateKeyRec glamor_egl_pixmap_private_key;

#define CREATE_PIXMAP_USAGE_LORIEBUFFER_BACKED 5

static struct glamor_egl_screen_private *
glamor_egl_get_screen_private(ScreenPtr screen)
{
   return (struct glamor_egl_screen_private *)dixLookupPrivate(
      &screen->devPrivates, &glamor_egl_screen_private_key);
}

static void
glamor_egl_set_screen_private(
   ScreenPtr screen, struct glamor_egl_screen_private *priv)
{
   dixSetPrivate(&screen->devPrivates, &glamor_egl_screen_private_key, priv);
}

static struct glamor_egl_pixmap_private *
glamor_egl_get_pixmap_private(PixmapPtr pixmap)
{
   return (struct glamor_egl_pixmap_private *)dixLookupPrivate(
      &pixmap->devPrivates, &glamor_egl_pixmap_private_key);
}

static void
glamor_egl_release_pixmap_private(PixmapPtr pixmap)
{
   struct glamor_egl_screen_private *glamor_egl_priv =
      glamor_egl_get_screen_private(pixmap->drawable.pScreen);
   struct glamor_egl_pixmap_private *pixmap_priv =
      glamor_egl_get_pixmap_private(pixmap);
   if (pixmap_priv->egl_image_khr != EGL_NO_IMAGE_KHR) {
      eglDestroyImageKHR(
         glamor_egl_priv->display, pixmap_priv->egl_image_khr);
   }
   if (pixmap_priv->lorie_buffer != NULL) {
      lorieUnregisterBuffer(pixmap_priv->lorie_buffer);
      LorieBuffer_release(pixmap_priv->lorie_buffer);
   }
   if (pixmap_priv->ahardware_buffer != NULL) {
      AHardwareBuffer_release(pixmap_priv->ahardware_buffer);
   }
}

static Bool
glamor_egl_destroy_pixmap(PixmapPtr pixmap)
{
   struct glamor_egl_screen_private *glamor_egl_priv =
      glamor_egl_get_screen_private(pixmap->drawable.pScreen);
   if (pixmap->refcnt == 1) {
      glamor_egl_release_pixmap_private(pixmap);
   }
   return glamor_egl_priv->DestroyPixmap(pixmap);
}

static Bool
glamor_create_texture_from_image(
   ScreenPtr screen, EGLImageKHR image, GLuint *texture)
{
   struct glamor_screen_private *glamor_priv =
      glamor_get_screen_private(screen);

   glamor_make_current(glamor_priv);

   glGenTextures(1, texture);
   glBindTexture(GL_TEXTURE_2D, *texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
   glBindTexture(GL_TEXTURE_2D, 0);

   return TRUE;
}

PixmapPtr
glamor_egl_create_pixmap_from_ahardware_buffer(
   ScreenPtr screen, int depth, AHardwareBuffer *ahardware_buffer)
{
   struct glamor_egl_screen_private *glamor_egl_priv =
      glamor_egl_get_screen_private(screen);

   AHardwareBuffer_Desc ahardware_buffer_desc;
   AHardwareBuffer_describe(ahardware_buffer, &ahardware_buffer_desc);

   PixmapPtr pixmap = glamor_create_pixmap(screen,
      ahardware_buffer_desc.width, ahardware_buffer_desc.height, depth,
      GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
   if (!pixmap)
      return NullPixmap;

   struct glamor_egl_pixmap_private *pixmap_priv =
      glamor_egl_get_pixmap_private(pixmap);

   static const EGLint egl_image_attribs[] = {
      EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};

   EGLImageKHR egl_image_khr = eglCreateImageKHR(glamor_egl_priv->display,
      EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
      eglGetNativeClientBufferANDROID(ahardware_buffer), egl_image_attribs);
   if (egl_image_khr == EGL_NO_IMAGE_KHR) {
      glamor_destroy_pixmap(pixmap);
      return NullPixmap;
   }

   uint32_t texture;
   glamor_create_texture_from_image(screen, egl_image_khr, &texture);
   glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_ONLY);
   glamor_set_pixmap_texture(pixmap, texture);

   pixmap_priv->ahardware_buffer = ahardware_buffer;
   pixmap_priv->egl_image_khr = egl_image_khr;
   return pixmap;
}

PixmapPtr
glamor_egl_create_pixmap(
   ScreenPtr screen, int w, int h, int depth, unsigned int usage)
{
   struct glamor_egl_screen_private *glamor_egl_priv =
      glamor_egl_get_screen_private(screen);
   struct glamor_screen_private *glamor_priv =
      glamor_get_screen_private(screen);

   if (usage != CREATE_PIXMAP_USAGE_LORIEBUFFER_BACKED)
      return glamor_egl_priv->CreatePixmap(screen, w, h, depth, usage);

   PixmapPtr pixmap = glamor_create_pixmap(
      screen, w, h, depth, GLAMOR_CREATE_PIXMAP_NO_TEXTURE);
   if (!pixmap)
      return NullPixmap;

   LorieBuffer *lorie_buffer = LorieBuffer_allocate(w, h,
      AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM, LORIEBUFFER_AHARDWAREBUFFER);
   if (!lorie_buffer) {
      glamor_destroy_pixmap(pixmap);
      return NullPixmap;
   }

   glamor_make_current(glamor_priv);

   LorieBuffer_attachToGL(lorie_buffer);
   glamor_set_pixmap_type(pixmap, GLAMOR_TEXTURE_ONLY);
   glamor_set_pixmap_texture(pixmap, LorieBuffer_getGLTexture(lorie_buffer));
   glamor_egl_get_pixmap_private(pixmap)->lorie_buffer = lorie_buffer;
   return pixmap;
}

static Bool
glamor_egl_close_screen(ScreenPtr screen)
{
   struct glamor_egl_screen_private *glamor_egl_priv =
      glamor_egl_get_screen_private(screen);

   screen->CreatePixmap = glamor_egl_priv->CreatePixmap;
   screen->DestroyPixmap = glamor_egl_priv->DestroyPixmap;
   screen->CloseScreen = glamor_egl_priv->CloseScreen;

   PixmapPtr screen_pixmap = screen->GetScreenPixmap(screen);
   glamor_egl_release_pixmap_private(screen_pixmap);

   eglDestroyContext(glamor_egl_priv->display, glamor_egl_priv->context);
   eglTerminate(glamor_egl_priv->display);

   free(glamor_egl_priv);
   return screen->CloseScreen(screen);
}

Bool
glamor_egl_init(ScreenPtr screen)
{
   struct glamor_egl_screen_private *glamor_egl_priv;

   glamor_egl_priv = calloc(1, sizeof(*glamor_egl_priv));
   if (!glamor_egl_priv)
      FatalError("glamor_egl: Failed to allocate screen private\n");

   if (!dixRegisterPrivateKey(&glamor_screen_private_key, PRIVATE_SCREEN, 0))
      FatalError("glamor_egl: Failed to allocate screen private\n");

   glamor_egl_set_screen_private(screen, glamor_egl_priv);

   if (!dixRegisterPrivateKey(&glamor_egl_pixmap_private_key, PRIVATE_PIXMAP,
          sizeof(struct glamor_egl_pixmap_private)))
      FatalError("glamor_egl: Failed to allocate pixmap private\n");

   if (!glamor_init(screen, GLAMOR_USE_EGL_SCREEN))
      return FALSE;

   glamor_egl_priv->CreatePixmap = screen->CreatePixmap;
   screen->CreatePixmap = glamor_egl_create_pixmap;
   glamor_egl_priv->DestroyPixmap = screen->DestroyPixmap;
   screen->DestroyPixmap = glamor_egl_destroy_pixmap;
   glamor_egl_priv->CloseScreen = screen->CloseScreen;
   screen->CloseScreen = glamor_egl_close_screen;
   return TRUE;
}

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
      FatalError("glamor_egl: Failed to make EGL context current\n");
   }
}

Bool
glamor_glx_screen_init(struct glamor_context *glamor_ctx)
{
   return FALSE;
}

void
glamor_egl_screen_init(ScreenPtr screen, struct glamor_context *glamor_ctx)
{
   struct glamor_egl_screen_private *glamor_egl_priv =
      glamor_egl_get_screen_private(screen);
   EGLConfig egl_config;
   int num_egl_config;

   glamor_egl_priv->display =
      glamor_egl_get_display(EGL_PLATFORM_ANDROID_KHR, EGL_DEFAULT_DISPLAY);

   if (!glamor_egl_priv->display)
      FatalError("eglGetDisplay() failed\n");

   if (!eglInitialize(glamor_egl_priv->display, NULL, NULL))
      FatalError("eglInitialize() failed\n");

   if (!epoxy_has_gl_extension("EGL_KHR_surfaceless_context") &&
       epoxy_egl_version(glamor_egl_priv->display) < 15)
      FatalError("glamor_egl: glamor requires EGL_KHR_surfaceless_context\n");

   if (!eglBindAPI(EGL_OPENGL_ES_API))
      FatalError("glamor_egl: Failed to bind either GLES APIs.\n");

   if (!eglChooseConfig(
          glamor_egl_priv->display, NULL, &egl_config, 1, &num_egl_config))
      FatalError("glamor_egl: No acceptable EGL configs found\n");

   static const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

   glamor_egl_priv->context = eglCreateContext(
      glamor_egl_priv->display, egl_config, EGL_NO_CONTEXT, context_attribs);

   if (glamor_egl_priv->context == EGL_NO_CONTEXT)
      FatalError("glamor_egl: Failed to create GLES2 contexts\n");

   if (!eglMakeCurrent(glamor_egl_priv->display, EGL_NO_SURFACE,
          EGL_NO_SURFACE, glamor_egl_priv->context))
      FatalError("glamor_egl: Failed to make GLES2 context current\n");

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

LorieBuffer *
glamor_egl_get_pixmap_lorie_buffer(PixmapPtr pixmap)
{
   struct glamor_egl_pixmap_private *pixmap_priv =
      glamor_egl_get_pixmap_private(pixmap);
   return pixmap_priv->lorie_buffer;
}
