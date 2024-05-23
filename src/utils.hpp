#pragma once

#include <cassert>
#include <cstdint>
#include <cstddef>
#include <utility>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using usize = size_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using f32 = float;
using f64 = double;

// TODO: Finish this
template <typename T>
class Optional {
    union {
        T value;
        char dummy;
    };

    bool hasValue = false;

public:
    Optional() { }

    template <typename... CTs>
    void emplace(CTs&&... cvals)
    {
        new (&value) T { std::forward<CTs>(cvals)... };
        hasValue = true;
    }

    T& get()
    {
        if (hasValue)
            return value;

        assert(false);
    }

    ~Optional()
    {
        if (hasValue)
            value.~T();
    }
};
