#include "MEncoder.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <poll.h>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <stdlib.h>
#include <string>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

#include "EncodePipeline.h"
#include "FrameRender.h"
#include "alvr_server/Logger.h"
#include "alvr_server/Settings.h"
#include "ffmpeg_helper.h"
#include "interface.h"

extern "C" {
#include "monado_interface.h"
#include <libavutil/avutil.h>
}

extern pthread_mutex_t *queue_mutex;

extern "C" struct Proxy *rendr_create_encoder(struct AlvrVkInfo *info) {
  Settings::Instance().Load();
  queue_mutex = info->queue_mutex;
  return (Proxy *)new CEncoder(info->instance, info->physDev, info->device,
                               info->queueFamIdx);
}

extern "C" void rendr_init_images(struct Proxy *enc) {
  ((CEncoder *)enc)->InitImages();
}

extern "C" void rendr_present(struct Proxy *enc, uint64_t timeline_value,
                              uint32_t img_idx) {
  ((CEncoder *)enc)->Present(0, timeline_value, img_idx);
}

CEncoder::CEncoder(VkInstance instance, VkPhysicalDevice physDev, VkDevice dev,
                   uint32_t queue_fam_idx)
    : vk_ctx(instance, physDev, dev, queue_fam_idx) {}

CEncoder::~CEncoder() {}

pthread_mutex_t render_mutex;
pthread_mutex_t double_mutex;

uint64_t tl_val;
uint32_t img_idx;

void *g_encoder;

namespace {

void av_logfn(void *, int level, const char *data, va_list va) {
  if (level >
#ifdef DEBUG
      AV_LOG_DEBUG)
#else
      AV_LOG_INFO)
#endif
    return;

  char buf[256];
  vsnprintf(buf, sizeof(buf), data, va);

  if (level <= AV_LOG_ERROR)
    Error("Encoder: %s", buf);
  else
    Info("Encoder: %s", buf);
}

} // namespace
//

void CEncoder::InitImages() {
  // TODO: Goofy when using software encoder
  av_log_set_callback(av_logfn);

  // uint32_t num_images = 3;

  // TODO: Actually get this info from the config
  VkImageCreateInfo image_create_info{};
  image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_create_info.imageType = VK_IMAGE_TYPE_2D;
  image_create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_create_info.extent.width = 2000;
  image_create_info.extent.height = 2000;
  image_create_info.extent.depth = 1;
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_create_info.usage = VK_IMAGE_USAGE_STORAGE_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  renderCtx.emplace(vk_ctx, image_create_info);

  // TODO: Get rid of this lmao
  g_encoder = (void *)this;

  // TODO: This shouldn't even exist, no?
  // DriverReadyIdle(true);
}

void CEncoder::Present(uint64_t frame, uint64_t semaphore_value,
                       uint32_t img_idx) {
  auto &ctx = renderCtx.get();

  ctx.encode_pipeline->SetParams(
      /* GetDynamicEncoderParams() */ FfiDynamicEncoderParams{
          .updated = false,
      });

  if (m_captureFrame) {
    m_captureFrame = false;
    ctx.render.CaptureInputFrame(Settings::Instance().m_captureFrameDir +
                                 "/alvr_frame_input.ppm");
    ctx.render.CaptureOutputFrame(Settings::Instance().m_captureFrameDir +
                                  "/alvr_frame_output.ppm");
  }

  ctx.render.Render(img_idx, semaphore_value);

  bool is_idr = m_scheduler.CheckIDRInsertion();
  ctx.encode_pipeline->PushFrame(0, is_idr);

  alvr::FramePacket packet;
  if (!ctx.encode_pipeline->GetEncoded(packet)) {
    Error("Failed to get encoded data!");
    return;
  }

  // TODO: Actually hand over info to alvr (-> sync with alvr startup)

  // ParseFrameNals(ctx.encode_pipeline->GetCodec(), packet.data, packet.size,
  //                packet.pts, packet.isIDR);
}

void CEncoder::OnStreamStart() { m_scheduler.OnStreamStart(); }

void CEncoder::OnPacketLoss() { m_scheduler.OnPacketLoss(); }

void CEncoder::InsertIDR() { m_scheduler.InsertIDR(); }

void CEncoder::CaptureFrame() { m_captureFrame = true; }
