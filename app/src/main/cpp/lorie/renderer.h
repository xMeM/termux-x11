#pragma once
#include <jni.h>
#include <android/hardware_buffer.h>

// X server is already linked to mesa so linking to Android's GLESv2 will confuse the linker.
// That is a reason why we should compile renderer as separate hared library with its own dependencies.
// In that case part of X server's api is unavailable,
// so we should pass addresses to all needed functions to the renderer lib.
typedef void (*renderer_message_func_type) (int type, int verb, const char *format, ...);
__unused void renderer_message_func(renderer_message_func_type function);

__unused int renderer_init(JNIEnv* env, int* legacy_drawing, uint8_t* flip, int* glamor);
__unused void renderer_set_buffer(JNIEnv* env, AHardwareBuffer* buffer);
__unused void renderer_set_texture(uint32_t tex, uint32_t width, uint32_t height);
__unused void renderer_set_window(JNIEnv* env, jobject surface, AHardwareBuffer* buffer);
__unused int renderer_should_redraw(void);
__unused int renderer_redraw(JNIEnv* env, uint8_t flip, uint64_t presentTime);
__unused void renderer_print_fps(float millis);

__unused void renderer_update_root(int w, int h, void* data, uint8_t flip);
__unused void renderer_update_cursor(int w, int h, int xhot, int yhot, void* data);
__unused void renderer_set_cursor_coordinates(int x, int y);

__unused void *renderer_image_from_buffer(AHardwareBuffer* buffer);
__unused uint32_t renderer_texture_from_image(void *egl_image);
__unused void renderer_destroy_image(void *egl_image);

#define AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM 5 // Stands to HAL_PIXEL_FORMAT_BGRA_8888
