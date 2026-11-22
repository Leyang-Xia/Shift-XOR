#pragma once

#include <cstddef>
#include <cstdint>

#include "shiftxor/buffer.h"
#include "shiftxor/status.h"

namespace shiftxor {

enum class BackendKind {
    kScalar,
    kAvx2,
};

bool avx2_available();
Status xor_shifted_scalar(ConstBufferView source, std::size_t source_offset,
                          BufferView destination, std::size_t destination_offset,
                          std::size_t bytes);
Status xor_shifted_avx2(ConstBufferView source, std::size_t source_offset,
                        BufferView destination, std::size_t destination_offset,
                        std::size_t bytes);

}
