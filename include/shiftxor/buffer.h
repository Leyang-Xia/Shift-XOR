#pragma once

#include <cstddef>
#include <cstdint>

namespace shiftxor {

struct BufferView {
    std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct ConstBufferView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct IndexedConstBufferView {
    std::size_t index = 0;
    ConstBufferView buffer{};
};

}
