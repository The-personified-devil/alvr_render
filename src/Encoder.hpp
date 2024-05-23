#pragma once

#include <memory>
#include <fstream>

#include "EncodePipeline.h"
#include "Renderer.hpp"

namespace alvr {

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

        av_log_set_level(AV_LOG_DEBUG);

        // TODO: Be able to setup foveated rendering and stuff

        // TODO: This should not exist this way
        auto& avHwCtx = *new alvr::HWContext (vkCtx);


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

        auto out = renderer.get().getOutput();

        // TODO: Nvidia only
        // auto framCtx = new alvr::VkFrameCtx(aCtx, *(vk::ImageCreateInfo*)&out.imageCI);

        auto frame = new alvr::VkFrame(vkCtx, out.image.image, out.imageCI, out.size, out.image.memory, out.drm);
        
        encoder = EncodePipeline::Create(vkCtx, devicePath, *frame, out.imageCI.extent.width, out.imageCI.extent.height);
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
