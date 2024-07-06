#ifndef LORIE_GLAMOR_EGL_H
#define LORIE_GLAMOR_EGL_H

#include "pixman.h"
#include <EGL/egl.h>
#include <android/native_window.h>

void lorieGlamorInit(void);
bool lorieGlamorSetWindow(ANativeWindow *win);
void lorieGlamorSetTexture(uint32_t tex);
void lorieGlamorUpdateCursor(int w, int h, int xhot, int yhot, void *data);
void lorieGlamorSetCursorCoordinates(int x, int y);
void lorieGlamorDamageReDisplay(struct pixman_region16 *damage);
EGLImage lorieGlamorEGLCreateImageFromAHardwareBuffer(AHardwareBuffer *bo);
void lorieGlamorEGLDestroyImage(EGLImage image);
uint32_t lorieGlamorTextureFromImage(EGLImage image);
void lorieGlamorDestroyTexture(uint32_t tex);

#endif // !LORIE_GLAMOR_EGL_H
