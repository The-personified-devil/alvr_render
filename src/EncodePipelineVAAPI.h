#pragma once

#include "EncodePipeline.h"
#include "ffmpeg_helper.h"

extern "C" struct AVBufferRef;
extern "C" struct AVCodecContext;
extern "C" struct AVFilterContext;
extern "C" struct AVFilterGraph;
extern "C" struct AVFrame;

class Renderer;

namespace alvr
{

#define PRESET_MODE_SPEED   (0)
#define PRESET_MODE_BALANCE (1)
#define PRESET_MODE_QUALITY (2)

enum EncoderQualityPreset {
	QUALITY = 0,
	BALANCED = 1,
	SPEED = 2
};

class EncodePipelineVAAPI: public EncodePipeline
{
public:
  ~EncodePipelineVAAPI();
  EncodePipelineVAAPI(HWContext &vk_ctx, std::string devicePath, alvr::Vendor vendor, VkFrame &input_frame, uint32_t width, uint32_t height);

  // TODO: Don't pass the timestamp here we're literally just passing it through (or does it help with dumped videos?)
  void PushFrame(uint64_t targetTimestampNs, bool idr) override;
  void SetParams(FfiDynamicEncoderParams params) override;

private:
  AVBufferRef *hw_ctx = nullptr;
  AVBufferRef *drm_ctx = nullptr;
  AVFrame *mapped_frame = nullptr;
  AVFrame *encoder_frame = nullptr;
  AVFilterGraph *filter_graph = nullptr;
  AVFilterContext *filter_in = nullptr;
  AVFilterContext *filter_out = nullptr;

   union vlVaQualityBits {
      unsigned int quality;
      struct {
         unsigned int valid_setting: 1;
         unsigned int preset_mode: 2;
         unsigned int pre_encode_mode: 1;
         unsigned int vbaq_mode: 1;
         unsigned int reservered: 27;
      };
   };

  };;
}
