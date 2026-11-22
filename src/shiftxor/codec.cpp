#include "shiftxor/codec.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <numeric>
#include <limits>
#include <utility>

namespace shiftxor {
namespace {

bool contains_duplicate(const std::vector<std::size_t>& values) {
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] == values[i - 1]) {
            return true;
        }
    }
    return false;
}

struct DecodeSchedule {
    std::vector<std::size_t> row_order;
    std::vector<std::size_t> topological_order;
};

Result<DecodeSchedule> build_decode_schedule(
    const ShiftMatrix& matrix, const std::vector<std::size_t>& rows,
    const std::vector<std::size_t>& columns, std::size_t slice_count) {
    if (rows.size() != columns.size() || rows.empty()) {
        return Status::error(StatusCode::kUnsupported,
                             "parity system is not square");
    }
    DecodeSchedule schedule;
    schedule.row_order = rows;
    if (columns.size() > 1) {
        const auto slope = [&](const std::size_t row) {
            return matrix.at(row, columns.back()) - matrix.at(row, columns.front());
        };
        std::sort(schedule.row_order.begin(), schedule.row_order.end(),
                  [&](const auto left, const auto right) {
                      return slope(left) < slope(right);
                  });
        for (std::size_t row = 1; row < schedule.row_order.size(); ++row) {
            if (slope(schedule.row_order[row - 1]) >= slope(schedule.row_order[row])) {
                return Status::error(StatusCode::kUnsupported,
                                     "parity rows do not have strict two-tone slopes");
            }
        }
        for (std::size_t first = 0; first < columns.size(); ++first) {
            for (std::size_t second = first + 1; second < columns.size(); ++second) {
                std::vector<std::int32_t> differences;
                for (const auto row : schedule.row_order) {
                    const auto left = matrix.at(row, columns[first]);
                    const auto right = matrix.at(row, columns[second]);
                    if (left == ShiftMatrix::kInfinity || right == ShiftMatrix::kInfinity) {
                        return Status::error(StatusCode::kUnsupported,
                                             "two-tone matrix contains an omitted term");
                    }
                    differences.push_back(right - left);
                }
                bool has_divide = false;
                for (std::size_t divide = 0; divide <= differences.size(); ++divide) {
                    bool valid = true;
                    for (std::size_t row = 0; row < divide; ++row) {
                        valid = valid && differences[row] <= 0;
                    }
                    for (std::size_t row = divide; row < differences.size(); ++row) {
                        valid = valid && differences[row] > 0;
                    }
                    has_divide = has_divide || valid;
                }
                if (!has_divide) {
                    return Status::error(StatusCode::kUnsupported,
                                         "two-tone matrix has no valid divide");
                }
            }
        }
    }

    const auto sequence_count = columns.size();
    if (slice_count > std::numeric_limits<std::size_t>::max() / sequence_count) {
        return Status::error(StatusCode::kInvalidArgument,
                             "decode schedule is too large");
    }
    const auto node_count = sequence_count * slice_count;
    std::vector<std::vector<std::size_t>> dependents(node_count);
    std::vector<std::size_t> indegree(node_count, 0);
    for (std::size_t variable = 0; variable < sequence_count; ++variable) {
        const auto row = schedule.row_order[sequence_count - 1 - variable];
        const auto pivot_shift = matrix.at(row, columns[variable]);
        if (pivot_shift == ShiftMatrix::kInfinity) {
            return Status::error(StatusCode::kUnsupported,
                                 "two-tone pivot is omitted");
        }
        for (std::size_t slice = 0; slice < slice_count; ++slice) {
            const auto node = variable * slice_count + slice;
            for (std::size_t other = 0; other < sequence_count; ++other) {
                if (other == variable) {
                    continue;
                }
                const auto shift = matrix.at(row, columns[other]);
                if (shift == ShiftMatrix::kInfinity) {
                    return Status::error(StatusCode::kUnsupported,
                                         "two-tone coefficient is omitted");
                }
                const auto dependency = static_cast<std::int64_t>(slice) +
                                        pivot_shift - shift;
                if (dependency < 0 || dependency >=
                    static_cast<std::int64_t>(slice_count)) {
                    continue;
                }
                const auto source = other * slice_count +
                                    static_cast<std::size_t>(dependency);
                dependents[source].push_back(node);
                ++indegree[node];
            }
        }
    }

    std::deque<std::size_t> ready;
    for (std::size_t node = 0; node < node_count; ++node) {
        if (indegree[node] == 0) {
            ready.push_back(node);
        }
    }
    schedule.topological_order.reserve(node_count);
    while (!ready.empty()) {
        const auto node = ready.front();
        ready.pop_front();
        schedule.topological_order.push_back(node);
        for (const auto dependent : dependents[node]) {
            if (--indegree[dependent] == 0) {
                ready.push_back(dependent);
            }
        }
    }
    if (schedule.topological_order.size() != node_count) {
        return Status::error(StatusCode::kUnsupported,
                             "two-tone dependency graph contains a cycle");
    }
    return schedule;
}

void xor_slice(std::vector<std::uint8_t>& destination,
               const std::vector<std::uint8_t>& source,
               std::size_t destination_offset, std::size_t source_offset,
               std::size_t bytes) {
    for (std::size_t index = 0; index < bytes; ++index) {
        destination[destination_offset + index] ^= source[source_offset + index];
    }
}

}  // namespace

Result<ShiftXorCodec> ShiftXorCodec::create(ShiftMatrix matrix,
                                             CodecConfig config) {
    if (config.input_length == 0 || config.slice_bytes == 0 ||
        config.input_length % config.slice_bytes != 0) {
        return Status::error(StatusCode::kInvalidArgument,
                             "input length must be divisible by a positive slice width");
    }

    std::vector<std::size_t> lengths;
    lengths.reserve(matrix.rows());
    try {
        for (std::size_t row = 0; row < matrix.rows(); ++row) {
            lengths.push_back(matrix.row_length(row, config.input_length,
                                                config.slice_bytes));
        }
    } catch (const std::exception& error) {
        return Status::error(StatusCode::kInvalidArgument, error.what());
    }
    return ShiftXorCodec(std::move(matrix), config, std::move(lengths));
}

Status ShiftXorCodec::xor_shifted(ConstBufferView source, std::size_t source_offset,
                                  BufferView destination,
                                  std::size_t destination_offset,
                                  std::size_t bytes) const {
    if (config_.backend == BackendKind::kAvx2 && avx2_available()) {
        return xor_shifted_avx2(source, source_offset, destination,
                                destination_offset, bytes);
    }
    return xor_shifted_scalar(source, source_offset, destination,
                              destination_offset, bytes);
}

Status ShiftXorCodec::encode(const std::vector<ConstBufferView>& data,
                             const std::vector<BufferView>& codeword) const {
    if (data.size() != matrix_.cols() || codeword.size() != matrix_.rows()) {
        return Status::error(StatusCode::kInvalidArgument,
                             "source and codeword counts do not match matrix dimensions");
    }
    for (const auto& source : data) {
        if (source.data == nullptr || source.size != config_.input_length) {
            return Status::error(StatusCode::kBufferTooSmall,
                                 "source buffer has the wrong length");
        }
    }
    for (std::size_t row = 0; row < codeword.size(); ++row) {
        if (codeword[row].size < row_lengths_[row] ||
            (row_lengths_[row] > 0 && codeword[row].data == nullptr)) {
            return Status::error(StatusCode::kBufferTooSmall,
                                 "codeword buffer has the wrong length");
        }
    }

    for (std::size_t row = 0; row < codeword.size(); ++row) {
        if (row_lengths_[row] > 0) {
            std::memset(codeword[row].data, 0, row_lengths_[row]);
        }
        for (std::size_t col = 0; col < data.size(); ++col) {
            const auto shift = matrix_.at(row, col);
            if (shift == ShiftMatrix::kInfinity) {
                continue;
            }
            const auto shift_bytes = static_cast<std::size_t>(shift) * config_.slice_bytes;
            for (std::size_t offset = 0; offset < config_.input_length;
                 offset += config_.slice_bytes) {
                const auto status = xor_shifted(
                    data[col], offset, codeword[row], offset + shift_bytes,
                    config_.slice_bytes);
                if (!status) {
                    return status;
                }
            }
        }
    }
    return Status::ok();
}

Status ShiftXorCodec::validate_survivors(
    const std::vector<std::size_t>& survivors) const {
    if (survivors.size() != matrix_.cols()) {
        return Status::error(StatusCode::kInsufficientBlocks,
                             "decode requires exactly k survivor rows");
    }
    std::vector<std::size_t> sorted = survivors;
    std::sort(sorted.begin(), sorted.end());
    if (contains_duplicate(sorted)) {
        return Status::error(StatusCode::kInvalidArgument,
                             "survivor rows must be unique");
    }
    for (const auto index : sorted) {
        if (index >= matrix_.rows() || row_lengths_[index] == 0) {
            return Status::error(StatusCode::kUnsupported,
                                 "survivor row is outside the decodable region");
        }
    }

    if (!matrix_.is_systematic()) {
        std::vector<std::size_t> columns(matrix_.cols());
        std::iota(columns.begin(), columns.end(), 0);
        const auto schedule = build_decode_schedule(
            matrix_, sorted, columns,
            config_.input_length / config_.slice_bytes);
        return schedule ? Status::ok() : schedule.status();
    }

    std::vector<std::size_t> parity_rows;
    std::vector<bool> present(matrix_.cols(), false);
    for (const auto index : sorted) {
        if (index < matrix_.cols()) {
            present[index] = true;
        } else {
            parity_rows.push_back(index);
        }
    }
    std::vector<std::size_t> missing;
    for (std::size_t col = 0; col < matrix_.cols(); ++col) {
        if (!present[col]) {
            missing.push_back(col);
        }
    }
    if (parity_rows.size() != missing.size()) {
        return Status::error(StatusCode::kUnsupported,
                             "survivors do not define a square parity system");
    }
    if (missing.empty()) {
        return Status::ok();
    }

    if (missing.size() > 1 && parity_rows.size() > 1) {
        for (std::size_t first_col = 0; first_col < missing.size(); ++first_col) {
            for (std::size_t second_col = first_col + 1; second_col < missing.size(); ++second_col) {
                bool increasing = true;
                bool decreasing = true;
                auto previous = matrix_.at(parity_rows.front(), missing[second_col]) -
                                matrix_.at(parity_rows.front(), missing[first_col]);
                for (std::size_t row = 1; row < parity_rows.size(); ++row) {
                    const auto current =
                        matrix_.at(parity_rows[row], missing[second_col]) -
                        matrix_.at(parity_rows[row], missing[first_col]);
                    increasing = increasing && current > previous;
                    decreasing = decreasing && current < previous;
                    previous = current;
                }
                if (!increasing && !decreasing) {
                    return Status::error(StatusCode::kUnsupported,
                                         "parity survivor set is not Two-Tone");
                }
            }
        }
    }

    const auto schedule = build_decode_schedule(
        matrix_, parity_rows, missing,
        config_.input_length / config_.slice_bytes);
    if (!schedule) {
        return schedule.status();
    }
    return Status::ok();
}

Status ShiftXorCodec::supports(
    const std::vector<std::size_t>& survivor_indices) const {
    return validate_survivors(survivor_indices);
}

Status ShiftXorCodec::decode(
    const std::vector<IndexedConstBufferView>& available,
    const std::vector<BufferView>& data_out) const {
    if (data_out.size() != matrix_.cols()) {
        return Status::error(StatusCode::kInvalidArgument,
                             "data output count does not match k");
    }
    std::vector<std::size_t> survivors;
    survivors.reserve(available.size());
    for (const auto& block : available) {
        survivors.push_back(block.index);
    }
    const auto support_status = validate_survivors(survivors);
    if (!support_status) {
        return support_status;
    }
    for (const auto& output : data_out) {
        if (output.data == nullptr || output.size < config_.input_length) {
            return Status::error(StatusCode::kBufferTooSmall,
                                 "data output buffer has the wrong length");
        }
    }

    std::vector<std::size_t> sorted_positions(available.size());
    std::iota(sorted_positions.begin(), sorted_positions.end(), 0);
    std::sort(sorted_positions.begin(), sorted_positions.end(),
              [&](const auto left, const auto right) {
                  return available[left].index < available[right].index;
              });
    for (const auto position : sorted_positions) {
        const auto& block = available[position];
        if (block.buffer.data == nullptr || block.buffer.size < row_lengths_[block.index]) {
            return Status::error(StatusCode::kBufferTooSmall,
                                 "survivor buffer has the wrong length");
        }
    }

    std::vector<const IndexedConstBufferView*> by_index(matrix_.rows(), nullptr);
    for (const auto position : sorted_positions) {
        by_index[available[position].index] = &available[position];
    }

    if (!matrix_.is_systematic()) {
        std::vector<std::size_t> selected_rows;
        selected_rows.reserve(sorted_positions.size());
        std::vector<std::vector<std::uint8_t>> rows_data;
        rows_data.reserve(sorted_positions.size());
        for (const auto position : sorted_positions) {
            const auto row = available[position].index;
            selected_rows.push_back(row);
            rows_data.emplace_back(row_lengths_[row], 0);
            std::memcpy(rows_data.back().data(), by_index[row]->buffer.data,
                        row_lengths_[row]);
        }
        std::vector<std::size_t> columns(matrix_.cols());
        std::iota(columns.begin(), columns.end(), 0);
        const auto schedule = build_decode_schedule(
            matrix_, selected_rows, columns,
            config_.input_length / config_.slice_bytes);
        if (!schedule) {
            return schedule.status();
        }
        std::vector<std::vector<std::uint8_t>> hats(
            columns.size(), std::vector<std::uint8_t>(config_.input_length));
        for (std::size_t variable = 0; variable < columns.size(); ++variable) {
            const auto row = schedule.value().row_order[columns.size() - 1 - variable];
            const auto row_index = static_cast<std::size_t>(
                std::find(selected_rows.begin(), selected_rows.end(), row) -
                selected_rows.begin());
            const auto shift = matrix_.at(row, columns[variable]);
            for (std::size_t slice = 0;
                 slice < config_.input_length / config_.slice_bytes; ++slice) {
                const auto source_offset =
                    (slice + static_cast<std::size_t>(shift)) * config_.slice_bytes;
                if (source_offset + config_.slice_bytes > rows_data[row_index].size()) {
                    return Status::error(StatusCode::kUnsupported,
                                         "two-tone subsequence falls outside the row buffer");
                }
                std::memcpy(hats[variable].data() + slice * config_.slice_bytes,
                            rows_data[row_index].data() + source_offset,
                            config_.slice_bytes);
            }
        }
        std::vector<std::vector<std::uint8_t>> decoded(
            columns.size(), std::vector<std::uint8_t>(config_.input_length));
        const auto slice_count = config_.input_length / config_.slice_bytes;
        for (const auto node : schedule.value().topological_order) {
            const auto variable = node / slice_count;
            const auto slice = node % slice_count;
            std::memcpy(decoded[variable].data() + slice * config_.slice_bytes,
                        hats[variable].data() + slice * config_.slice_bytes,
                        config_.slice_bytes);
            const auto row = schedule.value().row_order[columns.size() - 1 - variable];
            const auto pivot_shift = matrix_.at(row, columns[variable]);
            for (std::size_t other = 0; other < columns.size(); ++other) {
                if (other == variable) {
                    continue;
                }
                const auto shift = matrix_.at(row, columns[other]);
                const auto dependency = static_cast<std::int64_t>(slice) +
                                        pivot_shift - shift;
                if (dependency < 0 || dependency >= static_cast<std::int64_t>(slice_count)) {
                    continue;
                }
                xor_slice(decoded[variable], decoded[other],
                          slice * config_.slice_bytes,
                          static_cast<std::size_t>(dependency) * config_.slice_bytes,
                          config_.slice_bytes);
            }
        }
        for (std::size_t variable = 0; variable < columns.size(); ++variable) {
            std::memcpy(data_out[variable].data, decoded[variable].data(),
                        config_.input_length);
        }
        return Status::ok();
    }

    std::vector<bool> present(matrix_.cols(), false);
    for (std::size_t index = 0; index < matrix_.cols(); ++index) {
        if (by_index[index] != nullptr) {
            present[index] = true;
            std::memcpy(data_out[index].data, by_index[index]->buffer.data,
                        config_.input_length);
        }
    }
    std::vector<std::size_t> missing;
    for (std::size_t index = 0; index < matrix_.cols(); ++index) {
        if (!present[index]) {
            missing.push_back(index);
        }
    }
    if (missing.empty()) {
        return Status::ok();
    }

    std::vector<std::size_t> parity_rows;
    for (const auto index : sorted_positions) {
        if (available[index].index >= matrix_.cols()) {
            parity_rows.push_back(available[index].index);
        }
    }
    std::vector<std::vector<std::uint8_t>> residual;
    residual.reserve(parity_rows.size());
    for (const auto row : parity_rows) {
        residual.emplace_back(row_lengths_[row], 0);
    }
    for (std::size_t residual_index = 0; residual_index < parity_rows.size();
         ++residual_index) {
        const auto row = parity_rows[residual_index];
        std::memcpy(residual[residual_index].data(),
                    by_index[row]->buffer.data, row_lengths_[row]);
        for (std::size_t source_index = 0; source_index < matrix_.cols();
             ++source_index) {
            if (!present[source_index]) {
                continue;
            }
            const auto shift = matrix_.at(row, source_index);
            if (shift == ShiftMatrix::kInfinity) {
                continue;
            }
            const auto shift_bytes = static_cast<std::size_t>(shift) * config_.slice_bytes;
            for (std::size_t offset = 0; offset < config_.input_length;
                 offset += config_.slice_bytes) {
                const auto status = xor_shifted(
                    {data_out[source_index].data, config_.input_length}, offset,
                    {residual[residual_index].data(), residual[residual_index].size()},
                    offset + shift_bytes, config_.slice_bytes);
                if (!status) {
                    return status;
                }
            }
        }
    }

    const auto schedule = build_decode_schedule(
        matrix_, parity_rows, missing,
        config_.input_length / config_.slice_bytes);
    if (!schedule) {
        return schedule.status();
    }
    const auto slice_count = config_.input_length / config_.slice_bytes;
    std::vector<std::vector<std::uint8_t>> hats(
        missing.size(), std::vector<std::uint8_t>(config_.input_length));
    for (std::size_t variable = 0; variable < missing.size(); ++variable) {
        const auto row = schedule.value().row_order[missing.size() - 1 - variable];
        const auto residual_index = static_cast<std::size_t>(
            std::find(parity_rows.begin(), parity_rows.end(), row) - parity_rows.begin());
        const auto shift = matrix_.at(row, missing[variable]);
        for (std::size_t slice = 0; slice < slice_count; ++slice) {
            const auto source_offset =
                (slice + static_cast<std::size_t>(shift)) * config_.slice_bytes;
            if (source_offset + config_.slice_bytes > residual[residual_index].size()) {
                return Status::error(StatusCode::kUnsupported,
                                     "two-tone subsequence falls outside the row buffer");
            }
            std::memcpy(hats[variable].data() + slice * config_.slice_bytes,
                        residual[residual_index].data() + source_offset,
                        config_.slice_bytes);
        }
    }

    std::vector<std::vector<std::uint8_t>> decoded_missing(
        missing.size(), std::vector<std::uint8_t>(config_.input_length));
    for (const auto node : schedule.value().topological_order) {
        const auto variable = node / slice_count;
        const auto slice = node % slice_count;
        std::memcpy(decoded_missing[variable].data() + slice * config_.slice_bytes,
                    hats[variable].data() + slice * config_.slice_bytes,
                    config_.slice_bytes);
        const auto row = schedule.value().row_order[missing.size() - 1 - variable];
        const auto pivot_shift = matrix_.at(row, missing[variable]);
        for (std::size_t other = 0; other < missing.size(); ++other) {
            if (other == variable) {
                continue;
            }
            const auto other_shift = matrix_.at(row, missing[other]);
            const auto dependency = static_cast<std::int64_t>(slice) +
                                    pivot_shift - other_shift;
            if (dependency < 0 || dependency >= static_cast<std::int64_t>(slice_count)) {
                continue;
            }
            xor_slice(decoded_missing[variable], decoded_missing[other],
                      slice * config_.slice_bytes,
                      static_cast<std::size_t>(dependency) * config_.slice_bytes,
                      config_.slice_bytes);
        }
    }
    for (std::size_t variable = 0; variable < missing.size(); ++variable) {
        std::memcpy(data_out[missing[variable]].data,
                    decoded_missing[variable].data(), config_.input_length);
    }
    return Status::ok();
}

}

