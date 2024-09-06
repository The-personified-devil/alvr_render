#pragma once

#include <functional>

#include "utils.hpp"

extern "C" {
#include "alvr_binding.h"
}

// TODO: Make thread safe
class CallbackManager : public Singleton<CallbackManager> {
    template <typename T> using Fns = std::vector<std::function<T>>;

    Fns<void(u64, AlvrSpaceRelation)> tracking_updated;

public:
    template <AlvrEvent_Tag eventType, typename T> void registerCb(T cb)
    {
        if constexpr (eventType == ALVR_EVENT_TRACKING_UPDATED) {
            tracking_updated.push_back(std::move(cb));
        } else {
            assert(false);
        }
    }

    template <AlvrEvent_Tag eventType, typename... Ts> void dispatch(Ts&&... params)
    {
        auto& fns = [&]() -> auto& {
            if constexpr (eventType == ALVR_EVENT_TRACKING_UPDATED) {
                return tracking_updated;
            } else {
                assert(false);
            }
        }();

        for (auto& fn : fns) {
            fn(std::forward<Ts>(params)...);
        }
    }
};
