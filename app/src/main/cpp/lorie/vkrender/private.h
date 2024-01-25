#ifndef __VK_PRIVATE_H__
#define __VK_PRIVATE_H__

#include "dix-config.h"
#include "fb.h"
#include "list.h"
#include "public.h"

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>

#include <android/hardware_buffer.h>
#include <vulkan/vulkan.h>

#define NELEMS(x) (sizeof(x) / sizeof(*x))

#define LOG_ERROR(fmt, ...)                                                    \
  ErrorF("[%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define INSTANCE_ENTRYPOINTS_LIST(REQUIRE)                                     \
  REQUIRE(CreateInstance)                                                      \
  REQUIRE(DestroyInstance)                                                     \
  REQUIRE(GetInstanceProcAddr)                                                 \
  REQUIRE(EnumeratePhysicalDevices)                                            \
  REQUIRE(GetPhysicalDeviceProperties2)                                        \
  REQUIRE(GetPhysicalDeviceMemoryProperties)                                   \
  REQUIRE(GetPhysicalDeviceQueueFamilyProperties)                              \
  REQUIRE(CreateDevice)                                                        \
  REQUIRE(GetDeviceProcAddr)                                                   \
  REQUIRE(GetPhysicalDeviceFormatProperties2)                                  \
  REQUIRE(EnumerateInstanceExtensionProperties)                                \
  REQUIRE(EnumerateDeviceExtensionProperties)

#define DEVICE_ENTRYPOINTS_LIST(REQUIRE)                                       \
  REQUIRE(GetDeviceQueue)                                                      \
  REQUIRE(GetDeviceQueue2)                                                     \
  REQUIRE(CreateCommandPool)                                                   \
  REQUIRE(DeviceWaitIdle)                                                      \
  REQUIRE(FreeCommandBuffers)                                                  \
  REQUIRE(DestroyFence)                                                        \
  REQUIRE(DestroyCommandPool)                                                  \
  REQUIRE(EndCommandBuffer)                                                    \
  REQUIRE(BeginCommandBuffer)                                                  \
  REQUIRE(DestroyDevice)                                                       \
  REQUIRE(QueueSubmit)                                                         \
  REQUIRE(CmdCopyImage)                                                        \
  REQUIRE(CmdBlitImage)                                                        \
  REQUIRE(CreateFence)                                                         \
  REQUIRE(WaitForFences)                                                       \
  REQUIRE(ResetFences)                                                         \
  REQUIRE(CmdPipelineBarrier)                                                  \
  REQUIRE(AllocateCommandBuffers)                                              \
  REQUIRE(GetImageMemoryRequirements)                                          \
  REQUIRE(GetImageSubresourceLayout)                                           \
  REQUIRE(AllocateMemory)                                                      \
  REQUIRE(BindImageMemory)                                                     \
  REQUIRE(MapMemory)                                                           \
  REQUIRE(UnmapMemory)                                                         \
  REQUIRE(CreateSemaphore)                                                     \
  REQUIRE(FreeMemory)                                                          \
  REQUIRE(CreateImage)                                                         \
  REQUIRE(CreateImageView)                                                     \
  REQUIRE(DestroyImage)                                                        \
  REQUIRE(DestroyImageView)                                                    \
  REQUIRE(DestroySemaphore)                                                    \
  REQUIRE(SignalSemaphore)                                                     \
  REQUIRE(WaitSemaphores)                                                      \
  REQUIRE(GetImageDrmFormatModifierPropertiesEXT)                              \
  REQUIRE(CreatePipelineCache)                                                 \
  REQUIRE(DestroyPipelineCache)                                                \
  REQUIRE(CreatePipelineLayout)                                                \
  REQUIRE(DestroyPipelineLayout)                                               \
  REQUIRE(CreateRenderPass)                                                    \
  REQUIRE(DestroyRenderPass)                                                   \
  REQUIRE(CreateDescriptorSetLayout)                                           \
  REQUIRE(DestroyDescriptorSetLayout)                                          \
  REQUIRE(CreateShaderModule)                                                  \
  REQUIRE(DestroyShaderModule)                                                 \
  REQUIRE(CreateGraphicsPipelines)                                             \
  REQUIRE(DestroyPipeline)                                                     \
  REQUIRE(CmdPushConstants)                                                    \
  REQUIRE(CmdBindPipeline)                                                     \
  REQUIRE(CreateDescriptorPool)                                                \
  REQUIRE(DestroyDescriptorPool)                                               \
  REQUIRE(AllocateDescriptorSets)                                              \
  REQUIRE(FreeDescriptorSets)                                                  \
  REQUIRE(CreateFramebuffer)                                                   \
  REQUIRE(DestroyFramebuffer)                                                  \
  REQUIRE(CreateSampler)                                                       \
  REQUIRE(DestroySampler)                                                      \
  REQUIRE(UpdateDescriptorSets)                                                \
  REQUIRE(CmdEndRenderPass)                                                    \
  REQUIRE(CmdBeginRenderPass)                                                  \
  REQUIRE(CmdBindDescriptorSets)                                               \
  REQUIRE(CmdDraw)                                                             \
  REQUIRE(CmdSetViewport)                                                      \
  REQUIRE(CmdSetScissor)                                                       \
  REQUIRE(GetMemoryFdKHR)                                                      \
  REQUIRE(GetMemoryFdPropertiesKHR)                                            \
  REQUIRE(BindImageMemory2)                                                    \
  REQUIRE(GetImageMemoryRequirements2)                                         \
  REQUIRE(ImportSemaphoreFdKHR)                                                \
  REQUIRE(GetSemaphoreFdKHR)

#define SCREEN_PROCS_LIST(REQUIRE)                                             \
  REQUIRE(CreatePixmap)                                                        \
  REQUIRE(DestroyPixmap)                                                       \
  REQUIRE(CloseScreen)                                                         \
  REQUIRE(CreateGC)                                                            \
  REQUIRE(CreateScreenResources)

#define DEFINE_ENTRYPOINTS(x) extern PFN_vk##x x;
INSTANCE_ENTRYPOINTS_LIST(DEFINE_ENTRYPOINTS)
DEVICE_ENTRYPOINTS_LIST(DEFINE_ENTRYPOINTS)
#undef DEFINE_ENTRYPOINTS

struct vk_render_cb {
  VkCommandBuffer cb;
  VkFence fence;
  struct xorg_list link;
  struct vk_screen_private *scrnPriv;
  int wait_semaphore_count;
  uint64_t wait_timeline_points[8];
  VkPipelineStageFlags wait_stage_masks[8];
  VkSemaphore wait_timeline_semaphores[8];
  int signal_semaphore_count;
  uint64_t signal_timeline_points[8];
  VkSemaphore signal_timeline_semaphores[8];
};

struct vk_buffer_info {
  uint32_t offset;
  uint32_t row_pitch;
  uint64_t modifier;
  unsigned width, height;
  uint32_t format;
  char *map;
  int fd;
};

struct vk_screen_private {
  VkInstance instance;
  VkPhysicalDevice pdev;
  VkDevice dev;
  VkQueue queue;
  VkCommandPool pool;
  struct xorg_list list_of_cmdbufs;

  unsigned num_modifiers;
  uint64_t *modifiers;

#define SAVE_PROCS(x) x##ProcPtr x;
  SCREEN_PROCS_LIST(SAVE_PROCS);
#undef SAVE_PROCS
};

struct vk_pixmap_private {
  VkImage image;
  VkDeviceMemory memory;
  VkSemaphore timeline_semaphore;
  uint64_t timeline_point;
  struct vk_render *render;
  struct vk_buffer_info info;
  struct vk_screen_private *scrnPriv;
};

struct vk_gc_private {
  struct vk_screen_private *scrnPriv;
};

inline int os_dupfd_cloexec(int fd) {
  int minfd = 3;
  int newfd = fcntl(fd, F_DUPFD_CLOEXEC, minfd);

  if (newfd >= 0)
    return newfd;

  if (errno != EINVAL)
    return -1;

  newfd = fcntl(fd, F_DUPFD, minfd);

  if (newfd < 0)
    return -1;

  long flags = fcntl(newfd, F_GETFD);
  if (flags == -1) {
    close(newfd);
    return -1;
  }

  if (fcntl(newfd, F_SETFD, flags | FD_CLOEXEC) == -1) {
    close(newfd);
    return -1;
  }

  return newfd;
}

VkInstance create_instance(void);
VkDevice create_device(VkPhysicalDevice pdev, unsigned queue_family_count,
                       const int *queue_family_index, VkQueue *queue);
VkPhysicalDevice select_turnip_device(VkInstance instance);
int select_queue_family(VkPhysicalDevice pdev, VkQueueFlags flags);
unsigned query_drm_modifiers(VkPhysicalDevice pdev, uint32_t format,
                             uint64_t *modifiers);
VkImage import_dmabuf_image(VkPhysicalDevice pdev, VkDevice dev,
                            VkDeviceMemory *memory,
                            const struct vk_buffer_info *info);
unsigned query_drm_modifier_layers(VkPhysicalDevice pdev, uint32_t format,
                                   uint64_t modifier);
VkResult vk_pixmap_init(struct vk_screen_private *scrnPriv,
                        struct vk_pixmap_private *pixPriv, unsigned width,
                        unsigned height, uint32_t format, uint64_t modifier,
                        uint32_t offset, uint32_t stride, int fd);
void vk_pixmap_finish(struct vk_pixmap_private *pixPriv);

extern DevPrivateKeyRec vk_screen_private_key;
extern DevPrivateKeyRec vk_pixmap_private_key;
extern DevPrivateKeyRec vk_gc_private_key;

inline struct vk_screen_private *vk_screen_priv(ScreenPtr pScreen) {
  return dixGetPrivate(&pScreen->devPrivates, &vk_screen_private_key);
}

inline struct vk_pixmap_private *vk_pixmap_priv(PixmapPtr pPix) {
  return dixLookupPrivate(&pPix->devPrivates, &vk_pixmap_private_key);
}

inline struct vk_gc_private *vk_gc_priv(GCPtr pGC) {
  return dixLookupPrivate(&pGC->devPrivates, &vk_gc_private_key);
}

inline PixmapPtr get_drawable_pixmap(DrawablePtr drawable) {
  if (drawable->type == DRAWABLE_WINDOW)
    return drawable->pScreen->GetWindowPixmap((WindowPtr)drawable);
  else
    return (PixmapPtr)drawable;
}

typedef struct native_handle {
  int version; /* sizeof(native_handle_t) */
  int numFds;  /* number of file-descriptors at &data[0] */
  int numInts; /* number of ints at &data[numFds] */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#endif
  int data[0]; /* numFds + numInts ints */
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
} native_handle_t;

const native_handle_t *
AHardwareBuffer_getNativeHandle(const AHardwareBuffer *buffer);

#endif // !__VK_PRIVATE_H__
