#include "shiftxor/backend.h"

#include <cstring>

namespace shiftxor {

bool avx2_available() {
#if defined(SHIFT_XOR_HAS_AVX2) && (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(_M_X64))
    return __builtin_cpu_supports("avx2") != 0;
#else
    return false;
#endif
}

Status xor_shifted_scalar(ConstBufferView source, std::size_t source_offset,
                          BufferView destination, std::size_t destination_offset,
                          std::size_t bytes) {
    if (source.data == nullptr || destination.data == nullptr ||
        source_offset + bytes > source.size || destination_offset + bytes > destination.size) {
        return Status::error(StatusCode::kBufferTooSmall,
                             "buffer range is outside the provided view");
    }
    for (std::size_t i = 0; i < bytes; ++i) {
        destination.data[destination_offset + i] ^= source.data[source_offset + i];
    }
    return Status::ok();
}

#ifndef SHIFT_XOR_HAS_AVX2
Status xor_shifted_avx2(ConstBufferView source, std::size_t source_offset,
                        BufferView destination, std::size_t destination_offset,
                        std::size_t bytes) {
    return xor_shifted_scalar(source, source_offset, destination, destination_offset,
                              bytes);
}
#endif

}
