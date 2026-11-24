#include "shiftxor/backend.h"

#include <immintrin.h>

namespace shiftxor {

Status xor_shifted_avx2(ConstBufferView source, std::size_t source_offset,
                        BufferView destination, std::size_t destination_offset,
                        std::size_t bytes) {
    if (source.data == nullptr || destination.data == nullptr ||
        source_offset > source.size || bytes > source.size - source_offset ||
        destination_offset > destination.size || bytes > destination.size - destination_offset) {
        return Status::error(StatusCode::kBufferTooSmall,
                             "buffer range is outside the provided view");
    }

    if (bytes < 32 || source_offset % 32 != 0 || destination_offset % 32 != 0) {
        return xor_shifted_scalar(source, source_offset, destination,
                                  destination_offset, bytes);
    }

    std::size_t offset = 0;
    for (; offset + 32 <= bytes; offset += 32) {
        const auto source_value = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(source.data + source_offset + offset));
        const auto destination_value = _mm256_loadu_si256(
            reinterpret_cast<const __m256i*>(destination.data + destination_offset + offset));
        const auto result = _mm256_xor_si256(source_value, destination_value);
        _mm256_storeu_si256(
            reinterpret_cast<__m256i*>(destination.data + destination_offset + offset), result);
    }
    for (; offset < bytes; ++offset) {
        destination.data[destination_offset + offset] ^=
            source.data[source_offset + offset];
    }
    return Status::ok();
}

}
