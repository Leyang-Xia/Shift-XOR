#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "shiftxor/backend.h"
#include "shiftxor/buffer.h"
#include "shiftxor/matrix.h"
#include "shiftxor/status.h"

namespace shiftxor {

struct CodecConfig {
    std::size_t input_length = 0;
    std::size_t slice_bytes = 0;
    BackendKind backend = BackendKind::kScalar;
};

class ShiftXorCodec {
public:
    static Result<ShiftXorCodec> create(ShiftMatrix matrix, CodecConfig config);

    Status encode(const std::vector<ConstBufferView>& data,
                  const std::vector<BufferView>& codeword) const;
    Status decode(const std::vector<IndexedConstBufferView>& available,
                  const std::vector<BufferView>& data_out) const;
    Status supports(const std::vector<std::size_t>& survivor_indices) const;

    const std::vector<std::size_t>& row_lengths() const { return row_lengths_; }
    std::size_t input_length() const { return config_.input_length; }
    std::size_t slice_bytes() const { return config_.slice_bytes; }
    std::size_t data_blocks() const { return matrix_.cols(); }
    std::size_t code_blocks() const { return matrix_.rows(); }

private:
    ShiftXorCodec(ShiftMatrix matrix, CodecConfig config,
                  std::vector<std::size_t> row_lengths)
        : matrix_(std::move(matrix)), config_(config),
          row_lengths_(std::move(row_lengths)) {}

    Status validate_survivors(const std::vector<std::size_t>& survivors) const;
    Status xor_shifted(ConstBufferView source, std::size_t source_offset,
                       BufferView destination, std::size_t destination_offset,
                       std::size_t bytes) const;

    ShiftMatrix matrix_;
    CodecConfig config_;
    std::vector<std::size_t> row_lengths_;
};

}
