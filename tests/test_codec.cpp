#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "shiftxor/codec.h"
#include "shiftxor/matrix.h"

namespace {

std::vector<std::uint8_t> make_data(std::size_t length, std::uint8_t seed) {
    std::vector<std::uint8_t> data(length);
    for (std::size_t i = 0; i < length; ++i) {
        data[i] = static_cast<std::uint8_t>(seed + i * 3);
    }
    return data;
}

void assert_equal(const std::vector<std::uint8_t>& left,
                  const std::vector<std::uint8_t>& right) {
    assert(left.size() == right.size());
    assert(std::memcmp(left.data(), right.data(), left.size()) == 0);
}

}  // namespace

int main() {
    constexpr std::size_t input_length = 8;
    constexpr std::size_t slice_bytes = 2;
    const auto matrix = shiftxor::ShiftMatrix::create(
        2, 2, std::vector<std::int32_t>{0, shiftxor::ShiftMatrix::kInfinity,
                                        1, 0});
    assert(matrix);

    const auto codec_result = shiftxor::ShiftXorCodec::create(
        matrix.value(), {input_length, slice_bytes, shiftxor::BackendKind::kScalar});
    assert(codec_result);
    const auto& codec = codec_result.value();

    auto first = make_data(input_length, 1);
    auto second = make_data(input_length, 7);
    std::vector<shiftxor::ConstBufferView> data{
        {first.data(), first.size()}, {second.data(), second.size()}};
    std::vector<std::vector<std::uint8_t>> encoded_storage{
        std::vector<std::uint8_t>(codec.row_lengths()[0]),
        std::vector<std::uint8_t>(codec.row_lengths()[1])};
    std::vector<shiftxor::BufferView> encoded{
        {encoded_storage[0].data(), encoded_storage[0].size()},
        {encoded_storage[1].data(), encoded_storage[1].size()}};
    assert(codec.encode(data, encoded));
    assert(encoded_storage[0] == first);
    assert(encoded_storage[1].size() == 10);
    for (std::size_t i = 0; i < input_length; ++i) {
        const auto shifted_first = i >= slice_bytes ? first[i - slice_bytes] : 0;
        assert(encoded_storage[1][i] == static_cast<std::uint8_t>(shifted_first ^ second[i]));
    }

    const auto systematic = shiftxor::ShiftMatrix::make_systematic_two_tone(9, 6);
    assert(systematic);
    const auto systematic_codec_result = shiftxor::ShiftXorCodec::create(
        systematic.value(), {input_length, slice_bytes, shiftxor::BackendKind::kScalar});
    assert(systematic_codec_result);
    const auto& systematic_codec = systematic_codec_result.value();

    std::vector<std::vector<std::uint8_t>> originals;
    std::vector<shiftxor::ConstBufferView> original_views;
    for (std::size_t i = 0; i < 6; ++i) {
        originals.push_back(make_data(input_length, static_cast<std::uint8_t>(11 + i)));
        original_views.push_back({originals.back().data(), originals.back().size()});
    }
    std::vector<std::vector<std::uint8_t>> code_storage;
    std::vector<shiftxor::BufferView> code_views;
    for (const auto length : systematic_codec.row_lengths()) {
        code_storage.emplace_back(length);
        code_views.push_back({code_storage.back().data(), code_storage.back().size()});
    }
    assert(systematic_codec.encode(original_views, code_views));

    std::vector<shiftxor::IndexedConstBufferView> available;
    for (const std::size_t index : {0UL, 1UL, 2UL, 6UL, 7UL, 8UL}) {
        available.push_back({index, {code_storage[index].data(), code_storage[index].size()}});
    }
    std::vector<std::vector<std::uint8_t>> decoded_storage(6, std::vector<std::uint8_t>(input_length));
    std::vector<shiftxor::BufferView> decoded_views;
    for (auto& block : decoded_storage) {
        decoded_views.push_back({block.data(), block.size()});
    }
    assert(systematic_codec.supports({0, 1, 2, 6, 7, 8}));
    assert(systematic_codec.decode(available, decoded_views));
    for (std::size_t i = 0; i < originals.size(); ++i) {
        assert_equal(originals[i], decoded_storage[i]);
    }

    const std::vector<std::size_t> alternate_indices{0, 1, 5, 6, 7, 8};
    available.clear();
    for (const auto index : alternate_indices) {
        available.push_back({index, {code_storage[index].data(), code_storage[index].size()}});
    }
    for (auto& block : decoded_storage) {
        std::fill(block.begin(), block.end(), 0);
    }
    assert(systematic_codec.supports(alternate_indices));
    assert(systematic_codec.decode(available, decoded_views));
    for (std::size_t i = 0; i < originals.size(); ++i) {
        assert_equal(originals[i], decoded_storage[i]);
    }

    assert(systematic_codec.supports({0, 1, 2, 3, 4}).code() ==
           shiftxor::StatusCode::kInsufficientBlocks);
    assert(systematic_codec.supports({0, 1, 2, 3, 4, 4}).code() ==
           shiftxor::StatusCode::kInvalidArgument);
    assert(systematic_codec.supports({0, 1, 2, 3, 4, 9}).code() ==
           shiftxor::StatusCode::kUnsupported);

    const auto unsupported_matrix = shiftxor::ShiftMatrix::create(
        6, 3, std::vector<std::int32_t>{
                0, -1, -1, -1, 0, -1, -1, -1, 0,
                0, 1, 2, 0, 3, 6, 0, 2, 1});
    assert(unsupported_matrix);
    const auto unsupported_codec = shiftxor::ShiftXorCodec::create(
        unsupported_matrix.value(), {input_length, slice_bytes, shiftxor::BackendKind::kScalar});
    assert(unsupported_codec);
    assert(unsupported_codec.value().supports({3, 4, 5}).code() ==
           shiftxor::StatusCode::kUnsupported);

    std::vector<bool> survivor_mask(9, false);
    std::fill(survivor_mask.begin(), survivor_mask.begin() + 6, true);
    do {
        std::vector<std::size_t> subset;
        for (std::size_t index = 0; index < survivor_mask.size(); ++index) {
            if (survivor_mask[index]) {
                subset.push_back(index);
            }
        }
        available.clear();
        for (const auto index : subset) {
            available.push_back({index, {code_storage[index].data(), code_storage[index].size()}});
        }
        for (auto& block : decoded_storage) {
            std::fill(block.begin(), block.end(), 0);
        }
        assert(systematic_codec.supports(subset));
        assert(systematic_codec.decode(available, decoded_views));
        for (std::size_t block = 0; block < originals.size(); ++block) {
            assert_equal(originals[block], decoded_storage[block]);
        }
    } while (std::prev_permutation(survivor_mask.begin(), survivor_mask.end()));


    constexpr std::size_t large_k = 10;
    constexpr std::size_t large_n = 15;
    const auto large_matrix = shiftxor::ShiftMatrix::make_systematic_two_tone(large_n, large_k);
    assert(large_matrix);
    const auto large_codec_result = shiftxor::ShiftXorCodec::create(
        large_matrix.value(), {128, 8, shiftxor::BackendKind::kScalar});
    assert(large_codec_result);
    const auto& large_codec = large_codec_result.value();
    std::vector<std::vector<std::uint8_t>> large_originals(
        large_k, std::vector<std::uint8_t>(128));
    for (std::size_t block = 0; block < large_k; ++block) {
        std::uint32_t state = static_cast<std::uint32_t>(block + 1) * 0x9e3779b9u;
        for (std::size_t byte = 0; byte < 128; ++byte) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            large_originals[block][byte] = static_cast<std::uint8_t>(state);
        }
    }
    std::vector<shiftxor::ConstBufferView> large_data;
    for (const auto& block : large_originals) {
        large_data.push_back({block.data(), block.size()});
    }
    std::vector<std::vector<std::uint8_t>> large_code_storage;
    for (const auto length : large_codec.row_lengths()) {
        large_code_storage.emplace_back(length);
    }
    std::vector<shiftxor::BufferView> large_codeword;
    for (auto& block : large_code_storage) {
        large_codeword.push_back({block.data(), block.size()});
    }
    assert(large_codec.encode(large_data, large_codeword));
    const std::vector<std::size_t> large_survivors{5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    std::vector<shiftxor::IndexedConstBufferView> large_available;
    for (const auto index : large_survivors) {
        large_available.push_back({index, {large_code_storage[index].data(),
                                           large_code_storage[index].size()}});
    }
    std::vector<std::vector<std::uint8_t>> large_decoded_storage(
        large_k, std::vector<std::uint8_t>(128));
    std::vector<shiftxor::BufferView> large_decoded;
    for (auto& block : large_decoded_storage) {
        large_decoded.push_back({block.data(), block.size()});
    }
    assert(large_codec.supports(large_survivors));
    assert(large_codec.decode(large_available, large_decoded));
    for (std::size_t block = 0; block < large_k; ++block) {
        assert_equal(large_originals[block], large_decoded_storage[block]);
    }

    const auto generic_matrix = shiftxor::ShiftMatrix::create(
        5, 5, std::vector<std::int32_t>{
            8, 6, 4, 2, 0,
            4, 3, 2, 1, 0,
            0, 0, 0, 0, 0,
            0, 1, 2, 3, 4,
            0, 2, 4, 6, 8});
    assert(generic_matrix);
    assert(generic_matrix.value().is_two_tone());
    const auto generic_codec_result = shiftxor::ShiftXorCodec::create(
        generic_matrix.value(), {128, 8, shiftxor::BackendKind::kScalar});
    assert(generic_codec_result);
    const auto& generic_codec = generic_codec_result.value();
    std::vector<std::vector<std::uint8_t>> generic_originals(
        5, std::vector<std::uint8_t>(128));
    for (std::size_t block = 0; block < 5; ++block) {
        std::uint32_t state = static_cast<std::uint32_t>(block + 7) * 0x9e3779b9u;
        for (std::size_t byte = 0; byte < 128; ++byte) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            generic_originals[block][byte] = static_cast<std::uint8_t>(state);
        }
    }
    std::vector<shiftxor::ConstBufferView> generic_data;
    for (const auto& block : generic_originals) {
        generic_data.push_back({block.data(), block.size()});
    }
    std::vector<std::vector<std::uint8_t>> generic_code_storage;
    for (const auto length : generic_codec.row_lengths()) {
        generic_code_storage.emplace_back(length);
    }
    std::vector<shiftxor::BufferView> generic_codeword;
    for (auto& block : generic_code_storage) {
        generic_codeword.push_back({block.data(), block.size()});
    }
    assert(generic_codec.encode(generic_data, generic_codeword));
    std::vector<shiftxor::IndexedConstBufferView> generic_available;
    for (std::size_t row = 0; row < 5; ++row) {
        generic_available.push_back({row, {generic_code_storage[row].data(),
                                           generic_code_storage[row].size()}});
    }
    std::vector<std::vector<std::uint8_t>> generic_decoded_storage(
        5, std::vector<std::uint8_t>(128));
    std::vector<shiftxor::BufferView> generic_decoded;
    for (auto& block : generic_decoded_storage) {
        generic_decoded.push_back({block.data(), block.size()});
    }
    assert(generic_codec.supports({0, 1, 2, 3, 4}));
    assert(generic_codec.decode(generic_available, generic_decoded));
    for (std::size_t block = 0; block < 5; ++block) {
        assert_equal(generic_originals[block], generic_decoded_storage[block]);
    }

    const auto generic_wide_matrix = shiftxor::ShiftMatrix::create(
        6, 5, std::vector<std::int32_t>{
            8, 6, 4, 2, 0,
            4, 3, 2, 1, 0,
            0, 0, 0, 0, 0,
            0, 1, 2, 3, 4,
            0, 2, 4, 6, 8,
            0, 3, 6, 9, 12});
    assert(generic_wide_matrix);
    const auto generic_wide_codec_result = shiftxor::ShiftXorCodec::create(
        generic_wide_matrix.value(), {128, 8, shiftxor::BackendKind::kScalar});
    assert(generic_wide_codec_result);
    const auto& generic_wide_codec = generic_wide_codec_result.value();
    std::vector<std::vector<std::uint8_t>> generic_wide_code_storage;
    for (const auto length : generic_wide_codec.row_lengths()) {
        generic_wide_code_storage.emplace_back(length);
    }
    std::vector<shiftxor::BufferView> generic_wide_codeword;
    for (auto& block : generic_wide_code_storage) {
        generic_wide_codeword.push_back({block.data(), block.size()});
    }
    assert(generic_wide_codec.encode(generic_data, generic_wide_codeword));
    const std::vector<std::size_t> generic_wide_survivors{0, 1, 2, 3, 5};
    std::vector<shiftxor::IndexedConstBufferView> generic_wide_available;
    for (const auto index : generic_wide_survivors) {
        generic_wide_available.push_back({index, {generic_wide_code_storage[index].data(),
                                                  generic_wide_code_storage[index].size()}});
    }
    std::vector<std::vector<std::uint8_t>> generic_wide_decoded_storage(
        5, std::vector<std::uint8_t>(128));
    std::vector<shiftxor::BufferView> generic_wide_decoded;
    for (auto& block : generic_wide_decoded_storage) {
        generic_wide_decoded.push_back({block.data(), block.size()});
    }
    assert(generic_wide_codec.supports(generic_wide_survivors));
    assert(generic_wide_codec.decode(generic_wide_available, generic_wide_decoded));
    for (std::size_t block = 0; block < 5; ++block) {
        assert_equal(generic_originals[block], generic_wide_decoded_storage[block]);
    }

    return 0;
}
