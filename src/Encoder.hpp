#pragma once

#include <memory>

#include "EncodePipeline.h"
#include "Renderer.hpp"

#include "alvr_server/IDRScheduler.h"

namespace alvr {

// TODO: Move to source file
class Encoder {
    VkContext vkCtx;
    Optional<render::Renderer> renderer;
    std::unique_ptr<EncodePipeline> encoder;
    IDRScheduler idrScheduler;

public:
    Encoder(AlvrVkInfo vkInfo)
        : vkCtx(vkInfo)
    {
    }

    // TODO: Code path for steamvr
    AlvrVkExport createImages(ImageRequirements& imageReqs);

    void initEncoding();

    void present(u32 idx, u64 timelineVal);
};

}

// TODO: Where to put
vk::Extent2D ensureInit();
