#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "shiftxor/codec.h"
#include "shiftxor/matrix.h"

int main() {
    constexpr std::size_t data_blocks = 6;
    constexpr std::size_t code_blocks = 9;
    constexpr std::size_t input_length = 64 * 1024;
    constexpr std::size_t slice_bytes = 8;

    const auto matrix = shiftxor::ShiftMatrix::make_systematic_two_tone(
        code_blocks, data_blocks);
    if (!matrix) {
        std::cerr << matrix.status().message() << '\n';
        return 1;
    }
    const auto codec_result = shiftxor::ShiftXorCodec::create(
        matrix.value(), {input_length, slice_bytes, shiftxor::BackendKind::kScalar});
    if (!codec_result) {
        std::cerr << codec_result.status().message() << '\n';
        return 1;
    }
    const auto& codec = codec_result.value();

    std::vector<std::vector<std::uint8_t>> data_storage(
        data_blocks, std::vector<std::uint8_t>(input_length));
    for (std::size_t block = 0; block < data_blocks; ++block) {
        for (std::size_t byte = 0; byte < input_length; ++byte) {
            data_storage[block][byte] = static_cast<std::uint8_t>(block * 17 + byte);
        }
    }
    std::vector<shiftxor::ConstBufferView> data;
    for (const auto& block : data_storage) {
        data.push_back({block.data(), block.size()});
    }

    std::vector<std::vector<std::uint8_t>> code_storage;
    for (const auto length : codec.row_lengths()) {
        code_storage.emplace_back(length);
    }
    std::vector<shiftxor::BufferView> codeword;
    for (auto& block : code_storage) {
        codeword.push_back({block.data(), block.size()});
    }
    const auto encode_status = codec.encode(data, codeword);
    if (!encode_status) {
        std::cerr << encode_status.message() << '\n';
        return 1;
    }

    const std::vector<std::size_t> survivors{0, 1, 2, 6, 7, 8};
    std::vector<shiftxor::IndexedConstBufferView> available;
    for (const auto index : survivors) {
        available.push_back({index, {code_storage[index].data(), code_storage[index].size()}});
    }
    std::vector<std::vector<std::uint8_t>> decoded_storage(
        data_blocks, std::vector<std::uint8_t>(input_length));
    std::vector<shiftxor::BufferView> decoded;
    for (auto& block : decoded_storage) {
        decoded.push_back({block.data(), block.size()});
    }
    const auto decode_status = codec.decode(available, decoded);
    if (!decode_status) {
        std::cerr << decode_status.message() << '\n';
        return 1;
    }
    for (std::size_t block = 0; block < data_blocks; ++block) {
        if (std::memcmp(decoded_storage[block].data(), data_storage[block].data(),
                        input_length) != 0) {
            std::cerr << "decoded block " << block << " does not match" << '\n';
            return 1;
        }
    }
    std::cout << "general Shift-XOR (9,6) round-trip validated" << '\n';
    return 0;
}
