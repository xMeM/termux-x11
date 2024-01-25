#include "private.h"

#include <unistd.h>

Bool vk_render_PixmapUpdateBuffer(ScreenPtr pScreen, PixmapPtr pPixmap,
                                  const AHardwareBuffer *buffer) {
  AHardwareBuffer_Desc desc;

  AHardwareBuffer_describe(buffer, &desc);

  if (desc.layers != 1)
    return FALSE;

  int bpp = 4, fd = -1;
  uint32_t offset = 0;
  uint32_t row_pitch = desc.stride * bpp;
  size_t size = row_pitch * desc.height;

  uint32_t format = DRM_FORMAT_ARGB8888;
  uint64_t modifier = DRM_FORMAT_MOD_LINEAR;

  const native_handle_t *handle = AHardwareBuffer_getNativeHandle(buffer);

  for (int i = 0u; i < handle->numFds; i++) {
    size_t fd_size = lseek(handle->data[i], 0, SEEK_END);

    if (fd_size < size)
      continue;

    fd = os_dupfd_cloexec(handle->data[i]);
    break;
  }

  if (fd < 0)
    return FALSE;

  struct vk_pixmap_private *pixPriv = vk_pixmap_priv(pPixmap);
  struct vk_screen_private *scrnPriv = vk_screen_priv(pScreen);
  struct vk_pixmap_private new = {0};

  VkResult res = vk_pixmap_init(scrnPriv, &new, desc.width, desc.height, format,
                                modifier, offset, row_pitch, fd);
  if (res != VK_SUCCESS) {
    close(fd);
    return FALSE;
  }

  if (pixPriv->image != VK_NULL_HANDLE)
    vk_pixmap_finish(pixPriv);

  memcpy(pixPriv, &new, sizeof(new));

  pPixmap->drawable.pScreen->ModifyPixmapHeader(
      pPixmap, desc.width, desc.height, -1, -1, pixPriv->info.row_pitch,
      new.info.map);
  return TRUE;
}

PixmapPtr vk_render_CreatePixmap(ScreenPtr pScreen, int width, int height,
                                 int depth, unsigned int usage_hint) {
  if (usage_hint == CREATE_PIXMAP_USAGE_GLYPH_PICTURE ||
      (width == 0 && height == 0) || (width < 64 && height < 16))
    return fbCreatePixmap(pScreen, width, height, depth, usage_hint);

  PixmapPtr pPixmap = fbCreatePixmap(pScreen, 0, 0, depth, usage_hint);

  AHardwareBuffer *buf;
  AHardwareBuffer_Desc desc = {
      .format = 5,
      .width = width,
      .height = height,
      .layers = 1,
      .usage = AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
               AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT,
  };

  if (AHardwareBuffer_allocate(&desc, &buf)) {
    LOG_ERROR("failed to allocate AHardwareBuffer");
    goto error_destroy_pixmap;
  }

  if (!vk_render_PixmapUpdateBuffer(pScreen, pPixmap, buf)) {
    LOG_ERROR("failed to set pixmap buffer");
    goto error_ahwb_release;
  }

  AHardwareBuffer_release(buf);
  return pPixmap;

error_ahwb_release:
  AHardwareBuffer_release(buf);
error_destroy_pixmap:
  fbDestroyPixmap(pPixmap);
  return NullPixmap;
}

Bool vk_render_DestroyPixmap(PixmapPtr pPixmap) {
  struct vk_pixmap_private *pixPriv = vk_pixmap_priv(pPixmap);

  if (pPixmap->refcnt == 1 && pixPriv->image != VK_NULL_HANDLE) {
    vk_pixmap_finish(pixPriv);
  }
  return fbDestroyPixmap(pPixmap);
}
