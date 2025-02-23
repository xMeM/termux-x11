#include <fcntl.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vulkan/vulkan_core.h>

#include "vulkan_shm.h"

struct vk_shm_allocator {
   VkInstance instance;
   VkPhysicalDevice physical_dev;
   VkPhysicalDeviceMemoryProperties memory_props;
   VkDevice device;

   PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR;
};

struct vk_shm_bo {
   VkImage image;
   VkDeviceMemory memory;
   uint32_t stride;
   uint32_t offset;
   size_t size;
   int fd;
   unsigned int width, height, depth;
   struct vk_shm_allocator *allocator;
};

static const char *instance_exts[] = {
   "VK_KHR_external_memory_capabilities",
};

static const char *device_exts[] = {
   "VK_KHR_external_memory",
   "VK_KHR_external_memory_fd",
   "VK_KHR_dedicated_allocation",
};

void
vk_shm_bo_destroy(struct vk_shm_bo *bo)
{
   if (bo->image)
      vkDestroyImage(bo->allocator->device, bo->image, NULL);
   if (bo->memory)
      vkFreeMemory(bo->allocator->device, bo->memory, NULL);
   if (bo->fd != -1)
      close(bo->fd);
   free(bo);
}

static struct vk_shm_bo *
vk_shm_bo_create_internal(struct vk_shm_allocator *allocator,
   unsigned int width, unsigned int height, unsigned int depth,
   VkFormat format, bool linear, int fd)
{
   struct vk_shm_bo *bo;
   VkResult result;

   bo = calloc(1, sizeof(*bo));
   if (!bo)
      return NULL;

   bo->allocator = allocator;
   bo->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
   bo->width = width;
   bo->height = height;
   bo->depth = depth;

   const VkExternalMemoryImageCreateInfo emici = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
   };
   const VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &emici,
      .flags = VK_IMAGE_CREATE_ALIAS_BIT,
      .arrayLayers = 1,
      .mipLevels = 1,
      .extent.width = width,
      .extent.height = height,
      .extent.depth = 1,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = format,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .queueFamilyIndexCount = 1,
      .pQueueFamilyIndices = &(uint32_t){0},
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .tiling = linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
      .usage = linear ? VK_IMAGE_USAGE_TRANSFER_DST_BIT
                      : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT,
   };
   result = vkCreateImage(allocator->device, &ici, NULL, &bo->image);
   if (result != VK_SUCCESS)
      goto fail;

   VkMemoryRequirements reqs;
   vkGetImageMemoryRequirements(allocator->device, bo->image, &reqs);

   uint32_t memory_type_index = 0;
   for (int i = 0; i < allocator->memory_props.memoryTypeCount; i++) {
      if (allocator->memory_props.memoryTypes[i].propertyFlags &
          (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
         memory_type_index = i;
         break;
      }
   }

   VkMemoryDedicatedAllocateInfo mdai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .image = bo->image,
      .buffer = VK_NULL_HANDLE,
   };
   VkImportMemoryFdInfoKHR imfi = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &mdai,
      .fd = fd,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
   };
   VkExportMemoryAllocateInfo emai = {
      .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .pNext = &mdai,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
   };
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = reqs.size,
      .memoryTypeIndex = memory_type_index,
   };
   if (bo->fd != -1)
      mai.pNext = &imfi;
   else
      mai.pNext = &emai;
   result = vkAllocateMemory(allocator->device, &mai, NULL, &bo->memory);
   if (result != VK_SUCCESS)
      goto fail;

   result = vkBindImageMemory(
      allocator->device, bo->image, bo->memory, VK_WHOLE_SIZE);
   if (result != VK_SUCCESS)
      goto fail;

   if (bo->fd < 0) {
      VkMemoryGetFdInfoKHR mgfi = {
         .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
         .pNext = NULL,
         .memory = bo->memory,
         .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
      };
      result = allocator->vkGetMemoryFdKHR(allocator->device, &mgfi, &bo->fd);
      if (result != VK_SUCCESS)
         goto fail;
   }

   VkImageSubresource isr = {
      .arrayLayer = 0,
      .mipLevel = 0,
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
   };
   VkSubresourceLayout layout;
   vkGetImageSubresourceLayout(allocator->device, bo->image, &isr, &layout);

   bo->size = reqs.size;
   bo->stride = layout.rowPitch;
   bo->offset = layout.offset;
   return bo;

fail:
   vk_shm_bo_destroy(bo);
   return NULL;
}

struct vk_shm_bo *
vk_shm_bo_create(struct vk_shm_allocator *allocator, unsigned int width,
   unsigned int height, unsigned int depth, VkFormat format, bool linear)
{
   return vk_shm_bo_create_internal(
      allocator, width, height, depth, format, linear, -1);
}

struct vk_shm_bo *
vk_shm_bo_import(struct vk_shm_allocator *allocator, unsigned int width,
   unsigned int height, unsigned int depth, VkFormat format, bool linear,
   int fd)
{
   if (fd < 0)
      return NULL;

   return vk_shm_bo_create_internal(
      allocator, width, height, depth, format, linear, fd);
}

int
vk_shm_bo_fd(struct vk_shm_bo *bo)
{
   return bo->fd;
}

uint32_t
vk_shm_bo_stride(struct vk_shm_bo *bo)
{
   return bo->stride;
}

uint32_t
vk_shm_bo_offset(struct vk_shm_bo *bo)
{
   return bo->offset;
}

size_t
vk_shm_bo_size(struct vk_shm_bo *bo)
{
   return bo->size;
}

unsigned int
vk_shm_bo_width(struct vk_shm_bo *bo)
{
   return bo->width;
}

unsigned int
vk_shm_bo_height(struct vk_shm_bo *bo)
{
   return bo->height;
}

unsigned int
vk_shm_bo_depth(struct vk_shm_bo *bo)
{
   return bo->depth;
}

void *
vk_shm_bo_map(struct vk_shm_bo *bo)
{
   void *address = NULL;
   vkMapMemory(
      bo->allocator->device, bo->memory, bo->offset, bo->size, 0, &address);
   return address;
}

void
vk_shm_bo_unmap(struct vk_shm_bo *bo)
{
   vkUnmapMemory(bo->allocator->device, bo->memory);
}

void
vk_shm_allocator_destroy(struct vk_shm_allocator *allocator)
{
   if (allocator->device)
      vkDestroyDevice(allocator->device, NULL);
   if (allocator->instance)
      vkDestroyInstance(allocator->instance, NULL);
   free(allocator);
}

struct vk_shm_allocator *
vk_shm_allocator_create(void)
{
   struct vk_shm_allocator *allocator;
   VkResult result;

   allocator = calloc(1, sizeof(*allocator));
   if (!allocator)
      return NULL;

   const VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vulkan shm allocator",
      .apiVersion = VK_API_VERSION_1_1,
   };
   const VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
      .enabledExtensionCount =
         (sizeof instance_exts / sizeof instance_exts[0]),
      .ppEnabledExtensionNames = instance_exts,
   };
   result = vkCreateInstance(&ici, NULL, &allocator->instance);
   if (result != VK_SUCCESS)
      goto fail;

   uint32_t physical_dev_count = 1;
   result = vkEnumeratePhysicalDevices(
      allocator->instance, &physical_dev_count, &allocator->physical_dev);
   if (result != VK_SUCCESS && allocator->physical_dev == VK_NULL_HANDLE)
      goto fail;

   vkGetPhysicalDeviceMemoryProperties(
      allocator->physical_dev, &allocator->memory_props);

   const VkDeviceQueueCreateInfo dqci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 1,
      .queueFamilyIndex = 0,
      .pQueuePriorities = &(float){1.0f},
   };
   const VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &dqci,
      .enabledExtensionCount = (sizeof device_exts / sizeof device_exts[0]),
      .ppEnabledExtensionNames = device_exts,
   };
   result =
      vkCreateDevice(allocator->physical_dev, &dci, NULL, &allocator->device);
   if (result != VK_SUCCESS)
      goto fail;

   allocator->vkGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(
      allocator->device, "vkGetMemoryFdKHR");
   return allocator;

fail:
   vk_shm_allocator_destroy(allocator);
   return NULL;
}
