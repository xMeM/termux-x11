#ifndef __VK_RENDER_H__
#define __VK_RENDER_H__

#include "dix-config.h"
#include "drm/drm_fourcc.h"
#include "fb.h"

#include <android/hardware_buffer.h>
#include <unistd.h>

Bool vk_render_screen_init(ScreenPtr pScreen);

PixmapPtr vk_render_CreatePixmap(ScreenPtr pScreen, int width, int height,
                                 int depth, unsigned int usage_hint);

Bool vk_render_DestroyPixmap(PixmapPtr pPixmap);

Bool vk_render_PixmapUpdateBuffer(ScreenPtr pScreen, PixmapPtr pPix,
                                  const AHardwareBuffer *buffer);

#endif // !__VK_RENDER_H__
