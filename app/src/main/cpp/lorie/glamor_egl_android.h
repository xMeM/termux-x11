#include "lorie.h"
#include "pixmap.h"
#include "buffer.h"

Bool glamor_egl_init(ScreenPtr screen);

PixmapPtr glamor_egl_create_pixmap(
   ScreenPtr screen, int w, int h, int depth, unsigned int usage);

PixmapPtr glamor_egl_create_pixmap_from_ahardware_buffer(
   ScreenPtr screen, int depth, AHardwareBuffer *ahardware_buffer);

LorieBuffer *glamor_egl_get_pixmap_lorie_buffer(PixmapPtr pixmap);
