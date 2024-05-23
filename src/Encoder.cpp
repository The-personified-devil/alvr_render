extern "C" {
#include "alvr_binding.h"
#include "monado_interface.h"
}

#include "Encoder.hpp"
#include "alvr_server/Settings.h"

extern "C" struct Proxy* rendr_create_encoder(struct AlvrVkInfo* info)
{
    alvr_initialize_logging();
    alvr_initialize(nullptr);
    alvr_start_connection();

    Settings::Instance().Load();
    // queue_mutex = info->queue_mutex;
    return (Proxy*)new alvr::Encoder(*info);
}

extern "C" void rendr_init_images(struct Proxy* enc, struct ImageRequirements* imgReqs, struct AlvrVkExport* expt)
{
    *expt = ((alvr::Encoder*)enc)->createImages(*imgReqs);
    ((alvr::Encoder*)enc)->initEncoding();
}

extern "C" void rendr_present(struct Proxy* enc, uint64_t timeline_value,
    uint32_t img_idx)
{
    auto& en = *((alvr::Encoder*)enc);
    en.present(img_idx, timeline_value);
}
