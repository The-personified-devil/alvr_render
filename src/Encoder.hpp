#pragma once

#include <memory>

#include "EncodePipeline.h"
#include "EventManager.hpp"
#include "Renderer.hpp"

#include "alvr_server/IDRScheduler.h"

namespace alvr {

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

    AlvrVkExport createImages(ImageRequirements& imageReqs);

    void initEncoding();

    void present(u32 idx, u64 timelineVal, ViewsInfo const& views);
};

}

// TODO: Where to put
vk::Extent2D ensureInit();
