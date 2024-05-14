#pragma once

#include <pthread.h>
#include <vulkan/vulkan_core.h>

struct AlvrVkInfo {
	VkInstance instance;
	uint32_t version;
	VkPhysicalDevice physDev;
	uint32_t phyDevIdx;
	VkDevice device;
	uint32_t queueFamIdx;
	uint32_t queueIdx;
	VkQueue queue;

	// TODO: Replace with just function call into monado code
	// Or do we even need that at all if the present code is ungoofy?
	pthread_mutex_t* queue_mutex;
};

struct AlvrVkImg {
	uint64_t img;
	uint64_t view;
};

struct AlvrVkExport {
    struct AlvrVkImg imgs[3];
    uint64_t semaphore;
};

struct Proxy {};

// TODO: This is a hack move the monado code out to an entirely different file then we won't need this :)
// but I can implement this hack quicker so I can test quicker :)
struct Proxy* rendr_create_encoder(struct AlvrVkInfo* info);
void rendr_init_images(struct Proxy*);
void rendr_present(struct Proxy*, uint64_t sem_tl_val, uint32_t img_idx);

// implemented in framrender
void rendr_take_vk(struct AlvrVkExport*);
