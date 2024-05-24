#pragma once

#include <fstream>
#include <memory>
#include <type_traits>

#include "EncodePipeline.h"
#include "Renderer.hpp"

#include "alvr_server/Settings.h"

namespace alvr {

template <typename... T>
concept floats = (std::is_same_v<float, T> && ...);

template <typename... T>
auto makeSpecs(T... args)
    requires floats<T...>
{
    render::PipelineCreateInfo info {};

    constexpr auto Size = sizeof(float);
    u32 index = 0;

    // NOTE: This only works with little endian (but what pcvr target is big endian anyway)
    auto put = [&](float data) {
        std::array<u8, Size> arr;
        memcpy(arr.data(), reinterpret_cast<u8*>(&data), arr.size());

        info.specData.insert(info.specData.end(), arr.begin(), arr.end());
        info.specs.push_back({
            .constantID = index,
            .offset = static_cast<uint32_t>(index * Size),
            .size = Size,
        });
    };

    (put(args), ...);

    return info;
}

inline auto makeColorCorrection(Settings const& settings, vk::Extent2D extent)
{
    // The order of this needs to be kept in sync with the shader!
    // clang-format off
    auto info = makeSpecs(
        (float)extent.width,
        (float)extent.height,
        settings.m_brightness,
        settings.m_contrast + 1.f,
        settings.m_saturation + 1.f,
        settings.m_gamma,
        settings.m_sharpening);
    // clang-format on

    // TODO: Load shader!

    return info;
}

inline auto makeFoveation(Settings const& settings, vk::Extent2D extent)
{
    // TODO: Import this from the settings instead because the information from monado might not match up?
    float targetEyeWidth = (float)extent.width / 2;
    float targetEyeHeight = extent.height;

    float centerSizeX = settings.m_foveationCenterSizeX;
    float centerSizeY = settings.m_foveationCenterSizeY;
    float centerShiftX = settings.m_foveationCenterShiftX;
    float centerShiftY = settings.m_foveationCenterShiftY;
    float edgeRatioX = settings.m_foveationEdgeRatioX;
    float edgeRatioY = settings.m_foveationEdgeRatioY;

    float edgeSizeX = targetEyeWidth - centerSizeX * targetEyeWidth;
    float edgeSizeY = targetEyeHeight - centerSizeY * targetEyeHeight;

    float centerSizeXAligned = 1. - ceil(edgeSizeX / (edgeRatioX * 2.)) * (edgeRatioX * 2.) / targetEyeWidth;
    float centerSizeYAligned = 1. - ceil(edgeSizeY / (edgeRatioY * 2.)) * (edgeRatioY * 2.) / targetEyeHeight;

    float edgeSizeXAligned = targetEyeWidth - centerSizeXAligned * targetEyeWidth;
    float edgeSizeYAligned = targetEyeHeight - centerSizeYAligned * targetEyeHeight;

    float centerShiftXAligned
        = ceil(centerShiftX * edgeSizeXAligned / (edgeRatioX * 2.)) * (edgeRatioX * 2.) / edgeSizeXAligned;
    float centerShiftYAligned
        = ceil(centerShiftY * edgeSizeYAligned / (edgeRatioY * 2.)) * (edgeRatioY * 2.) / edgeSizeYAligned;

    float foveationScaleX = (centerSizeXAligned + (1. - centerSizeXAligned) / edgeRatioX);
    float foveationScaleY = (centerSizeYAligned + (1. - centerSizeYAligned) / edgeRatioY);

    float optimizedEyeWidth = foveationScaleX * targetEyeWidth;
    float optimizedEyeHeight = foveationScaleY * targetEyeHeight;

    // round the frame dimensions to a number of pixel multiple of 32 for the encoder
    auto optimizedEyeWidthAligned = (uint32_t)ceil(optimizedEyeWidth / 32.f) * 32;
    auto optimizedEyeHeightAligned = (uint32_t)ceil(optimizedEyeHeight / 32.f) * 32;

    float eyeWidthRatioAligned = optimizedEyeWidth / optimizedEyeWidthAligned;
    float eyeHeightRatioAligned = optimizedEyeHeight / optimizedEyeHeightAligned;

    // TODO: Export the new size?

    // The order of this needs to be kept in sync with the shader!
    // clang-format off
    auto info = makeSpecs(
        eyeWidthRatioAligned,
        eyeHeightRatioAligned,
        centerSizeXAligned,
        centerSizeYAligned,
        centerShiftXAligned,
        centerShiftYAligned,
        edgeRatioX,
        edgeRatioY);
    // clang-format on

    return info;
}

class Encoder {
    VkContext vkCtx;
    Optional<render::Renderer> renderer;
    std::unique_ptr<EncodePipeline> encoder;

public:
    Encoder(AlvrVkInfo vkInfo)
        : vkCtx(vkInfo)
    {
    }

    AlvrVkExport createImages(ImageRequirements& imageReqs)
    {
        // TODO: This should not be this way
        std::ifstream stream("/home/duck/devel/alvr_render/src/shader/quad.comp.spv", std::ios::binary);
        stream.seekg(0, std::ios::end);
        auto len = stream.tellg();
        stream.seekg(0, std::ios::beg);

        // TODO: Enable all the other shader loading

        auto shader = std::vector<u8>(len);
        stream.read(reinterpret_cast<char*>(shader.data()), len);

        render::PipelineCreateInfo pipeCI {
            .shaderData = shader,
        };
        renderer.emplace(vkCtx, imageReqs, std::vector { pipeCI });

        return renderer.get().getImages();
    }

    void initEncoding()
    {
        VkPhysicalDeviceDrmPropertiesEXT drmProps = {};
        drmProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT;

        VkPhysicalDeviceProperties2 deviceProps = {};
        deviceProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProps.pNext = &drmProps;
        vkGetPhysicalDeviceProperties2(vkCtx.physDev, &deviceProps);

        std::string devicePath;
        for (int i = 128; i < 136; ++i) {
            auto path = "/dev/dri/renderD" + std::to_string(i);
            int fd = open(path.c_str(), O_RDONLY);
            if (fd == -1) {
                continue;
            }
            struct stat s = {};
            int ret = fstat(fd, &s);
            close(fd);
            if (ret != 0) {
                continue;
            }
            dev_t primaryDev = makedev(drmProps.primaryMajor, drmProps.primaryMinor);
            dev_t renderDev = makedev(drmProps.renderMajor, drmProps.renderMinor);
            if (primaryDev == s.st_rdev || renderDev == s.st_rdev) {
                devicePath = path;
                break;
            }
        }
        if (devicePath.empty()) {
            devicePath = "/dev/dri/renderD128";
        }
        // Info("Using device path %s", devicePath.c_str());

        av_log_set_level(AV_LOG_DEBUG);

        // TODO: Be able to setup foveated rendering and stuff

        // TODO: This should not exist this way
        auto& avHwCtx = *new alvr::HWContext(vkCtx);

        auto out = renderer.get().getOutput();

        // TODO: Nvidia only
        // auto framCtx = new alvr::VkFrameCtx(aCtx, *(vk::ImageCreateInfo*)&out.imageCI);

        auto frame = new alvr::VkFrame(vkCtx, out.image.image, out.imageCI, out.size, out.image.memory, out.drm);

        encoder
            = EncodePipeline::Create(vkCtx, devicePath, *frame, out.imageCI.extent.width, out.imageCI.extent.height);
        // vaapi.emplace(aCtx, *frame, out.imageCI.extent.width, out.imageCI.extent.height);
    }

    void present(u32 idx, u64 timelineVal)
    {
        renderer.get().render(vkCtx, idx, timelineVal);

        encoder->PushFrame(0, true);
        // TODO: Idr scheduler

        alvr::FramePacket framePacket;
        if (!encoder->GetEncoded(framePacket)) {
            assert(false);
        }

        ParseFrameNals(encoder->GetCodec(), framePacket.data, framePacket.size, framePacket.pts, framePacket.isIDR);
    }
};

}
