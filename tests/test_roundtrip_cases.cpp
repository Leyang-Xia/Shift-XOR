#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "shiftxor/codec.h"
#include "shiftxor/matrix.h"

namespace {

struct CodeCase {
    std::size_t n;
    std::size_t k;
};

std::vector<std::size_t> make_survivors(const CodeCase config, bool from_front) {
    const auto parity = config.n - config.k;
    std::vector<std::size_t> survivors;
    const auto offset = from_front ? 0 : parity;
    for (std::size_t index = 0; index < config.k - parity; ++index) {
        survivors.push_back(offset + index);
    }
    for (std::size_t index = config.k; index < config.n; ++index) {
        survivors.push_back(index);
    }
    return survivors;
}

void run_case(const CodeCase config, bool from_front, std::size_t slice_bytes) {
    constexpr std::size_t length = 256;
    const auto matrix = shiftxor::ShiftMatrix::make_systematic_two_tone(config.n, config.k);
    assert(matrix);
    const auto codec_result = shiftxor::ShiftXorCodec::create(
        matrix.value(), {length, slice_bytes,
                         slice_bytes == 32 ? shiftxor::BackendKind::kAvx2
                                           : shiftxor::BackendKind::kScalar});
    assert(codec_result);
    const auto& codec = codec_result.value();

    std::vector<std::vector<std::uint8_t>> original(
        config.k, std::vector<std::uint8_t>(length));
    for (std::size_t block = 0; block < config.k; ++block) {
        std::uint32_t state = static_cast<std::uint32_t>(block + 1) * 0x9e3779b9u;
        for (std::size_t byte = 0; byte < length; ++byte) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            original[block][byte] = static_cast<std::uint8_t>(state);
        }
    }
    std::vector<shiftxor::ConstBufferView> data;
    for (const auto& block : original) {
        data.push_back({block.data(), block.size()});
    }

    std::vector<std::vector<std::uint8_t>> encoded;
    for (const auto row_length : codec.row_lengths()) {
        encoded.emplace_back(row_length);
    }
    std::vector<shiftxor::BufferView> codeword;
    for (auto& block : encoded) {
        codeword.push_back({block.data(), block.size()});
    }
    assert(codec.encode(data, codeword));

    const auto survivors = make_survivors(config, from_front);
    assert(codec.supports(survivors));
    std::vector<shiftxor::IndexedConstBufferView> available;
    for (const auto index : survivors) {
        available.push_back({index, {encoded[index].data(), encoded[index].size()}});
    }
    std::vector<std::vector<std::uint8_t>> decoded(
        config.k, std::vector<std::uint8_t>(length));
    std::vector<shiftxor::BufferView> output;
    for (auto& block : decoded) {
        output.push_back({block.data(), block.size()});
    }
    assert(codec.decode(available, output));
    for (std::size_t block = 0; block < config.k; ++block) {
        assert(std::memcmp(original[block].data(), decoded[block].data(), length) == 0);
    }
}

}  // namespace

int main() {
    const std::vector<CodeCase> cases{
        {3, 2}, {5, 3}, {8, 6}, {9, 6}, {11, 8}, {15, 10}};
    for (const auto config : cases) {
        for (const auto slice_bytes : {std::size_t{4}, std::size_t{8}, std::size_t{32}}) {
            run_case(config, true, slice_bytes);
            run_case(config, false, slice_bytes);
        }
    }
    return 0;
}

