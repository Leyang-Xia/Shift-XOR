#include <cassert>
#include <cstdint>
#include <vector>

#include "shiftxor/backend.h"
#include "shiftxor/codec.h"
#include "shiftxor/matrix.h"

int main() {
    constexpr std::size_t length = 128;
    constexpr std::size_t slice_bytes = 32;
    const auto matrix = shiftxor::ShiftMatrix::make_systematic_two_tone(9, 6);
    assert(matrix);
    const auto scalar_result = shiftxor::ShiftXorCodec::create(
        matrix.value(), {length, slice_bytes, shiftxor::BackendKind::kScalar});
    const auto avx2_result = shiftxor::ShiftXorCodec::create(
        matrix.value(), {length, slice_bytes, shiftxor::BackendKind::kAvx2});
    assert(scalar_result && avx2_result);

    std::vector<std::vector<std::uint8_t>> data_storage(6,
                                                         std::vector<std::uint8_t>(length));
    for (std::size_t block = 0; block < data_storage.size(); ++block) {
        for (std::size_t byte = 0; byte < length; ++byte) {
            data_storage[block][byte] = static_cast<std::uint8_t>(block * 17 + byte);
        }
    }
    std::vector<shiftxor::ConstBufferView> data;
    for (const auto& block : data_storage) {
        data.push_back({block.data(), block.size()});
    }

    std::vector<std::vector<std::uint8_t>> scalar_storage;
    std::vector<shiftxor::BufferView> scalar_codeword;
    std::vector<std::vector<std::uint8_t>> avx2_storage;
    std::vector<shiftxor::BufferView> avx2_codeword;
    for (const auto row_length : scalar_result.value().row_lengths()) {
        scalar_storage.emplace_back(row_length);
        scalar_codeword.push_back({scalar_storage.back().data(), row_length});
        avx2_storage.emplace_back(row_length);
        avx2_codeword.push_back({avx2_storage.back().data(), row_length});
    }
    assert(scalar_result.value().encode(data, scalar_codeword));
    assert(avx2_result.value().encode(data, avx2_codeword));
    assert(scalar_storage == avx2_storage);

    if (shiftxor::avx2_available()) {
        assert(shiftxor::xor_shifted_avx2(
            {data_storage[0].data(), data_storage[0].size()}, 0,
            {avx2_storage[0].data(), avx2_storage[0].size()}, 0, 32));
    }
    return 0;
}
