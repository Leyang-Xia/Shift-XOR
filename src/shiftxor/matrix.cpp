#include "shiftxor/matrix.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace shiftxor {

Result<ShiftMatrix> ShiftMatrix::create(std::size_t rows, std::size_t cols,
                                         std::vector<std::int32_t> shifts) {
    if (rows == 0 || cols == 0 ||
        (rows > std::numeric_limits<std::size_t>::max() / cols) ||
        shifts.size() != rows * cols) {
        return Status::error(StatusCode::kInvalidMatrix,
                             "matrix dimensions or shift count are invalid");
    }
    for (const auto shift : shifts) {
        if (shift < kInfinity) {
            return Status::error(StatusCode::kInvalidMatrix,
                                 "shift must be -1 or non-negative");
        }
    }
    return ShiftMatrix(rows, cols, std::move(shifts));
}

Result<ShiftMatrix> ShiftMatrix::make_systematic_two_tone(std::size_t n,
                                                            std::size_t k) {
    if (k == 0 || n < k) {
        return Status::error(StatusCode::kInvalidMatrix,
                             "systematic code requires 0 < k <= n");
    }

    if (n > std::numeric_limits<std::size_t>::max() / k) {
        return Status::error(StatusCode::kInvalidMatrix,
                             "systematic matrix dimensions overflow");
    }
    std::vector<std::int32_t> shifts(n * k, kInfinity);
    for (std::size_t row = 0; row < k; ++row) {
        shifts[row * k + row] = 0;
    }

    const std::size_t parity_rows = n - k;
    if (parity_rows > 0) {
        const std::size_t divide = parity_rows / 2 + 1;
        for (std::size_t row = 0; row < parity_rows; ++row) {
            for (std::size_t col = 0; col < k; ++col) {
                std::int32_t shift = 0;
                if (row + 1 < divide) {
                    shift = static_cast<std::int32_t>((divide - row - 1) * (k - col - 1));
                } else if (row + 1 > divide) {
                    shift = static_cast<std::int32_t>((row + 1 - divide) * col);
                }
                shifts[(k + row) * k + col] = shift;
            }
        }
    }
    return ShiftMatrix(n, k, std::move(shifts));
}

std::int32_t ShiftMatrix::at(std::size_t row, std::size_t col) const {
    if (row >= rows_ || col >= cols_) {
        throw std::out_of_range("shift matrix index out of range");
    }
    return shifts_[row * cols_ + col];
}

std::int32_t ShiftMatrix::row_max_shift(std::size_t row) const {
    if (row >= rows_) {
        throw std::out_of_range("shift matrix row out of range");
    }
    std::int32_t maximum = kInfinity;
    for (std::size_t col = 0; col < cols_; ++col) {
        maximum = std::max(maximum, at(row, col));
    }
    return maximum;
}

std::size_t ShiftMatrix::row_length(std::size_t row, std::size_t input_length,
                                    std::size_t slice_bytes) const {
    if (slice_bytes == 0 || input_length % slice_bytes != 0) {
        throw std::invalid_argument("input length must be divisible by slice width");
    }
    const auto maximum = row_max_shift(row);
    if (maximum == kInfinity) {
        return 0;
    }
    const auto shift = static_cast<std::size_t>(maximum);
    if (shift > (std::numeric_limits<std::size_t>::max() - input_length) / slice_bytes) {
        throw std::overflow_error("row length overflows size_t");
    }
    return input_length + shift * slice_bytes;
}

std::size_t ShiftMatrix::storage_overhead(std::size_t slice_bytes) const {
    if (slice_bytes == 0) {
        throw std::invalid_argument("slice width must be positive");
    }
    std::size_t overhead = 0;
    for (std::size_t row = 0; row < rows_; ++row) {
        const auto maximum = row_max_shift(row);
        if (maximum != kInfinity) {
            const auto shift = static_cast<std::size_t>(maximum);
            if (shift > std::numeric_limits<std::size_t>::max() / slice_bytes ||
                overhead > std::numeric_limits<std::size_t>::max() - shift * slice_bytes) {
                throw std::overflow_error("storage overhead overflows size_t");
            }
            overhead += shift * slice_bytes;
        }
    }
    return overhead;
}

bool ShiftMatrix::is_systematic() const {
    if (rows_ < cols_) {
        return false;
    }
    for (std::size_t row = 0; row < cols_; ++row) {
        for (std::size_t col = 0; col < cols_; ++col) {
            const auto expected = row == col ? 0 : kInfinity;
            if (at(row, col) != expected) {
                return false;
            }
        }
    }
    return true;
}

bool ShiftMatrix::is_two_tone_rows(std::size_t first_row,
                                   std::size_t row_count) const {
    if (row_count == 0 || first_row + row_count > rows_) {
        return false;
    }
    if (cols_ < 2) {
        for (std::size_t row = 0; row < row_count; ++row) {
            if (at(first_row + row, 0) == kInfinity) {
                return false;
            }
        }
        return true;
    }
    for (std::size_t first_col = 0; first_col < cols_; ++first_col) {
        for (std::size_t second_col = first_col + 1; second_col < cols_; ++second_col) {
            std::vector<std::int32_t> differences;
            differences.reserve(row_count);
            for (std::size_t row = 0; row < row_count; ++row) {
                const auto left = at(first_row + row, first_col);
                const auto right = at(first_row + row, second_col);
                if (left == kInfinity || right == kInfinity) {
                    return false;
                }
                differences.push_back(right - left);
            }
            for (std::size_t row = 1; row < differences.size(); ++row) {
                if (differences[row - 1] >= differences[row]) {
                    return false;
                }
            }
            bool has_valid_divide = false;
            for (std::size_t divide = 0; divide <= row_count; ++divide) {
                bool valid = true;
                for (std::size_t row = 0; row < divide; ++row) {
                    valid = valid && differences[row] <= 0;
                }
                for (std::size_t row = divide; row < row_count; ++row) {
                    valid = valid && differences[row] > 0;
                }
                has_valid_divide = has_valid_divide || valid;
            }
            if (!has_valid_divide) {
                return false;
            }
        }
    }
    return true;
}

bool ShiftMatrix::is_two_tone() const {
    if (is_systematic()) {
        return rows_ == cols_ || is_two_tone_rows(cols_, rows_ - cols_);
    }
    return is_two_tone_rows(0, rows_);
}

}
