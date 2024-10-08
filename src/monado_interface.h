#pragma once

#include <pthread.h>
#include <vulkan/vulkan_core.h>

struct MutexProxy {
    void* mutex;
};

struct SharedMutex {
    void (*lock) (struct MutexProxy*);
    void (*unlock) (struct MutexProxy*);

    struct MutexProxy mutex;
};

struct AlvrVkInfo {
	VkInstance instance;
	uint32_t version;
	VkPhysicalDevice physDev;
	uint32_t phyDevIdx;
	VkDevice device;
	uint32_t queueFamIdx;
	uint32_t queueIdx;
	VkQueue queue;

    struct SharedMutex mutex;
};


#define ALVR_SWAPCHAIN_IMGS 3
struct ImgExport {
    VkImage img;
    VkImageView view;
};

struct AlvrVkExport {
    struct ImgExport imgs[ALVR_SWAPCHAIN_IMGS];
    VkSemaphore sem;
};

struct ImageRequirements
{
	//! Image usage for the images, must be followed.
	VkImageUsageFlags image_usage;

	//! Acceptable formats for the images, must be followed.
	VkFormat formats[16];

	// Number of formats.
	uint32_t format_count;

	//! Preferred extent, can be ignored by the target.
	VkExtent2D extent;

	//! Preferred color space, can be ignored by the target.
	VkColorSpaceKHR color_space;

	// Preferred present_mode, can be ignored by the target.
	VkPresentModeKHR present_mode;
};
