#include "lorie.h"

#include "buffer.h"
#include "pixmap.h"

Bool glamor_egl_init(ScreenPtr screen);

PixmapPtr glamor_egl_create_pixmap(
   ScreenPtr screen, int w, int h, int depth, unsigned int usage);

PixmapPtr glamor_egl_create_pixmap_from_ahardware_buffer(
   ScreenPtr screen, int depth, AHardwareBuffer *ahardware_buffer);

PixmapPtr glamor_egl_create_pixmap_from_opaque_fd(ScreenPtr screen, int w,
   int h, int depth, size_t size, uint32_t offset, int fd);

LorieBuffer *glamor_egl_get_pixmap_lorie_buffer(PixmapPtr pixmap);
