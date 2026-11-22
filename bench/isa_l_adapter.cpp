#include "bench/adapter.h"

#include <algorithm>
#include <climits>
#include <chrono>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#if defined(SHIFT_XOR_HAVE_ISAL)
#include <erasure_code.h>
#endif

namespace shiftxor::bench {
namespace {

std::vector<std::size_t> default_survivors(const BenchmarkCase& config) {
    const auto parity = config.n - config.k;
    std::vector<std::size_t> survivors;
    if (parity <= config.k) {
        for (std::size_t index = 0; index < config.k - parity; ++index) {
            survivors.push_back(index);
        }
        for (std::size_t index = config.k; index < config.n; ++index) {
            survivors.push_back(index);
        }
    } else {
        for (std::size_t index = 0; index < config.k; ++index) {
            survivors.push_back(index);
        }
    }
    return survivors;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) / 2.0
                                  : values[middle];
}

#if defined(SHIFT_XOR_HAVE_ISAL)
class IsaLAdapter final : public CodecAdapter {
public:
    std::string name() const override { return "isa-l"; }
    bool available() const override { return true; }

    std::vector<BenchmarkRecord> run(const BenchmarkCase& config) const override {
        BenchmarkRecord encode_record = make_record(config, "encode");
        BenchmarkRecord decode_record = make_record(config, "decode");
        if (config.n <= config.k || config.n > 256 || config.input_length > INT_MAX ||
            config.input_length < 32) {
            encode_record.error = "ISA-L requires n > k, n <= 256, and a block length <= INT_MAX";
            decode_record.error = encode_record.error;
            return {encode_record, decode_record};
        }
        const auto m = config.n - config.k;
        std::vector<std::vector<unsigned char>> data(
            config.k, std::vector<unsigned char>(config.input_length));
        for (std::size_t block = 0; block < config.k; ++block) {
            for (std::size_t byte = 0; byte < config.input_length; ++byte) {
                data[block][byte] = static_cast<unsigned char>(block * 31 + byte);
            }
        }
        std::vector<unsigned char*> data_ptrs;
        for (auto& block : data) {
            data_ptrs.push_back(block.data());
        }
        std::vector<std::vector<unsigned char>> parity(
            m, std::vector<unsigned char>(config.input_length));
        std::vector<unsigned char*> parity_ptrs;
        for (auto& block : parity) {
            parity_ptrs.push_back(block.data());
        }
        std::vector<unsigned char> matrix(config.n * config.k);
        gf_gen_cauchy1_matrix(matrix.data(), static_cast<int>(config.n),
                              static_cast<int>(config.k));
        std::vector<unsigned char> encode_tables(32 * config.k * m);
        ec_init_tables(static_cast<int>(config.k), static_cast<int>(m),
                       matrix.data() + config.k * config.k, encode_tables.data());
        ec_encode_data(static_cast<int>(config.input_length), static_cast<int>(config.k),
                       static_cast<int>(m), encode_tables.data(), data_ptrs.data(),
                       parity_ptrs.data());
        const auto iterations = std::max<std::size_t>(1, config.iterations);
        std::vector<double> encode_samples;
        encode_samples.reserve(iterations);
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            ec_encode_data(static_cast<int>(config.input_length), static_cast<int>(config.k),
                           static_cast<int>(m), encode_tables.data(), data_ptrs.data(),
                           parity_ptrs.data());
            const auto end = std::chrono::steady_clock::now();
            encode_samples.push_back(std::chrono::duration<double>(end - start).count());
        }
        fill_timing(encode_record, median(encode_samples),
                    config.k * config.input_length, config.n * config.input_length);
        encode_record.validated = true;

        const auto survivors = config.survivors.empty() ? default_survivors(config)
                                                         : config.survivors;
        if (survivors.size() != config.k) {
            decode_record.error = "ISA-L decode requires exactly k survivor indices";
            return {encode_record, decode_record};
        }
        std::vector<bool> present(config.k, false);
        std::vector<std::size_t> missing;
        std::vector<unsigned char*> survivor_ptrs;
        std::vector<unsigned char> survivor_matrix(config.k * config.k);
        for (std::size_t row = 0; row < survivors.size(); ++row) {
            const auto index = survivors[row];
            if (index >= config.n) {
                decode_record.error = "survivor index is outside the codeword";
                return {encode_record, decode_record};
            }
            survivor_ptrs.push_back(index < config.k ? data[index].data()
                                                      : parity[index - config.k].data());
            std::memcpy(survivor_matrix.data() + row * config.k,
                        matrix.data() + index * config.k, config.k);
            if (index < config.k) {
                present[index] = true;
            }
        }
        for (std::size_t index = 0; index < config.k; ++index) {
            if (!present[index]) {
                missing.push_back(index);
            }
        }
        if (missing.empty()) {
            decode_record.available = true;
            decode_record.validated = true;
            decode_record.logical_bytes = 0;
            decode_record.physical_bytes = config.n * config.input_length;
            return {encode_record, decode_record};
        }
        std::vector<unsigned char> inverse(config.k * config.k);
        if (gf_invert_matrix(survivor_matrix.data(), inverse.data(),
                             static_cast<int>(config.k)) != 0) {
            decode_record.error = "ISA-L survivor matrix is singular";
            return {encode_record, decode_record};
        }
        std::vector<unsigned char> decode_matrix(missing.size() * config.k, 0);
        for (std::size_t missing_row = 0; missing_row < missing.size(); ++missing_row) {
            const auto source_row = missing[missing_row];
            for (std::size_t col = 0; col < config.k; ++col) {
                for (std::size_t row = 0; row < config.k; ++row) {
                    decode_matrix[missing_row * config.k + col] ^=
                        gf_mul(inverse[row * config.k + col],
                               matrix[source_row * config.k + row]);
                }
            }
        }
        std::vector<unsigned char> decode_tables(32 * config.k * missing.size());
        ec_init_tables(static_cast<int>(config.k), static_cast<int>(missing.size()),
                       decode_matrix.data(), decode_tables.data());
        std::vector<std::vector<unsigned char>> recovered(
            missing.size(), std::vector<unsigned char>(config.input_length));
        std::vector<unsigned char*> recovered_ptrs;
        for (auto& block : recovered) {
            recovered_ptrs.push_back(block.data());
        }
        std::vector<double> decode_samples;
        decode_samples.reserve(iterations);
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            ec_encode_data(static_cast<int>(config.input_length), static_cast<int>(config.k),
                           static_cast<int>(missing.size()), decode_tables.data(),
                           survivor_ptrs.data(), recovered_ptrs.data());
            const auto end = std::chrono::steady_clock::now();
            decode_samples.push_back(std::chrono::duration<double>(end - start).count());
        }
        fill_timing(decode_record, median(decode_samples),
                    missing.size() * config.input_length, config.n * config.input_length);
        decode_record.validated = true;
        for (std::size_t row = 0; row < missing.size(); ++row) {
            decode_record.validated = decode_record.validated &&
                recovered[row] == data[missing[row]];
        }
        return {encode_record, decode_record};
    }

private:
    static BenchmarkRecord make_record(const BenchmarkCase& config,
                                       const char* operation) {
        BenchmarkRecord record;
        record.codec = "isa-l";
        record.backend = "gf256";
        record.operation = operation;
        record.config = config;
        record.available = true;
        return record;
    }

    static void fill_timing(BenchmarkRecord& record, double elapsed,
                            std::size_t logical, std::size_t physical) {
        record.logical_bytes = logical;
        record.physical_bytes = physical;
        record.elapsed_seconds = std::max(elapsed, 1e-12);
        record.throughput_mib_per_second =
            static_cast<double>(logical) / (1024.0 * 1024.0) / record.elapsed_seconds;
    }
};
#else
class IsaLAdapter final : public CodecAdapter {
public:
    std::string name() const override { return "isa-l"; }
    bool available() const override { return false; }
    std::vector<BenchmarkRecord> run(const BenchmarkCase& config) const override {
        std::vector<BenchmarkRecord> records;
        for (const char* operation : {"encode", "decode"}) {
            BenchmarkRecord record;
            record.codec = name();
            record.backend = "external";
            record.operation = operation;
            record.config = config;
            record.error = "ISA-L was not found at configure time";
            records.push_back(record);
        }
        return records;
    }
};
#endif

}  // namespace

std::unique_ptr<CodecAdapter> make_isa_l_adapter() {
    return std::make_unique<IsaLAdapter>();
}

}
