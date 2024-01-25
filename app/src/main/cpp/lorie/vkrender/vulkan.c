#include "drm/drm_fourcc.h"
#include "private.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFINE_ENTRYPOINTS(x) PFN_vk##x x;
INSTANCE_ENTRYPOINTS_LIST(DEFINE_ENTRYPOINTS)
DEVICE_ENTRYPOINTS_LIST(DEFINE_ENTRYPOINTS)
#undef DEFINE_ENTRYPOINTS

static VkResult instance_dispatch_table_init(void) {
  static void *libvulkan = NULL;
  if (libvulkan == NULL) {
    char path[PATH_MAX] = {0};
    const char *prefix = getenv("PREFIX");

    strcat(path, prefix);
    strcat(path, "/lib/libvulkan.so");

    libvulkan = dlopen(path, RTLD_NOW);
    if (libvulkan == NULL) {
      LOG_ERROR("%s", dlerror());
      return VK_ERROR_INITIALIZATION_FAILED;
    }
  }
#define GET_INSTANCE_PROC(name)                                                \
  name = dlsym(libvulkan, "vk" #name);                                         \
  if (name == NULL) {                                                          \
    LOG_ERROR("%s", dlerror());                                                \
    return VK_ERROR_INITIALIZATION_FAILED;                                     \
  }

  INSTANCE_ENTRYPOINTS_LIST(GET_INSTANCE_PROC)
#undef GET_INSTANCE_PROC
  return VK_SUCCESS;
}

static VkResult device_dispatch_table_init(VkDevice dev) {
#define GET_DEVICE_PROC(name)                                                  \
  name = (void *)GetDeviceProcAddr(dev, "vk" #name);                           \
  if (name == NULL) {                                                          \
    LOG_ERROR("Failed to dispatch vk" #name);                                  \
    return VK_ERROR_INITIALIZATION_FAILED;                                     \
  }

  DEVICE_ENTRYPOINTS_LIST(GET_DEVICE_PROC)
#undef GET_DEVICE_PROC
  return VK_SUCCESS;
}

static VkResult check_instance_extensions(const unsigned count,
                                          const char **exts) {
  unsigned c;

  VkResult res = EnumerateInstanceExtensionProperties(NULL, &c, NULL);

  if (res != VK_SUCCESS)
    return res;

  VkExtensionProperties props[c];

  EnumerateInstanceExtensionProperties(NULL, &c, props);

  for (int i = 0u; i < count; i++) {
    Bool support = FALSE;

    for (int j = 0u; j < c; j++) {
      if (strcmp(exts[i], props[j].extensionName) == 0)
        support = TRUE;
    }

    if (support != TRUE) {
      LOG_ERROR("Require extension %s", exts[i]);
      return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
  }

  return VK_SUCCESS;
}

VkInstance create_instance(void) {
  VkResult res;

  res = instance_dispatch_table_init();
  if (res != VK_SUCCESS)
    return VK_NULL_HANDLE;

  const char *enable_layers[] = {
#if 0
    "VK_LAYER_KHRONOS_validation",
#endif
  };

  const char *enable_extensions[] = {
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_SURFACE_EXTENSION_NAME,
  };

  res = check_instance_extensions(NELEMS(enable_extensions), enable_extensions);
  if (res != VK_SUCCESS)
    return VK_NULL_HANDLE;

  VkApplicationInfo app_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext = NULL,
      .apiVersion = VK_API_VERSION_1_3,
      .pEngineName = "No Engine",
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .pApplicationName = "Xorg",
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
  };

  VkInstanceCreateInfo instance_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = NELEMS(enable_layers),
      .ppEnabledLayerNames = enable_layers,
      .enabledExtensionCount = NELEMS(enable_extensions),
      .ppEnabledExtensionNames = enable_extensions,
  };

  VkInstance instance;

  res = CreateInstance(&instance_info, NULL, &instance);
  if (res != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }

  return instance;
}

VkPhysicalDevice select_turnip_device(VkInstance instance) {
  unsigned gpu_count;

  VkResult result = EnumeratePhysicalDevices(instance, &gpu_count, NULL);
  if (result != VK_SUCCESS)
    return VK_NULL_HANDLE;

  if (gpu_count > 0) {
    VkPhysicalDevice gpus[gpu_count];

    EnumeratePhysicalDevices(instance, &gpu_count, gpus);

    for (int i = 0u; i < gpu_count; i++) {
      VkPhysicalDeviceDriverProperties driver_prop = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
          .pNext = NULL,
      };

      VkPhysicalDeviceProperties2 device_prop = {
          .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
          .pNext = &driver_prop,
      };

      GetPhysicalDeviceProperties2(gpus[i], &device_prop);

      if (driver_prop.driverID != VK_DRIVER_ID_MESA_TURNIP) {
        continue;
      }

      return gpus[i];
    }
  }

  return VK_NULL_HANDLE;
}

static int find_queue_family(unsigned family_count,
                             VkQueueFamilyProperties *family_prop,
                             VkQueueFlags reqs) {
  for (int i = 0u; i < family_count; i++) {

    if (family_prop[i].queueFlags & reqs)
      return i;
  }

  return -1;
}

int select_queue_family(VkPhysicalDevice pdev, VkQueueFlags flags) {
  unsigned count;

  GetPhysicalDeviceQueueFamilyProperties(pdev, &count, NULL);

  if (count < 1)
    return -1;

  VkQueueFamilyProperties prop[count];

  GetPhysicalDeviceQueueFamilyProperties(pdev, &count, prop);

  return find_queue_family(count, prop, flags);
}

static VkResult check_device_extensions(VkPhysicalDevice pdev,
                                        const unsigned count,
                                        const char **exts) {
  unsigned c;

  VkResult res = EnumerateDeviceExtensionProperties(pdev, NULL, &c, NULL);
  if (res != VK_SUCCESS)
    return res;

  VkExtensionProperties props[c];

  EnumerateDeviceExtensionProperties(pdev, NULL, &c, props);

  for (int i = 0u; i < count; i++) {
    Bool support = FALSE;

    for (int j = 0u; j < c; j++) {
      if (strcmp(exts[i], props[j].extensionName) == 0)
        support = TRUE;
    }

    if (support != TRUE) {
      LOG_ERROR("Require extension %s", exts[i]);
      return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
  }

  return VK_SUCCESS;
}

VkDevice create_device(VkPhysicalDevice pdev, unsigned queue_family_count,
                       const int *queue_family_index, VkQueue *queue) {
  const char *enable_extensions[] = {
      VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
      VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
      VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
      VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
      VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
      VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  VkResult res;

  res = check_device_extensions(pdev, NELEMS(enable_extensions),
                                enable_extensions);
  if (res != VK_SUCCESS)
    return VK_NULL_HANDLE;

  VkPhysicalDeviceFeatures enable_features = {0};

  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_feature = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
      .pNext = NULL,
      .timelineSemaphore = VK_TRUE,
  };

  VkDeviceQueueCreateInfo queue_family_info[queue_family_count];

  for (int i = 0u; i < queue_family_count; i++) {
    queue_family_info[i] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount = 1,
        .pQueuePriorities = &(float){1.0f},
        .queueFamilyIndex = queue_family_index[i],
    };
  }

  VkDeviceCreateInfo device_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &timeline_feature,
      .flags = 0,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL,
      .queueCreateInfoCount = queue_family_count,
      .pQueueCreateInfos = queue_family_info,
      .enabledExtensionCount = NELEMS(enable_extensions),
      .ppEnabledExtensionNames = enable_extensions,
      .pEnabledFeatures = &enable_features,
  };

  VkDevice device;

  res = CreateDevice(pdev, &device_info, NULL, &device);
  if (res != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }

  res = device_dispatch_table_init(device);
  if (res != VK_SUCCESS) {
    DestroyDevice(device, NULL);
    return VK_NULL_HANDLE;
  }

  VkDeviceQueueInfo2 queue_info[queue_family_count];

  for (int i = 0u, n = 1u; i < queue_family_count; i++, n++) {
    queue_info[i] = (VkDeviceQueueInfo2){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
        .queueIndex = 0,
        .queueFamilyIndex = queue_family_index[i],
    };

    if (n < queue_family_count)
      queue_info[i].pNext = &queue_info[n];
  }

  GetDeviceQueue2(device, queue_info, queue);

  return device;
}

static VkFormat vk_format(uint32_t format) {
  switch (format) {
  case DRM_FORMAT_ARGB8888:
  case DRM_FORMAT_XRGB8888:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case DRM_FORMAT_XRGB2101010:
    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  case DRM_FORMAT_RGB565:
    return VK_FORMAT_B5G6R5_UNORM_PACK16;
  default: {
    LOG_ERROR("unsupported format %u", format);
    return VK_FORMAT_UNDEFINED;
  }
  }
}

static unsigned get_modifiers_prop(VkPhysicalDevice pdev, VkFormat format,
                                   VkDrmFormatModifierPropertiesEXT **props) {
  VkDrmFormatModifierPropertiesListEXT drm_modifier_list = {
      .sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
      .pNext = NULL,
      .drmFormatModifierCount = 0,
      .pDrmFormatModifierProperties = NULL,
  };

  VkFormatProperties2 prop = {
      .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
      .pNext = &drm_modifier_list,
  };

  GetPhysicalDeviceFormatProperties2(pdev, format, &prop);

  if (drm_modifier_list.drmFormatModifierCount < 1)
    return 0;

  *props = drm_modifier_list.pDrmFormatModifierProperties =
      malloc(drm_modifier_list.drmFormatModifierCount *
             sizeof(VkDrmFormatModifierPropertiesEXT));

  GetPhysicalDeviceFormatProperties2(pdev, format, &prop);

  return drm_modifier_list.drmFormatModifierCount;
}

unsigned query_drm_modifiers(VkPhysicalDevice pdev, uint32_t format,
                             uint64_t *modifiers) {
  VkDrmFormatModifierPropertiesEXT *props = NULL;

  unsigned c = get_modifiers_prop(pdev, vk_format(format), &props);

  if (modifiers != NULL) {
    for (int i = 0u; i < c; i++) {
      modifiers[i] = props[i].drmFormatModifier;
    }
  }

  free(props);
  return c;
}

unsigned query_drm_modifier_layers(VkPhysicalDevice pdev, uint32_t format,
                                   uint64_t modifier) {
  VkDrmFormatModifierPropertiesEXT *props = NULL;

  unsigned c = get_modifiers_prop(pdev, vk_format(format), &props);

  for (int i = 0u; i < c; i++) {

    if (props[i].drmFormatModifier == modifier) {

      free(props);
      return props[i].drmFormatModifierPlaneCount;
    }
  }

  free(props);
  return 0;
}

static int find_memory_type(VkPhysicalDevice pdev, VkMemoryPropertyFlags reqs) {
  VkPhysicalDeviceMemoryProperties prop;

  GetPhysicalDeviceMemoryProperties(pdev, &prop);

  for (int i = 0u; i < prop.memoryTypeCount; i++) {
    if (prop.memoryTypes[i].propertyFlags & reqs)
      return i;
  }

  return -1;
}

VkImage import_dmabuf_image(VkPhysicalDevice pdev, VkDevice dev,
                            VkDeviceMemory *memory,
                            const struct vk_buffer_info *info) {
  VkExternalMemoryHandleTypeFlagBits handle_type =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

  VkSubresourceLayout layout = (VkSubresourceLayout){
      .offset = info->offset,
      .rowPitch = info->row_pitch,
      .size = 0,
  };

  VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info = {
      .sType =
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
      .drmFormatModifierPlaneCount = 1,
      .drmFormatModifier = info->modifier,
      .pPlaneLayouts = &layout,
  };

  VkExternalMemoryImageCreateInfo external_image = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .pNext = &modifier_info,
      .handleTypes = handle_type,
  };

  VkImageCreateInfo image_create_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &external_image,
      .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = vk_format(info->format),
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .extent = (VkExtent3D){info->width, info->height, 1},
      .usage = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
  };

  VkImage image;

  VkResult res = CreateImage(dev, &image_create_info, NULL, &image);

  if (res != VK_SUCCESS)
    return VK_NULL_HANDLE;

  VkMemoryFdPropertiesKHR fdp = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
  };

  res = GetMemoryFdPropertiesKHR(dev, handle_type, info->fd, &fdp);
  if (res != VK_SUCCESS) {
    LOG_ERROR("invalid file descriptor");
    goto error_image;
  }

  VkImageMemoryRequirementsInfo2 reqs_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .pNext = NULL,
      .image = image,
  };

  VkMemoryRequirements2 reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
  };

  GetImageMemoryRequirements2(dev, &reqs_info, &reqs);

  int memory_type_index = find_memory_type(
      pdev, reqs.memoryRequirements.memoryTypeBits & fdp.memoryTypeBits);

  if (memory_type_index < 0) {
    LOG_ERROR("memory type not found");
    goto error_image;
  }

  VkMemoryDedicatedAllocateInfo dedicated_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = NULL,
      .buffer = VK_NULL_HANDLE,
      .image = image,
  };

  VkImportMemoryFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .pNext = &dedicated_info,
      .fd = os_dupfd_cloexec(info->fd),
      .handleType = handle_type,
  };

  VkMemoryAllocateInfo mem_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &import_info,
      .allocationSize = reqs.memoryRequirements.size,
      .memoryTypeIndex = memory_type_index,
  };

  res = AllocateMemory(dev, &mem_info, NULL, memory);
  if (res != VK_SUCCESS) {
    LOG_ERROR("failed to import memory");
    goto error_image;
  }

  VkBindImageMemoryInfo bind_info = (VkBindImageMemoryInfo){
      .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image = image,
      .memory = *memory,
  };

  res = BindImageMemory2(dev, 1, &bind_info);
  if (res != VK_SUCCESS) {
    LOG_ERROR("failed to bind memory");
    goto error_memory;
  }

  return image;

error_memory:
  FreeMemory(dev, *memory, NULL);
error_image:
  DestroyImage(dev, image, NULL);
  return VK_NULL_HANDLE;
}
