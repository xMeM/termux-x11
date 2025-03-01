#include "lorie.h"

#include "buffer.h"
#include "pixmap.h"
#include "vulkan_shm.h"

Bool glamor_egl_init(ScreenPtr screen);

PixmapPtr glamor_egl_create_pixmap(ScreenPtr screen, int w, int h, int depth,
                                   unsigned int usage);

PixmapPtr glamor_egl_create_pixmap_from_ahardware_buffer(
   ScreenPtr screen, int depth, AHardwareBuffer *ahardware_buffer);

PixmapPtr
glamor_egl_create_pixmap_from_opaque_fd(ScreenPtr screen, int w, int h,
                                        int depth, size_t size,
                                        uint32_t offset, int fd);

PixmapPtr
glamor_egl_create_pixmap_from_lorie_buffer(ScreenPtr screen, int depth,
                                           LorieBuffer *lorie_buffer);

LorieBuffer *glamor_egl_get_pixmap_lorie_buffer(PixmapPtr pixmap);

void glamor_egl_set_pixmap_bo(PixmapPtr pixmap, struct vk_shm_bo *bo);
void glamor_egl_set_pixmap_lorie_buffer(PixmapPtr pixmap, LorieBuffer *lb);
