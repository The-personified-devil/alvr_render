#include "Renderer.hpp"

namespace alvr::render::detail {

RenderPipeline::RenderPipeline(VkContext const& ctx, vk::DescriptorSetLayout& layout, PipelineCreateInfo& pipelineCI)
{
    vk::ShaderModuleCreateInfo shaderCI {
        .codeSize = pipelineCI.shaderData.size(),
        .pCode = reinterpret_cast<u32 const*>(pipelineCI.shaderData.data()),
    };
    shader = ctx.dev.createShaderModule(shaderCI);

    vk::PipelineLayoutCreateInfo pipeLayoutCI {
        .setLayoutCount = 1,
        .pSetLayouts = &layout,
    };
    pipeLayout = ctx.dev.createPipelineLayout(pipeLayoutCI);

    vk::SpecializationInfo specInfo {
        .mapEntryCount = static_cast<u32>(pipelineCI.specs.size()),
        .pMapEntries = pipelineCI.specs.data(),
        .dataSize = pipelineCI.specData.size(),
        .pData = pipelineCI.specData.data(),
    };

    vk::PipelineShaderStageCreateInfo stageCI {
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = shader,
        .pName = "main",
        .pSpecializationInfo = not pipelineCI.specs.empty() ? &specInfo : nullptr,
    };
    vk::ComputePipelineCreateInfo pipeCI {
        .stage = stageCI,
        .layout = pipeLayout,
    };
    auto [result, pipes] = ctx.dev.createComputePipelines({}, pipeCI);

    assert(result == vk::Result::eSuccess);
    pipe = pipes[0];
};

void RenderPipeline::Render(
    VkContext const& ctx, vk::CommandBuffer cmdBuf, vk::ImageView in, vk::ImageView out, vk::Extent2D outSize)
{
    cmdBuf.bindPipeline(vk::PipelineBindPoint::eCompute, pipe);

    vk::DescriptorImageInfo descImgInfoIn {
        .imageView = in,
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    vk::DescriptorImageInfo descImgInfoOut {
        .imageView = out,
        .imageLayout = vk::ImageLayout::eGeneral,
    };

    vk::WriteDescriptorSet descWriteSets[2];
    descWriteSets[0] = {
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &descImgInfoIn,
    };
    descWriteSets[1] = {
        .dstBinding = 1,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .pImageInfo = &descImgInfoOut,
    };
    cmdBuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, pipeLayout, 0, descWriteSets, ctx.dispatch);

    cmdBuf.dispatch((outSize.width + 7) / 8, (outSize.height + 7) / 8, 1);
}

void RenderPipeline::destroy(VkContext const& ctx)
{
    ctx.dev.destroy(pipe);
    ctx.dev.destroy(pipeLayout);
    ctx.dev.destroy(shader);
}

}

// TODO: Seperate linux hacks out
#ifndef DRM_FORMAT_INVALID
#define DRM_FORMAT_INVALID 0
#define fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define DRM_FORMAT_ARGB8888 fourcc_code('A', 'R', '2', '4')
#define DRM_FORMAT_ABGR8888 fourcc_code('A', 'B', '2', '4')
#define fourcc_mod_code(vendor, val) ((((uint64_t)vendor) << 56) | ((val) & 0x00ffffffffffffffULL))
#define DRM_FORMAT_MOD_INVALID fourcc_mod_code(0, ((1ULL << 56) - 1))
#define DRM_FORMAT_MOD_LINEAR fourcc_mod_code(0, 0)
#define DRM_FORMAT_MOD_VENDOR_AMD 0x02
#define AMD_FMT_MOD_DCC_SHIFT 13
#define AMD_FMT_MOD_DCC_MASK 0x1
#define IS_AMD_FMT_MOD(val) (((val) >> 56) == DRM_FORMAT_MOD_VENDOR_AMD)
#define AMD_FMT_MOD_GET(field, value) (((value) >> AMD_FMT_MOD_##field##_SHIFT) & AMD_FMT_MOD_##field##_MASK)
#endif

static bool filter_modifier(uint64_t modifier)
{
    if (IS_AMD_FMT_MOD(modifier)) {
        // DCC not supported as encode input
        if (AMD_FMT_MOD_GET(DCC, modifier)) {
            return false;
        }
    }
    return true;
}

static u32 to_drm_format(vk::Format format)
{
    switch (format) {
    case vk::Format::eB8G8R8A8Unorm:
        return DRM_FORMAT_ARGB8888;
    case vk::Format::eR8G8B8A8Unorm:
        return DRM_FORMAT_ABGR8888;
    default:
        // assert(false);
        return DRM_FORMAT_INVALID;
    }
}

using namespace alvr;
using namespace alvr::render;

u32 memoryTypeIndex(VkContext const& ctx, vk::MemoryPropertyFlags properties, u32 typeBits)
{
    auto prop = ctx.physDev.getMemoryProperties();
    for (u32 i = 0; i < prop.memoryTypeCount; ++i) {
        if ((prop.memoryTypes[i].propertyFlags & properties) == properties && typeBits & (1 << i)) {
            return i;
        }
    }
    return 0xFFFFFFFF;
}

inline Image createImage(VkContext const& ctx, vk::ImageCreateInfo imageCI)
{
    Image img {
        .image = ctx.dev.createImage(imageCI),
        .layout = imageCI.initialLayout,
    };

    vk::MemoryDedicatedRequirements dedicatedReqs {};
    vk::MemoryRequirements2 memReqs {
        .pNext = &dedicatedReqs,
    };
    vk::ImageMemoryRequirementsInfo2 memReqInfo {
        .image = img.image,
    };
    ctx.dev.getImageMemoryRequirements2(&memReqInfo, &memReqs);

    vk::MemoryDedicatedAllocateInfo memDedicatedInfo {
        .image = img.image,
    };
    vk::MemoryAllocateInfo memAllocInfo {
        .pNext = &memDedicatedInfo,
        .allocationSize = memReqs.memoryRequirements.size,
        .memoryTypeIndex
        = memoryTypeIndex(ctx, vk::MemoryPropertyFlagBits::eDeviceLocal, memReqs.memoryRequirements.memoryTypeBits),
    };
    img.memory = ctx.dev.allocateMemory(memAllocInfo);

    vk::BindImageMemoryInfo bindImgInfo {
        .image = img.image,
        .memory = img.memory,
        .memoryOffset = 0,
    };
    ctx.dev.bindImageMemory2(bindImgInfo);

    vk::ImageViewCreateInfo imgViewCI {
        .image = img.image,
        .viewType = vk::ImageViewType::e2D,
        .format = imageCI.format,
        .components {
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity,
        },
        .subresourceRange {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = imageCI.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = imageCI.arrayLayers,
        },
    };
    img.view = ctx.dev.createImageView(imgViewCI);

    return img;
}

Output createOutputImage(VkContext const& ctx, vk::Extent2D extent, vk::Format format, HandleType handleType)
{
    Image img;
    Output out;

    vk::ImageCreateInfo imgCI {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {
            .width = extent.width,
            .height = extent.height,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    img.layout = imgCI.initialLayout;

    vk::ExternalMemoryImageCreateInfo extImgCI {};

    // Lifetime
    std::vector<uint64_t> imageModifiers;

    // TODO: Actually import this
    bool haveDmaBuf = true;
    bool haveDrmModifiers = true;
    std::vector<vk::DrmFormatModifierPropertiesEXT> modProps;

    if (haveDrmModifiers && handleType == HandleType::DmaBuf) {
        imgCI.tiling = vk::ImageTiling::eDrmFormatModifierEXT;

        vk::DrmFormatModifierPropertiesListEXT modPropList {};
        vk::FormatProperties2 formatProps {
            .pNext = &modPropList,
        };
        ctx.physDev.getFormatProperties2(imgCI.format, &formatProps);

        modProps.resize(modPropList.drmFormatModifierCount);
        modPropList.pDrmFormatModifierProperties = modProps.data();
        ctx.physDev.getFormatProperties2(imgCI.format, &formatProps);

        for (auto& prop : modProps) {
            if (!filter_modifier(prop.drmFormatModifier)) {
                continue;
            }

            vk::PhysicalDeviceImageDrmFormatModifierInfoEXT modInfo {
                .drmFormatModifier = prop.drmFormatModifier,
                .sharingMode = imgCI.sharingMode,
                .queueFamilyIndexCount = imgCI.queueFamilyIndexCount,
                .pQueueFamilyIndices = imgCI.pQueueFamilyIndices,
            };
            vk::PhysicalDeviceImageFormatInfo2 formatInfo {
                .pNext = &modInfo,
                .format = imgCI.format,
                .type = imgCI.imageType,
                .tiling = imgCI.tiling,
                .usage = imgCI.usage,
                .flags = imgCI.flags,
            };
            vk::ImageFormatProperties2 imgFormatProps {
                .pNext = nullptr,
            };
            auto result = ctx.physDev.getImageFormatProperties2(&formatInfo, &imgFormatProps);
            if (result == vk::Result::eSuccess)
                imageModifiers.push_back(prop.drmFormatModifier);
        }

        extImgCI.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT;

        vk::ImageDrmFormatModifierListCreateInfoEXT modListCI {
            .pNext = &extImgCI,
            .drmFormatModifierCount = static_cast<u32>(imageModifiers.size()),
            .pDrmFormatModifiers = imageModifiers.data(),
        };

        imgCI.pNext = &modListCI;
    } else if (haveDmaBuf && handleType == HandleType::DmaBuf) {
        extImgCI.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT;
        imgCI.pNext = &extImgCI;
        imgCI.tiling = vk::ImageTiling::eLinear;
    } else if (handleType == HandleType::OpaqueFd) {
        extImgCI.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
        imgCI.pNext = &extImgCI;
        imgCI.tiling = vk::ImageTiling::eOptimal;
    } else if (handleType == HandleType::None) {
        imgCI.tiling = vk::ImageTiling::eOptimal;
    }
    img.image = ctx.dev.createImage(imgCI);

    vk::MemoryDedicatedRequirements dedicatedReqs {};
    vk::MemoryRequirements2 memReqs {
        .pNext = &dedicatedReqs,
    };

    vk::ImageMemoryRequirementsInfo2 memReqInfo {
        .image = img.image,
    };
    ctx.dev.getImageMemoryRequirements2(&memReqInfo, &memReqs);

    vk::MemoryDedicatedAllocateInfo memDedicatedInfo {
        .image = img.image,
    };
    vk::ExportMemoryAllocateInfo memExportInfo {
        .handleTypes = extImgCI.handleTypes,
    };
    if (handleType != HandleType::None)
        memDedicatedInfo.pNext = &memExportInfo;

    vk::MemoryAllocateInfo memAllocInfo {
        .pNext = &memDedicatedInfo,
        .allocationSize = memReqs.memoryRequirements.size,
        .memoryTypeIndex
        = memoryTypeIndex(ctx, vk::MemoryPropertyFlagBits::eDeviceLocal, memReqs.memoryRequirements.memoryTypeBits),
    };
    img.memory = ctx.dev.allocateMemory(memAllocInfo);

    vk::BindImageMemoryInfo bindImgInfo {
        .image = img.image,
        .memory = img.memory,
        .memoryOffset = 0,
    };
    ctx.dev.bindImageMemory2(bindImgInfo);

    if (haveDmaBuf) {
        vk::MemoryGetFdInfoKHR memFdInfo {
            .memory = img.memory,
            .handleType = vk::ExternalMemoryHandleTypeFlagBits::eDmaBufEXT,
        };
        out.drm.fd = ctx.dev.getMemoryFdKHR(memFdInfo, ctx.dispatch);

        if (haveDrmModifiers) {
            auto imgDrmProps = ctx.dev.getImageDrmFormatModifierPropertiesEXT(img.image, ctx.dispatch);

            out.drm.modifier = imgDrmProps.drmFormatModifier;
            for (auto prop : modProps) {
                if (prop.drmFormatModifier == out.drm.modifier)
                    out.drm.planes = prop.drmFormatModifierPlaneCount;
            }
        } else {
            out.drm.modifier = DRM_FORMAT_MOD_INVALID;
            out.drm.planes = 1;
        }

        for (u32 i = 0; i < out.drm.planes; ++i) {
            vk::ImageSubresource subresource {
                .aspectMask = static_cast<vk::ImageAspectFlags>(
                    haveDrmModifiers ? VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT << i : VK_IMAGE_ASPECT_COLOR_BIT),
            };
            auto layout = ctx.dev.getImageSubresourceLayout(img.image, subresource);
            out.drm.strides[i] = layout.rowPitch;
            out.drm.offsets[i] = layout.offset;
        }

        out.drm.format = to_drm_format(imgCI.format);
    }

    vk::ImageViewCreateInfo imgViewCI {
        .image = img.image,
        .viewType = vk::ImageViewType::e2D,
        .format = imgCI.format,
        .components {
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity,
        },
        .subresourceRange {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = imgCI.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = imgCI.arrayLayers,
        },
    };
    img.view = ctx.dev.createImageView(imgViewCI);

    // out.semaphore = ctx.dev.createSemaphore({});

    out.imageCI = imgCI;
    out.size = memReqs.memoryRequirements.size;
    out.image = img;

    return out;
}

namespace alvr::render {

// TODO: This should only take fixed stuff, nothing else
// but image import?
// and output import?
Renderer::Renderer(VkContext const& vkCtx, ImageRequirements& imgReqs, std::vector<PipelineCreateInfo> pipeCIs)
{
    // We can use the extend from monado since that's directed by what we told monado
    imgExtend = imgReqs.extent;

    vk::Format format = vk::Format::eR8G8B8A8Unorm;

    // TODO: Fix bruh

    // bool supportsFormat = false;
    // for (u32 i = 0; i < imgReqs.format_count; ++i) {
    //     std::cout << "Format id: " << imgReqs.formats[i] << "\n";
    //     if (imgReqs.formats[i] == format) {
    //         supportsFormat = true;
    //         break;
    //     }
    // }
    // assert(supportsFormat);

    vk::ImageCreateInfo imgCI {
            .imageType = vk::ImageType::e2D,
            .format = format,

            .extent = {
                .width = imgExtend.width,
                .height = imgExtend.height,
                .depth = 1,
            },

            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = static_cast<vk::ImageUsageFlagBits>(imgReqs.image_usage)
                | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };

    for (int i = 0; i < ImageCount; ++i) {
        inputImages[i] = createImage(vkCtx, imgCI);
    }

    for (auto& img : stagingImgs) {
        img = createImage(vkCtx, imgCI);
    }

    // TODO: Make dependent on settings
    // This actually depends on foveation not the encoder extent
    // Which means that the quad shader is just... useless
    // Since the encoder does the work already
    output = createOutputImage(vkCtx, { imgCI.extent.width, imgCI.extent.height }, imgCI.format, HandleType::DmaBuf);

    vk::QueryPoolCreateInfo poolCI {
        .queryType = vk::QueryType::eTimestamp,
        .queryCount = 2,
    };
    timestampPool = vkCtx.dev.createQueryPool(poolCI);

    vk::CommandPoolCreateInfo cmdPoolCI {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = vkCtx.meta.queueFamily,
    };
    cmdPool = vkCtx.dev.createCommandPool(cmdPoolCI);

    vk::CommandBufferAllocateInfo cmdBufAllocInfo {
        .commandPool = cmdPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    cmdBuf = vkCtx.dev.allocateCommandBuffers(cmdBufAllocInfo)[0];

    vk::SamplerCreateInfo samplerCI {
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,

        // TODO: Put feature selection into monado so we can reenable this
        .anisotropyEnable = false,
        .maxAnisotropy = 16.f,

        .borderColor = vk::BorderColor::eFloatTransparentBlack,
    };
    sampler = vkCtx.dev.createSampler(samplerCI);

    vk::DescriptorSetLayoutBinding descBindings[2];
    descBindings[0] = {
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .pImmutableSamplers = &sampler,
    };
    descBindings[1] = {
        .binding = 1,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
    };

    vk::DescriptorSetLayoutCreateInfo descLayoutCI {
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = 2,
        .pBindings = descBindings,
    };
    descLayout = vkCtx.dev.createDescriptorSetLayout(descLayoutCI);

    pipes.reserve(pipeCIs.size());
    for (auto& pipeCI : pipeCIs) {
        pipes.emplace_back(vkCtx, descLayout, pipeCI);
    }

    vk::SemaphoreTypeCreateInfo timelineCI {
        .semaphoreType = vk::SemaphoreType::eTimeline,
        .initialValue = 0,
    };
    vk::SemaphoreCreateInfo semCI {
        .pNext = &timelineCI,
    };
    monadoFinishedSem = vkCtx.dev.createSemaphore(semCI);

    fence = vkCtx.dev.createFence({});
}

void Renderer::render(VkContext& vkCtx, u32 index, u64 waitValue)
{
    vk::CommandBufferBeginInfo beginInfo {};
    cmdBuf.begin(beginInfo);

    cmdBuf.resetQueryPool(timestampPool, 0, 2);
    cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, timestampPool, 0);

    Image* prev = &inputImages[index];
    for (usize i = 0; i < pipes.size(); ++i) {
        Image* out = nullptr;
        if (i == pipes.size() - 1) {
            out = &output.image;
        } else {
            out = &stagingImgs[i % StagingImgCount];
        }

        vk::ImageMemoryBarrier imgBarrier { .subresourceRange {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1,
        } };

        std::vector<vk::ImageMemoryBarrier> barriers;

        if (prev->layout != vk::ImageLayout::eShaderReadOnlyOptimal) {
            imgBarrier.image = prev->image;
            imgBarrier.oldLayout = prev->layout;
            prev->layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imgBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imgBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
            imgBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            barriers.push_back(imgBarrier);
        }

        if (out->layout != vk::ImageLayout::eGeneral) {
            imgBarrier.image = out->image;
            imgBarrier.oldLayout = vk::ImageLayout::eUndefined;
            out->layout = vk::ImageLayout::eGeneral;
            imgBarrier.newLayout = vk::ImageLayout::eGeneral;
            imgBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
            imgBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
            barriers.push_back(imgBarrier);
        }

        if (barriers.size()) {
            cmdBuf.pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eComputeShader,
                {}, {}, {}, barriers);
        }

        pipes[i].Render(vkCtx, cmdBuf, prev->view, out->view, imgExtend);

        prev = out;
    }

    cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timestampPool, 1);

    cmdBuf.end();

    vk::TimelineSemaphoreSubmitInfo timelineInfo {
        .waitSemaphoreValueCount = 1,
        .pWaitSemaphoreValues = &waitValue,
    };
    vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    vk::SubmitInfo submitInfo {
        .pNext = &timelineInfo,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &monadoFinishedSem,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
    };

    vkCtx.useQueue([&](auto& queue) { queue.submit(submitInfo, fence); });
    assert(vkCtx.dev.waitForFences(fence, true, UINT64_MAX) == vk::Result::eSuccess);
    vkCtx.dev.resetFences(fence);
}

void Renderer::destroy(VkContext const& ctx)
{
    ctx.dev.destroy(monadoFinishedSem);

    for (auto& pipe : pipes) {
        pipe.destroy(ctx);
    }

    ctx.dev.destroy(fence);
    ctx.dev.destroy(descLayout);
    ctx.dev.destroy(sampler);

    ctx.dev.destroy(cmdPool);
    ctx.dev.destroy(timestampPool);

    output.image.destroy(ctx);

    for (auto& img : stagingImgs) {
        img.destroy(ctx);
    }

    for (auto& img : inputImages) {
        img.destroy(ctx);
    }
}

}
