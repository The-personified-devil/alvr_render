#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <optional>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "utils.hpp"

#include "monado_interface.h"

extern "C" {
#include <libavutil/log.h>
}

// TODO: Clean up
void ParseFrameNals(
    int codec, unsigned char* buf, int len, unsigned long long targetTimestampNs, bool isIdr);

namespace alvr {

enum class Vendor {
    Amd,
    Intel,
    Nvidia
};

class VkContext {
public:
    vk::Instance instance;
    vk::PhysicalDevice physDev;
    vk::Device dev;

    vk::Queue queue;

    vk::DispatchLoaderDynamic dispatch;

    struct Meta {
        Vendor vendor;

        std::vector<char const*> instExtensions;
        std::vector<char const*> devExtensions;

        vk::PhysicalDeviceVulkan12Features feats12;
        vk::PhysicalDeviceFeatures2 feats;

        u32 queueFamily;
        u32 queueIndex;
    } meta;

private:
    std::optional<SharedMutex> queueMutex;

public:
    // TODO: Don't have this as a constructor, we need to pass so much stuff into here
    VkContext(AlvrVkInfo vkInfo)
    {
        instance = vkInfo.instance;
        physDev = vkInfo.physDev;
        dev = vkInfo.device;
        queue = vkInfo.queue;

        meta.queueFamily = vkInfo.queueFamIdx;
        meta.queueIndex = vkInfo.queueIdx;

        // TODO: Initialize the rest of the meta info properly
        
        auto devProps = physDev.getProperties2();

        // TODO: Tbh we also don't need this when we do software encoding
        if (devProps.properties.vendorID == 0x1002)
            meta.vendor = Vendor::Amd;
        else if (devProps.properties.vendorID == 0x8086)
            meta.vendor = Vendor::Intel;
        else if (devProps.properties.vendorID == 0x10de)
            meta.vendor = Vendor::Nvidia;

        dispatch = {instance, vkGetInstanceProcAddr};
    }

    // TODO: Take id and create whole context (Steamvr case)
    VkContext()
    {
        std::vector<std::string_view> wantedInstExts = {
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        };

        auto availInstExts = vk::enumerateInstanceExtensionProperties();
        std::vector<const char*> acquiredInstExts;
        for (auto wantedName : wantedInstExts) {
            auto it = std::find_if(availInstExts.begin(), availInstExts.end(), [&](auto& ext) {
                return wantedName == ext.extensionName;
            });
            if (it == availInstExts.end())
                assert(false);
            acquiredInstExts.push_back(wantedName.data());
        }

        vk::ApplicationInfo appInfo {
            .pApplicationName = "ALVR",
            .apiVersion = VK_API_VERSION_1_2,
        };
        vk::InstanceCreateInfo instanceCI {
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = static_cast<uint32_t>(acquiredInstExts.size()),
            .ppEnabledExtensionNames = acquiredInstExts.data(),
        };
        instance = vk::createInstance(instanceCI);
        auto physDevs = instance.enumeratePhysicalDevices();

        // TODO: Match to uuid
        physDev = physDevs[0];

        u32 queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physDev, &queueFamilyCount, queueFamilyProps.data());

        std::optional<u32> wantedQueueFamily;

        for (u32 i = 0; i < queueFamilyCount; ++i) {
            auto& props = queueFamilyProps[i];
            bool isGraphics = props.queueFlags & VK_QUEUE_GRAPHICS_BIT;
            bool isCompute = props.queueFlags & VK_QUEUE_COMPUTE_BIT;

            if (isCompute && (!wantedQueueFamily.has_value() || !isGraphics)) {
                wantedQueueFamily = i;
            }
        }
        meta.queueFamily = wantedQueueFamily.value();

        u32 queueIndex = 0;

        // TODO: Request the instance extension for this in monado
        // TODO: Structure chains, vulkan raii?

        std::vector<std::string_view> wantedExts = {
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
            VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
            VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
            VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME,
            VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME,
            VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME,
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME,
        };

        vk::PhysicalDevice vphysDev(physDev);
        auto availExts = vphysDev.enumerateDeviceExtensionProperties();

        std::vector<char const*> acquiredExts;
        for (auto name : wantedExts) {
            auto it = std::find_if(availExts.begin(), availExts.end(), [&](auto& other) {
                return name == (const char*)other.extensionName;
            });
            if (it != availExts.end())
                acquiredExts.push_back(name.data());
        }

        f32 queuePrio = 1.f;
        vk::DeviceQueueCreateInfo queueCI {
            .queueFamilyIndex = meta.queueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePrio,
        };

        meta.feats12 = vk::PhysicalDeviceVulkan12Features{
            .timelineSemaphore = 1,
        };

        meta.feats = vk::PhysicalDeviceFeatures2{
            .pNext = &meta.feats12,
            .features = {
                .robustBufferAccess = true,
                .samplerAnisotropy = true,
            },
        };
        vk::DeviceCreateInfo devCI {
            .pNext = &meta.feats,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCI,
            .enabledExtensionCount = static_cast<u32>(acquiredExts.size()),
            .ppEnabledExtensionNames = acquiredExts.data(),
        };
        dev = vphysDev.createDevice(devCI);

        meta.devExtensions = acquiredExts;

        queue = dev.getQueue(wantedQueueFamily.value(), queueIndex);
    }
};

}
