#pragma once

#include "EncodePipeline.h"
#include "FrameRender.h"
#include "alvr_server/IDRScheduler.h"
#include "ffmpeg_helper.h"
#include <atomic>
#include <cassert>
#include <memory>
#include <poll.h>
#include <sys/types.h>
#include <vulkan/vulkan_core.h>

// TODO: Finish this
template <typename T> class Optional {
    union {
        T value;
    };

    bool hasValue = false;

  public:
    Optional() {}

    template <typename... CTs> void emplace(CTs &&...cvals) {
        new(&value) T{std::forward<CTs>(cvals)...};
	hasValue = true;
    }

    T &get() {
        if (hasValue)
            return value;

        assert(false);
    }

    ~Optional() {
        if (hasValue)
            value.~T();
    }
};

struct RenderCtx {
    FrameRender render;
    Renderer::Output output;
    alvr::VkFrameCtx vk_frame_ctx;
    alvr::VkFrame frame;
    std::unique_ptr<alvr::EncodePipeline> encode_pipeline;

    RenderCtx(alvr::VkContext vk_ctx, VkImageCreateInfo imgCreateInfo)
        : render(vk_ctx, 3, imgCreateInfo), output(render.CreateOutput()),
          vk_frame_ctx(vk_ctx, output.imageInfo),
          frame(vk_ctx, output.image, output.imageInfo, output.size, output.memory, output.drm),
          encode_pipeline(alvr::EncodePipeline::Create(&render,
                                                       vk_ctx,
                                                       frame,
                                                       vk_frame_ctx,
                                                       render.GetEncodingWidth(),
                                                       render.GetEncodingHeight())) {}
};

class CEncoder {
  public:
    CEncoder(VkInstance, VkPhysicalDevice, VkDevice, uint32_t);
    ~CEncoder();

    void InitImages();
    void Present(uint64_t frame, uint64_t semaphore_value, uint32_t img_idx);

    void OnStreamStart();
    void OnPacketLoss();
    void InsertIDR();

    bool IsConnected() {
        // TODO: Replace or delete
        return true;
    }
    void CaptureFrame();

  private:
    IDRScheduler m_scheduler;
    std::atomic_bool m_captureFrame = false;
    alvr::VkContext vk_ctx;
    // TODO: This is fucking stupid, pls encode state in type
    Optional<RenderCtx> renderCtx;
};
