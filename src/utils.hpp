#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
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
template <typename T> class Optional {
    union {
        T value;
        char dummy;
    };

    bool hasValue_ = false;

public:
    Optional() { }

    template <typename... CTs> void emplace(CTs&&... cvals)
    {
        if (hasValue_)
            value.~T();

        new (&value) T { std::forward<CTs>(cvals)... };
        hasValue_ = true;
    }

    void erase() {
        if (hasValue_)
            value.~T();

        hasValue_ = false;
    }

    T& get()
    {
        if (hasValue_)
            return value;

        assert(false);
    }

    bool hasValue() { return hasValue_; }

    ~Optional()
    {
        if (hasValue_)
            value.~T();
    }
};

template <typename T> class Singleton {
public:
    Singleton() = default;
    Singleton(const Singleton&) = delete;
    Singleton(Singleton&&) = delete;
    decltype(auto) operator=(const Singleton&) = delete;
    decltype(auto) operator=(Singleton&&) = delete;

    static T& get()
    {
        static T val;
        return val;
    }
};
