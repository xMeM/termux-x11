#include "private.h"

#include "dri3.h"

#include <sys/mman.h>

DevPrivateKeyRec vk_screen_private_key;
DevPrivateKeyRec vk_pixmap_private_key;
DevPrivateKeyRec vk_gc_private_key;

static struct vk_render_cb *vk_alloc_cb(struct vk_screen_private *scrnPriv) {
  struct vk_render_cb *cb;
  xorg_list_for_each_entry(cb, &scrnPriv->list_of_cmdbufs, link) {
    VkResult res = WaitForFences(scrnPriv->dev, 1, &cb->fence, VK_TRUE, 0);
    if (res == VK_SUCCESS) {
      ResetFences(scrnPriv->dev, 1, &cb->fence);
      cb->wait_semaphore_count = 0;
      cb->signal_semaphore_count = 0;
      return cb;
    }
  }

  cb = calloc(1, sizeof(*cb));

  if (cb == NULL)
    return NULL;

  VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandPool = scrnPriv->pool,
      .commandBufferCount = 1,
  };

  VkResult res = AllocateCommandBuffers(scrnPriv->dev, &alloc_info, &cb->cb);

  if (res != VK_SUCCESS) {
    LOG_ERROR("failed to allocate command buffer");
    free(cb);
    return NULL;
  }

  VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
  };

  res = CreateFence(scrnPriv->dev, &fence_info, NULL, &cb->fence);

  if (res != VK_SUCCESS) {
    LOG_ERROR("failed to create fence");
    FreeCommandBuffers(scrnPriv->dev, scrnPriv->pool, 1, &cb->cb);
    free(cb);
    return NULL;
  }

  xorg_list_add(&cb->link, &scrnPriv->list_of_cmdbufs);
  cb->scrnPriv = scrnPriv;
  return cb;
}

static void ChangeLayout(VkCommandBuffer cb, VkImage image,
                         VkAccessFlags srcAccessMask,
                         VkAccessFlags dstAccessMask, VkImageLayout oldLayout,
                         VkImageLayout newLayout,
                         VkPipelineStageFlags srcStageMask,
                         VkPipelineStageFlags dstStageMask,
                         VkDependencyFlags dependencyFlags) {
  VkImageSubresourceRange range = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .layerCount = 1,
      .baseArrayLayer = 0,
      .levelCount = 1,
      .baseMipLevel = 0,
  };

  VkImageMemoryBarrier barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = NULL,
      .image = image,
      .oldLayout = oldLayout,
      .newLayout = newLayout,
      .srcAccessMask = srcAccessMask,
      .dstAccessMask = dstAccessMask,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .subresourceRange = range,
  };
  CmdPipelineBarrier(cb, srcStageMask, dstStageMask, dependencyFlags, 0, NULL,
                     0, NULL, 1, &barrier);
}

static struct vk_render_cb *
vk_render_begin(struct vk_screen_private *scrnPriv) {
  struct vk_render_cb *cb;

  if ((cb = vk_alloc_cb(scrnPriv)) == NULL)
    return NULL;

  VkCommandBufferBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };

  BeginCommandBuffer(cb->cb, &begin);

  return cb;
}

static void vk_render_end(struct vk_render_cb *cb) {
  EndCommandBuffer(cb->cb);

  VkTimelineSemaphoreSubmitInfo timeline_submit = {
      .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
      .pNext = NULL,
      .waitSemaphoreValueCount = cb->wait_semaphore_count,
      .pWaitSemaphoreValues = cb->wait_timeline_points,
      .signalSemaphoreValueCount = cb->signal_semaphore_count,
      .pSignalSemaphoreValues = cb->signal_timeline_points,
  };

  VkSubmitInfo submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = &timeline_submit,
      .commandBufferCount = 1,
      .pCommandBuffers = &cb->cb,
      .waitSemaphoreCount = cb->wait_semaphore_count,
      .pWaitSemaphores = cb->wait_timeline_semaphores,
      .pWaitDstStageMask = cb->wait_stage_masks,
      .signalSemaphoreCount = cb->signal_semaphore_count,
      .pSignalSemaphores = cb->signal_timeline_semaphores,
  };

  QueueSubmit(cb->scrnPriv->queue, 1, &submit, cb->fence);

  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .pNext = NULL,
      .flags = 0,
      .semaphoreCount = cb->signal_semaphore_count,
      .pSemaphores = cb->signal_timeline_semaphores,
      .pValues = cb->signal_timeline_points,
  };

  WaitSemaphores(cb->scrnPriv->dev, &wait_info, UINT64_MAX);
}

static void AddWaitSemaphore(struct vk_render_cb *cb, VkSemaphore sem,
                             uint64_t point, VkPipelineStageFlags wait_stage) {
  if (cb->wait_semaphore_count < 8) {
    int i = cb->wait_semaphore_count++;
    cb->wait_timeline_semaphores[i] = sem;
    cb->wait_timeline_points[i] = point;
    cb->wait_stage_masks[i] = wait_stage;
  }
}

static void AddSignalSemaphore(struct vk_render_cb *cb, VkSemaphore sem,
                               uint64_t point) {
  if (cb->signal_semaphore_count < 8) {
    int i = cb->signal_semaphore_count++;
    cb->signal_timeline_semaphores[i] = sem;
    cb->signal_timeline_points[i] = point;
  }
}

static void vk_render_blit(struct vk_render_cb *cb,
                           struct vk_pixmap_private *src,
                           struct vk_pixmap_private *dst, int nbox,
                           const VkImageCopy *pbox) {
  ChangeLayout(cb->cb, src->image, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_READ_BIT,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT);

  ChangeLayout(cb->cb, dst->image, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_DEPENDENCY_BY_REGION_BIT);

  CmdCopyImage(cb->cb, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, nbox, pbox);

  ChangeLayout(cb->cb, src->image, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_NONE,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
               VK_DEPENDENCY_BY_REGION_BIT);

  ChangeLayout(cb->cb, dst->image, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_NONE,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
               VK_DEPENDENCY_BY_REGION_BIT);

  AddWaitSemaphore(cb, dst->timeline_semaphore, dst->timeline_point++,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
  AddSignalSemaphore(cb, dst->timeline_semaphore, dst->timeline_point);
}

static void vk_pixmap_lock(struct vk_pixmap_private *pixPriv) {
  if (pixPriv->image == VK_NULL_HANDLE)
    return;

  uint64_t timeline_point = pixPriv->timeline_point++;

  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .pNext = NULL,
      .flags = 0,
      .semaphoreCount = 1,
      .pSemaphores = &pixPriv->timeline_semaphore,
      .pValues = &timeline_point,
  };

  WaitSemaphores(pixPriv->scrnPriv->dev, &wait_info, UINT64_MAX);
}

static void vk_pixmap_unlock(struct vk_pixmap_private *pixPriv) {
  if (pixPriv->image == VK_NULL_HANDLE)
    return;

  VkSemaphoreSignalInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
      .pNext = NULL,
      .value = pixPriv->timeline_point,
      .semaphore = pixPriv->timeline_semaphore,
  };

  SignalSemaphore(pixPriv->scrnPriv->dev, &signal_info);
}
static void vk_render_copy(DrawablePtr pSrcDrawable, DrawablePtr pDstDrawable,
                           GCPtr pGC, BoxPtr pbox, int nbox, int dx, int dy,
                           Bool reverse, Bool upsidedown, Pixel bitplane,
                           void *closure) {
  PixmapPtr srcPixmap = get_drawable_pixmap(pSrcDrawable);
  PixmapPtr dstPixmap = get_drawable_pixmap(pDstDrawable);
  struct vk_pixmap_private *srcPriv = vk_pixmap_priv(srcPixmap);
  struct vk_pixmap_private *dstPriv = vk_pixmap_priv(dstPixmap);
  struct vk_gc_private *gcPriv = vk_gc_priv(pGC);

  if (pSrcDrawable->width < 1 || pSrcDrawable->height < 1 ||
      pDstDrawable->width < 1 || pDstDrawable->height < 1)
    return;

  if (srcPriv->image == VK_NULL_HANDLE || dstPriv->image == VK_NULL_HANDLE)
    return fbCopyNtoN(pSrcDrawable, pDstDrawable, pGC, pbox, nbox, dx, dy,
                      reverse, upsidedown, bitplane, closure);

  FbBits *src;
  FbStride srcStride;
  int srcBpp;
  int srcXoff, srcYoff;
  FbBits *dst;
  FbStride dstStride;
  int dstBpp;
  int dstXoff, dstYoff;

  fbGetDrawable(pSrcDrawable, src, srcStride, srcBpp, srcXoff, srcYoff);
  fbGetDrawable(pDstDrawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

  struct vk_render_cb *cmds = vk_render_begin(gcPriv->scrnPriv);
  if (cmds != NULL) {
    VkImageCopy pRegions[nbox];

    for (int i = 0u; i < nbox; i++) {
      BoxPtr box = &pbox[i];
      pRegions[i] = (VkImageCopy){
          .srcOffset.x = box->x1 + dx + srcXoff,
          .srcOffset.y = box->y1 + dy + srcYoff,
          .srcOffset.z = 0,
          .dstOffset.x = box->x1 + dstXoff,
          .dstOffset.y = box->y1 + dstYoff,
          .dstOffset.z = 0,
          .extent.width = box->x2 - box->x1,
          .extent.height = box->y2 - box->y1,
          .extent.depth = 1,
      };
      pRegions[i].srcSubresource = pRegions[i].dstSubresource =
          (VkImageSubresourceLayers){
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
              .baseArrayLayer = 0,
              .mipLevel = 0,
          };
    }
    vk_render_blit(cmds, srcPriv, dstPriv, nbox, pRegions);

    vk_render_end(cmds);
  }

  fbFinishAccess(pSrcDrawable);
  fbFinishAccess(pDstDrawable);
}

static RegionPtr CopyArea(DrawablePtr pSrcDrawable, DrawablePtr pDstDrawable,
                          GCPtr pGC, int xIn, int yIn, int widthSrc,
                          int heightSrc, int xOut, int yOut) {
  return miDoCopy(pSrcDrawable, pDstDrawable, pGC, xIn, yIn, widthSrc,
                  heightSrc, xOut, yOut, vk_render_copy, 0, 0);
}

static void PutImage(DrawablePtr pDrawable, GCPtr pGC, int depth, int x, int y,
                     int w, int h, int leftPad, int format, char *pImage) {
  PixmapPtr pPix = get_drawable_pixmap(pDrawable);
  struct vk_pixmap_private *pixPriv = vk_pixmap_priv(pPix);

  if (pDrawable->width < 1 || pDrawable->height < 1)
    return;

  vk_pixmap_lock(pixPriv);

  fbPutImage(pDrawable, pGC, depth, x, y, w, h, leftPad, format, pImage);

  vk_pixmap_unlock(pixPriv);
}

const GCOps vk_gc_ops = {
    fbFillSpans,     fbSetSpans,      PutImage,       CopyArea,
    fbCopyPlane,     fbPolyPoint,     fbPolyLine,     fbPolySegment,
    fbPolyRectangle, fbPolyArc,       miFillPolygon,  fbPolyFillRect,
    fbPolyFillArc,   miPolyText8,     miPolyText16,   miImageText8,
    miImageText16,   fbImageGlyphBlt, fbPolyGlyphBlt, fbPushPixels,
};

static Bool vk_render_CreateGC(GCPtr pGC) {
  struct vk_gc_private *gcPriv = vk_gc_priv(pGC);
  gcPriv->scrnPriv = vk_screen_priv(pGC->pScreen);

  fbCreateGC(pGC);

  pGC->ops = &vk_gc_ops;
  return TRUE;
}

static Bool vk_render_CreateScreenResources(ScreenPtr pScreen) {
  struct vk_screen_private *scrnPriv = vk_screen_priv(pScreen);

  Bool ret = scrnPriv->CreateScreenResources(pScreen);
  if (ret != TRUE)
    return FALSE;

#if defined(__TERMUX__)
  PixmapPtr old = pScreen->GetScreenPixmap(pScreen);

  PixmapPtr pix = vk_render_CreatePixmap(pScreen, pScreen->width,
                                         pScreen->height, pScreen->rootDepth,
                                         CREATE_PIXMAP_USAGE_BACKING_PIXMAP);

  if (pix == NullPixmap)
    return FALSE;

  pScreen->SetScreenPixmap(pix);

  if (old != NullPixmap)
    scrnPriv->DestroyPixmap(old);
#endif
  return TRUE;
}

void vk_pixmap_finish(struct vk_pixmap_private *pixPriv) {
  VkDevice dev = pixPriv->scrnPriv->dev;

  FreeMemory(dev, pixPriv->memory, NULL);
  DestroyImage(dev, pixPriv->image, NULL);
  DestroySemaphore(dev, pixPriv->timeline_semaphore, NULL);

  if (pixPriv->info.map)
    munmap(pixPriv->info.map, pixPriv->info.row_pitch * pixPriv->info.height);

  close(pixPriv->info.fd);
}

VkResult vk_pixmap_init(struct vk_screen_private *scrnPriv,
                        struct vk_pixmap_private *pixPriv, unsigned width,
                        unsigned height, uint32_t format, uint64_t modifier,
                        uint32_t offset, uint32_t stride, int fd) {
  if (query_drm_modifier_layers(scrnPriv->pdev, format, modifier) > 1) {
    LOG_ERROR("unsupported format");
    return VK_ERROR_FORMAT_NOT_SUPPORTED;
  }

  pixPriv->info = (struct vk_buffer_info){
      .format = format,
      .modifier = modifier,
      .width = width,
      .height = height,
      .offset = offset,
      .row_pitch = stride,
      .fd = fd,
  };

  VkSemaphoreTypeCreateInfo sem_type_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .pNext = NULL,
      .initialValue = 0,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
  };

  VkSemaphoreCreateInfo sem_create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &sem_type_info,
      .flags = 0,
  };

  VkResult res = CreateSemaphore(scrnPriv->dev, &sem_create_info, NULL,
                                 &pixPriv->timeline_semaphore);
  if (res != VK_SUCCESS) {
    LOG_ERROR("failed to create timeline semaphore");
    return res;
  }

  pixPriv->image = import_dmabuf_image(scrnPriv->pdev, scrnPriv->dev,
                                       &pixPriv->memory, &pixPriv->info);
  if (pixPriv->image == VK_NULL_HANDLE) {
    LOG_ERROR("failed to create vk image");
    goto error;
  }

  if (modifier == DRM_FORMAT_MOD_LINEAR) {
    char *map = mmap(0, pixPriv->info.row_pitch * pixPriv->info.height,
                     PROT_READ | PROT_WRITE, MAP_SHARED, pixPriv->info.fd, 0);

    if (map != MAP_FAILED)
      pixPriv->info.map = map;
    else
      LOG_ERROR("failed to mapping dmabuf");
  }

  pixPriv->scrnPriv = scrnPriv;
  return VK_SUCCESS;

error:
  DestroySemaphore(scrnPriv->dev, pixPriv->timeline_semaphore, NULL);
  return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

static PixmapPtr vk_pixmap_from_fds(ScreenPtr screen, CARD8 num_fds,
                                    const int *fds, CARD16 width, CARD16 height,
                                    const CARD32 *strides,
                                    const CARD32 *offsets, CARD8 depth,
                                    CARD8 bpp, CARD64 modifier) {
  PixmapPtr pPixmap = fbCreatePixmap(screen, 0, 0, depth, 0);

  if (pPixmap == NullPixmap)
    return NullPixmap;

  struct vk_screen_private *scrnPriv = vk_screen_priv(screen);
  struct vk_pixmap_private *pixPriv = vk_pixmap_priv(pPixmap);

  if (modifier == 1274 || modifier == DRM_FORMAT_MOD_INVALID)
    modifier = DRM_FORMAT_MOD_LINEAR;

  VkResult res = vk_pixmap_init(scrnPriv, pixPriv, width, height,
                                drm_format_for_depth(depth, bpp), modifier,
                                offsets[0], strides[0], os_dupfd_cloexec(fds[0]));
  if (res != VK_SUCCESS) {
    fbDestroyPixmap(pPixmap);
    return NullPixmap;
  }

  screen->ModifyPixmapHeader(pPixmap, width, height, 0, 0, strides[0], NULL);
  return pPixmap;
}

static int vk_fds_from_pixmap(ScreenPtr screen, PixmapPtr pixmap, int *fds,
                              uint32_t *strides, uint32_t *offsets,
                              uint64_t *modifier) {
  struct vk_pixmap_private *pixPriv = vk_pixmap_priv(pixmap);

  if (pixPriv->image == VK_NULL_HANDLE)
    return 0;

  modifier[0] = pixPriv->info.modifier;
  strides[0] = pixPriv->info.row_pitch;
  offsets[0] = pixPriv->info.offset;
  fds[0] = os_dupfd_cloexec(pixPriv->info.fd);

  return 1;
}

static int vk_get_formats(ScreenPtr screen, CARD32 *num_formats,
                          CARD32 **formats) {
  static CARD32 support_formats[] = {
      DRM_FORMAT_ARGB8888,
      DRM_FORMAT_XRGB8888,
      DRM_FORMAT_RGB565,
      DRM_FORMAT_XRGB2101010,
  };

  *num_formats = NELEMS(support_formats);
  *formats = support_formats;

  return TRUE;
}

static int vk_get_modifiers(ScreenPtr screen, uint32_t format,
                            uint32_t *num_modifiers, uint64_t **modifiers) {
  struct vk_screen_private *scrnPriv = vk_screen_priv(screen);
  if (scrnPriv->modifiers != NULL)
    goto success;

  int num_mods = query_drm_modifiers(scrnPriv->pdev, format, NULL);
  if (num_mods < 1)
    goto error;

  uint64_t *mods = malloc(num_mods * sizeof(*mods));
  if (mods == NULL)
    goto error;

  query_drm_modifiers(scrnPriv->pdev, format, mods);

  scrnPriv->num_modifiers = num_mods;
  scrnPriv->modifiers = mods;

success:
  *num_modifiers = scrnPriv->num_modifiers;
  *modifiers = scrnPriv->modifiers;

  return TRUE;

error:
  *num_modifiers = 0;
  *modifiers = NULL;
  return FALSE;
}

static int vk_get_drawable_modifiers(DrawablePtr draw, uint32_t format,
                                     uint32_t *num_modifiers,
                                     uint64_t **modifiers) {
  *num_modifiers = 0;
  *modifiers = NULL;
  return FALSE;
}

const dri3_screen_info_rec vk_dri3_info = {
    .version = 2,
    .pixmap_from_fds = vk_pixmap_from_fds,
    .fds_from_pixmap = vk_fds_from_pixmap,
    .get_formats = vk_get_formats,
    .get_modifiers = vk_get_modifiers,
    .get_drawable_modifiers = vk_get_drawable_modifiers,
};

static Bool vk_render_CloseScreen(ScreenPtr pScreen) {
  struct vk_screen_private *scrnPriv = vk_screen_priv(pScreen);

  DeviceWaitIdle(scrnPriv->dev);

  struct vk_render_cb *cb, *ncb;
  xorg_list_for_each_entry_safe(cb, ncb, &scrnPriv->list_of_cmdbufs, link) {
    xorg_list_del(&cb->link);
    DestroyFence(scrnPriv->dev, cb->fence, NULL);
    FreeCommandBuffers(scrnPriv->dev, scrnPriv->pool, 1, &cb->cb);
    free(cb);
  }

  DestroyCommandPool(scrnPriv->dev, scrnPriv->pool, NULL);
  DestroyDevice(scrnPriv->dev, NULL);
  DestroyInstance(scrnPriv->instance, NULL);

#define UNWARP(x) pScreen->x = scrnPriv->x;
  SCREEN_PROCS_LIST(UNWARP)
#undef UNWARP

  if (scrnPriv->num_modifiers)
    free(scrnPriv->modifiers);

  free(scrnPriv);

  return pScreen->CloseScreen(pScreen);
}

Bool vk_render_screen_init(ScreenPtr pScreen) {
  struct vk_screen_private *scrnPriv;

  if ((scrnPriv = calloc(1, sizeof(*scrnPriv))) == NULL)
    return FALSE;

  if (!dixRegisterPrivateKey(&vk_screen_private_key, PRIVATE_SCREEN, 0)) {
    LOG_ERROR("failed to register screen private key");
    goto error_free_priv;
  }
  dixSetPrivate(&pScreen->devPrivates, &vk_screen_private_key, scrnPriv);

  if (!dixRegisterPrivateKey(&vk_pixmap_private_key, PRIVATE_PIXMAP,
                             sizeof(struct vk_pixmap_private))) {
    LOG_ERROR("failed to register pixmap private key");
    goto error_free_priv;
  }

  if (!dixRegisterPrivateKey(&vk_gc_private_key, PRIVATE_GC,
                             sizeof(struct vk_gc_private))) {
    LOG_ERROR("failed to register GC private key");
    goto error_free_priv;
  }

  if (!dri3_screen_init(pScreen, &vk_dri3_info)) {
    LOG_ERROR("dri3 screen initialze error");
    goto error_free_priv;
  }

  VkInstance inst = create_instance();
  if (inst == VK_NULL_HANDLE) {
    LOG_ERROR("failed to create vk instance");
    goto error_free_priv;
  }

  VkPhysicalDevice pdev = select_turnip_device(inst);
  if (pdev == VK_NULL_HANDLE) {
    LOG_ERROR("unsupported device");
    goto error_destroy_instance;
  }

  int queue_family_index = select_queue_family(pdev, VK_QUEUE_TRANSFER_BIT);
  if (queue_family_index < 0) {
    LOG_ERROR("tranfser queue not found");
    goto error_destroy_instance;
  }

  VkQueue queue;

  VkDevice dev = create_device(pdev, 1, &queue_family_index, &queue);
  if (dev == VK_NULL_HANDLE) {
    LOG_ERROR("failed to create vk device");
    goto error_destroy_instance;
  }

  VkCommandPoolCreateInfo pool_create_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
               VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_family_index,
  };

  VkCommandPool pool;

  VkResult res = CreateCommandPool(dev, &pool_create_info, NULL, &pool);
  if (res != VK_SUCCESS) {
    LOG_ERROR("failed to create cmdbuf pool");
    goto error_destroy_device;
  }

  *scrnPriv = (struct vk_screen_private){
      .instance = inst,
      .pdev = pdev,
      .dev = dev,
      .queue = queue,
      .pool = pool,
  };

#define WARP(x)                                                                \
  scrnPriv->x = pScreen->x;                                                    \
  pScreen->x = vk_render_##x;
  SCREEN_PROCS_LIST(WARP)
#undef WARP

  xorg_list_init(&scrnPriv->list_of_cmdbufs);

  return TRUE;

error_destroy_device:
  DestroyDevice(dev, NULL);
error_destroy_instance:
  DestroyInstance(inst, NULL);
error_free_priv:
  free(scrnPriv);
  return FALSE;
}
