#pragma once

#include <functional>
#include <mutex>

#include "utils.hpp"

extern "C" {
#include "alvr_binding.h"
}

// TODO: Make thread safe
class CallbackManager : public Singleton<CallbackManager> {
    template <typename T> using Fns = std::vector<std::function<T>>;

    // TODO: This is bad for performance, would be fine if rwlock
    std::mutex mutex;

    Fns<void(u64, AlvrSpaceRelation)> trackingUpdated;
    Fns<void(ViewsConfig_Body)> viewsConfig;

    // TODO: This is... interesting
    Optional<ViewsConfig_Body> lastViewsConfig;

public:
    // NOTE: Callbacks cause a lock, so beware
    template <AlvrEvent_Tag eventType, typename T> void registerCb(T cb)
    {
        std::lock_guard mutexGuard_(mutex);

        if constexpr (eventType == ALVR_EVENT_TRACKING_UPDATED) {
            trackingUpdated.push_back(std::move(cb));
        } else if constexpr (eventType == ALVR_EVENT_VIEWS_CONFIG) {
            viewsConfig.push_back(std::move(cb));

            // NOTE: This pushes to all consumers
            if (lastViewsConfig.hasValue()) {
                dispatch<ALVR_EVENT_VIEWS_CONFIG>(lastViewsConfig.get());
            }
        } else {
            assert(false);
        }
    }

    template <AlvrEvent_Tag eventType, typename... Ts> void dispatch(Ts&&... params)
    {
        std::lock_guard mutexGuard_(mutex);

        auto& fns = [&]() -> auto& {
            if constexpr (eventType == ALVR_EVENT_TRACKING_UPDATED) {
                return trackingUpdated;
            } else if constexpr (eventType == ALVR_EVENT_VIEWS_CONFIG) {
                lastViewsConfig.emplace(std::get<0>(std::tuple { params... }));

                return viewsConfig;
            } else {
                assert(false);
            }
        }();

        for (auto& fn : fns) {
            // needs to also lock because the push_back could move the vec in memory
            fn(std::forward<Ts>(params)...);
        }
    }
};
