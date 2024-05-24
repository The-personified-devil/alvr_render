#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "VkContext.hpp"
#include "utils.hpp"

#include "ffmpeg_helper.h"
#include "monado_interface.h"

namespace alvr::render {

enum class HandleType {
    None,
    DmaBuf,
    OpaqueFd,
};

struct Image {
    vk::Image image = VK_NULL_HANDLE;
    vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    vk::DeviceMemory memory = VK_NULL_HANDLE;
    vk::ImageView view = VK_NULL_HANDLE;

    void destroy(VkContext const& ctx)
    {
        ctx.dev.destroy(view);
        ctx.dev.free(memory);
        ctx.dev.destroy(image);
    }
};

struct Output {
    Image image;
    DrmImage drm;
    // VkSemaphore semaphore;
    VkImageCreateInfo imageCI;
    VkDeviceSize size;
};

struct PipelineCreateInfo {
    std::vector<u8> shaderData;
    std::vector<vk::SpecializationMapEntry> specs;
    std::vector<u8> specData;
};

namespace detail {

    class RenderPipeline {
        vk::ShaderModule shader;
        vk::PipelineLayout pipeLayout;
        vk::Pipeline pipe;

    public:
        RenderPipeline(VkContext const& ctx, vk::DescriptorSetLayout& layout, PipelineCreateInfo& pipelineCI);

        void Render(
            VkContext const& ctx, vk::CommandBuffer cmdBuf, vk::ImageView in, vk::ImageView out, vk::Extent2D outSize);

        void destroy(VkContext const& ctx);
    };

}

constexpr u32 ImageCount = ALVR_SWAPCHAIN_IMGS;
constexpr usize StagingImgCount = 2;

class Renderer {
    Image inputImages[ImageCount];
    Image stagingImgs[StagingImgCount];
    Output output;

    vk::QueryPool timestampPool;
    vk::CommandPool cmdPool;
    vk::CommandBuffer cmdBuf;
    vk::Sampler sampler;
    vk::DescriptorSetLayout descLayout;
    vk::Fence fence;

    vk::Extent2D imgExtend;
    std::vector<detail::RenderPipeline> pipes;

    vk::Semaphore monadoFinishedSem;

public:
    // TODO: Abstract for compat with Steamvr direct mode
    Renderer(VkContext const& vkCtx, ImageRequirements& imgReqs, std::vector<PipelineCreateInfo> pipeCIs);

    // TODO: Import output (somehow?)

    // NOTE: Use the output immediately afterwards, as this synchronizes to the end of gpu operations
    void render(VkContext& vkCtx, u32 index, u64 waitValue);

    AlvrVkExport getImages()
    {
        AlvrVkExport expt {
            .sem = monadoFinishedSem,
        };

        for (u32 i = 0; i < ImageCount; ++i) {
            expt.imgs[i].img = inputImages[i].image;
            expt.imgs[i].view = inputImages[i].view;
        }

        return expt;
    }

    Output getOutput() { return output; }

    void destroy(VkContext const& ctx);
};

}
