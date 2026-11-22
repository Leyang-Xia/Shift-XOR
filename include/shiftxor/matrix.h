#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "shiftxor/status.h"

namespace shiftxor {

class ShiftMatrix {
public:
    static constexpr std::int32_t kInfinity = -1;

    static Result<ShiftMatrix> create(std::size_t rows, std::size_t cols,
                                      std::vector<std::int32_t> shifts);
    static Result<ShiftMatrix> make_systematic_two_tone(std::size_t n,
                                                        std::size_t k);

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    std::int32_t at(std::size_t row, std::size_t col) const;
    std::int32_t row_max_shift(std::size_t row) const;
    std::size_t row_length(std::size_t row, std::size_t input_length,
                           std::size_t slice_bytes) const;
    std::size_t storage_overhead(std::size_t slice_bytes) const;
    bool is_systematic() const;
    bool is_two_tone() const;

private:
    ShiftMatrix(std::size_t rows, std::size_t cols,
                std::vector<std::int32_t> shifts)
        : rows_(rows), cols_(cols), shifts_(std::move(shifts)) {}

    bool is_two_tone_rows(std::size_t first_row, std::size_t row_count) const;

    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
    std::vector<std::int32_t> shifts_;
};

}
