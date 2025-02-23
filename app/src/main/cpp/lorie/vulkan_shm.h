#pragma once
#include <stdbool.h>
#include <vulkan/vulkan.h>

struct vk_shm_allocator;
struct vk_shm_bo;

struct vk_shm_bo *
vk_shm_bo_create(struct vk_shm_allocator *allocator, unsigned int width,
   unsigned int height, unsigned int depth, VkFormat format, bool linear);
struct vk_shm_bo *vk_shm_bo_import(struct vk_shm_allocator *allocator,
   unsigned int width, unsigned int height, unsigned int depth,
   VkFormat format, bool linear, int fd);
int vk_shm_bo_fd(struct vk_shm_bo *bo);
uint32_t vk_shm_bo_stride(struct vk_shm_bo *bo);
uint32_t vk_shm_bo_offset(struct vk_shm_bo *bo);
size_t vk_shm_bo_size(struct vk_shm_bo *bo);
unsigned int vk_shm_bo_depth(struct vk_shm_bo *bo);
unsigned int vk_shm_bo_width(struct vk_shm_bo *bo);
unsigned int vk_shm_bo_height(struct vk_shm_bo *bo);
void *vk_shm_bo_map(struct vk_shm_bo *bo);
void vk_shm_bo_unmap(struct vk_shm_bo *bo);
void vk_shm_bo_destroy(struct vk_shm_bo *bo);

struct vk_shm_allocator *vk_shm_allocator_create(void);
void vk_shm_allocator_destroy(struct vk_shm_allocator *allocator);
