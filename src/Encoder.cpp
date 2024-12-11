#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <source_location>
#include <thread>
#include <type_traits>

extern "C" {
#include "alvr_binding.h"
#include "monado_interface.h"
}

#include "alvr_server/Settings.h"

#include "Encoder.hpp"
#include "EventManager.hpp"

// TODO: Move handling code into it's own library?

// TODO: Move Monado code out into it's own file

void handleEvents()
{
    auto ids = alvr_get_ids();

    while (true) {
        AlvrEvent event;
        auto gotEvent = alvr_poll_event(&event, 1000000);

        if (gotEvent == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // TODO: Handle connection and disconnection
        if (event.tag == ALVR_EVENT_TRACKING_UPDATED) {
            auto ts = event.TRACKING_UPDATED.sample_timestamp_ns;

            AlvrDeviceMotion motion;
            if (alvr_get_device_motion(ids.head, ts, &motion)) {
                CallbackManager::get().dispatch<ALVR_EVENT_TRACKING_UPDATED>(ts, motion);
            }
        } else if (event.tag == ALVR_EVENT_VIEWS_PARAMS) {
            auto params = event.views_params;
            ViewsInfo info {
                .left = params[0],
                .right = params[1],
            };

            CallbackManager::get().dispatch<ALVR_EVENT_VIEWS_PARAMS>(info);
        } else {
            std::cout << "event handler for tag " << (u32)event.tag << " not yet implemend\n";
        }
    }
}

vk::Extent2D ensureInit()
{
    struct InitManager : Singleton<InitManager> {
        vk::Extent2D extent;

        InitManager()
        {
            // TODO: Make this less janky
            auto const homeDir = std::string(std::getenv("HOME"));
            auto confDir = homeDir + "/.config/alvr/";

            alvr_initialize_environment(confDir.c_str(), homeDir.c_str());

            auto sessionLog = homeDir + "/alvr_session.log";
            auto crashLog = homeDir + "/alvr_crash.log";

            alvr_initialize_logging(sessionLog.c_str(), crashLog.c_str());

            auto targetCfg = alvr_initialize();

            // NOTE: We don't care about the emulated size because there's no reason to have them differ
            extent.width = targetCfg.stream_width;
            extent.height = targetCfg.stream_height;

            // TODO: Should we actually run this here? Or should we put it into the driver and not have the headache of
            // caching dispatches?
            alvr_start_connection();

            {
                std::string json;
                auto jsonLen = alvr_get_settings_json(nullptr);
                json.resize(jsonLen);
                alvr_get_settings_json(json.data());

                Settings::Instance().Load(std::move(json));
            }

            std::thread t { handleEvents };
            t.detach();
        }
    };

    return InitManager::get().extent;
}

template <typename... T>
concept floats = (std::is_same_v<float, T> && ...);

template <typename... T>
auto makeSpecs(T... args)
    requires floats<T...>
{
    alvr::render::PipelineCreateInfo info {};

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

// TODO: #embed will have to do...
inline std::vector<u8> loadShaderFile(std::string shaderName)
{
    auto srcFile = std::source_location::current().file_name();
    auto shader = std::filesystem::path(srcFile).remove_filename() / "shader/" / (shaderName + ".comp.spv");

    std::ifstream stream(shader, std::ios::binary);
    stream.seekg(0, std::ios::end);
    auto len = stream.tellg();
    stream.seekg(0, std::ios::beg);

    auto data = std::vector<u8>(len);
    stream.read(reinterpret_cast<char*>(data.data()), len);

    return data;
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

    info.shaderData = loadShaderFile("color");

    return info;
}

inline auto makeFoveation(Settings const& settings, vk::Extent2D extent)
{
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

    vk::Extent2D outSize {
        .width = optimizedEyeWidthAligned * 2,
        .height = optimizedEyeHeightAligned,
    };

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

    info.shaderData = loadShaderFile("ffr");

    return std::tuple(info, outSize);
}

// TODO: Clean up
void ParseFrameNals(
    int codec, AlvrViewParams const* viewParams,  unsigned char *buf, int len, unsigned long long targetTimestampNs, bool isIdr);


namespace alvr {

AlvrVkExport Encoder::createImages(ImageRequirements& imageReqs)
{
    auto const& settings = Settings::Instance();

    vk::Extent2D inExtent {
        .width = imageReqs.extent.width,
        .height = imageReqs.extent.height,
    };

    std::vector<render::PipelineCreateInfo> pipeCIs;
    auto outExtent = inExtent;

    // TODO: Where to get it from
    if (settings.m_enableColorCorrection)
        pipeCIs.push_back(makeColorCorrection(settings, inExtent));

    if (settings.m_enableFoveatedEncoding) {
        auto [info, newExtent] = makeFoveation(settings, inExtent);
        outExtent = newExtent;

        pipeCIs.push_back(info);
    }

    if (pipeCIs.empty()) {
        pipeCIs.push_back({
            .shaderData = loadShaderFile("quad"),
        });
    }

    renderer.emplace(vkCtx, imageReqs, pipeCIs);

    return renderer.get().getImages();
}

void Encoder::initEncoding()
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

    // av_log_set_level(AV_LOG_DEBUG);

    // TODO: The EncodePipeline should store this on it's own
    auto& avHwCtx = *new alvr::HWContext(vkCtx);

    auto out = renderer.get().getOutput();

    // auto framCtx = new alvr::VkFrameCtx(aCtx, *(vk::ImageCreateInfo*)&out.imageCI);

    auto frame = new alvr::VkFrame(vkCtx, out.image.image, out.imageCI, out.size, out.image.memory, out.drm);

    encoder = EncodePipeline::Create(vkCtx, devicePath, *frame, out.imageCI.extent.width, out.imageCI.extent.height);

    idrScheduler.OnStreamStart();
}

void Encoder::present(u32 idx, u64 timelineVal, ViewsInfo const& views)
{
    renderer.get().render(vkCtx, idx, timelineVal);

    // TODO: not sure, but might actually work
    static u64 counter = 0;

    encoder->PushFrame(counter++, /* idrScheduler.CheckIDRInsertion() */ true);

    alvr::FramePacket framePacket;
    if (!encoder->GetEncoded(framePacket)) {
        assert(false);
    }

    // TODO: This constant conversion sucks
    AlvrViewParams viewParams[2];
    viewParams[0] = views.left;
    viewParams[1] = views.right;

    ParseFrameNals(encoder->GetCodec(), viewParams, framePacket.data, framePacket.size, framePacket.pts, framePacket.isIDR);
}

}
