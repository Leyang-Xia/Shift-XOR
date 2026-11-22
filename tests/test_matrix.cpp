#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#include "shiftxor/backend.h"
#include "shiftxor/buffer.h"
#include "shiftxor/matrix.h"
#include "shiftxor/status.h"

int main() {
    shiftxor::BufferView output{};
    shiftxor::ConstBufferView input{};
    shiftxor::IndexedConstBufferView indexed{2, input};
    assert(output.data == nullptr);
    assert(indexed.index == 2);

    const auto matrix = shiftxor::ShiftMatrix::create(
        3, 2, std::vector<std::int32_t>{0, -1, 1, 2, 3, 0});
    assert(matrix);
    assert(matrix.value().rows() == 3);
    assert(matrix.value().cols() == 2);
    assert(matrix.value().at(0, 1) == shiftxor::ShiftMatrix::kInfinity);
    assert(matrix.value().row_max_shift(2) == 3);
    assert(matrix.value().row_length(0, 16, 2) == 16);
    assert(matrix.value().row_length(1, 16, 2) == 20);
    assert(matrix.value().row_length(2, 16, 2) == 22);
    assert(matrix.value().storage_overhead(2) == 10);

    const auto empty_row = shiftxor::ShiftMatrix::create(
        1, 2, std::vector<std::int32_t>{shiftxor::ShiftMatrix::kInfinity,
                                        shiftxor::ShiftMatrix::kInfinity});
    assert(empty_row);
    assert(empty_row.value().row_max_shift(0) == shiftxor::ShiftMatrix::kInfinity);
    assert(empty_row.value().row_length(0, 16, 2) == 0);

    const auto code = shiftxor::ShiftMatrix::make_systematic_two_tone(9, 6);
    assert(code);
    assert(code.value().rows() == 9);
    assert(code.value().cols() == 6);
    assert(code.value().is_systematic());
    assert(code.value().is_two_tone());
    assert(code.value().at(0, 0) == 0);
    assert(code.value().at(0, 1) == shiftxor::ShiftMatrix::kInfinity);
    assert(code.value().at(6, 0) == 5);
    assert(code.value().at(6, 5) == 0);
    assert(code.value().at(7, 0) == 0);
    assert(code.value().at(7, 5) == 0);
    assert(code.value().at(8, 0) == 0);
    assert(code.value().at(8, 5) == 5);

    const auto single_data_code = shiftxor::ShiftMatrix::make_systematic_two_tone(3, 1);
    assert(single_data_code);
    assert(single_data_code.value().is_two_tone());

    const auto invalid_code = shiftxor::ShiftMatrix::make_systematic_two_tone(5, 6);
    assert(!invalid_code);
    assert(invalid_code.status().code() == shiftxor::StatusCode::kInvalidMatrix);

    const auto overflow_matrix = shiftxor::ShiftMatrix::create(
        std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max(), {});
    assert(!overflow_matrix);

    assert(shiftxor::Status{}.code() == shiftxor::StatusCode::kOk);
    assert(shiftxor::BackendKind::kScalar != shiftxor::BackendKind::kAvx2);
    return 0;
}
