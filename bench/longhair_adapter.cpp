#include "bench/adapter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

#if defined(SHIFT_XOR_HAVE_LONGHAIR)
#include <cauchy_256.h>
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

#if defined(SHIFT_XOR_HAVE_LONGHAIR)
class LonghairAdapter final : public CodecAdapter {
public:
    std::string name() const override { return "longhair"; }
    bool available() const override { return initialized_; }

    LonghairAdapter() : initialized_(cauchy_256_init() == 0) {}

    std::vector<BenchmarkRecord> run(const BenchmarkCase& config) const override {
        BenchmarkRecord encode_record = make_record(config, "encode");
        BenchmarkRecord decode_record = make_record(config, "decode");
        if (!initialized_) {
            encode_record.error = "Longhair API version check failed";
            decode_record.error = encode_record.error;
            return {encode_record, decode_record};
        }
        if (config.k == 0 || config.n <= config.k || config.n > 256 ||
            config.input_length % 8 != 0) {
            encode_record.error = "Longhair requires 0 < k < n <= 256 and bytes divisible by 8";
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
        std::vector<const unsigned char*> data_ptrs;
        for (const auto& block : data) {
            data_ptrs.push_back(block.data());
        }
        std::vector<unsigned char> recovery(m * config.input_length);
        if (cauchy_256_encode(static_cast<int>(config.k), static_cast<int>(m),
                              data_ptrs.data(), recovery.data(),
                              static_cast<int>(config.input_length)) != 0) {
            encode_record.error = "Longhair encode rejected the benchmark parameters";
            decode_record.error = encode_record.error;
            return {encode_record, decode_record};
        }
        const auto iterations = std::max<std::size_t>(1, config.iterations);
        std::vector<double> encode_samples;
        encode_samples.reserve(iterations);
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            const auto start = std::chrono::steady_clock::now();
            cauchy_256_encode(static_cast<int>(config.k), static_cast<int>(m),
                              data_ptrs.data(), recovery.data(),
                              static_cast<int>(config.input_length));
            const auto end = std::chrono::steady_clock::now();
            encode_samples.push_back(std::chrono::duration<double>(end - start).count());
        }
        fill_timing(encode_record, median(encode_samples),
                    config.k * config.input_length, config.n * config.input_length);
        encode_record.validated = true;

        const auto survivors = config.survivors.empty() ? default_survivors(config)
                                                         : config.survivors;
        if (survivors.size() != config.k) {
            decode_record.error = "Longhair decode requires exactly k survivor indices";
            return {encode_record, decode_record};
        }
        std::vector<std::vector<unsigned char>> received(
            config.k, std::vector<unsigned char>(config.input_length));
        std::vector<Block> blocks(config.k);
        for (std::size_t position = 0; position < survivors.size(); ++position) {
            const auto index = survivors[position];
            if (index >= config.n) {
                decode_record.error = "survivor index is outside the codeword";
                return {encode_record, decode_record};
            }
            if (index < config.k) {
                std::memcpy(received[position].data(), data[index].data(), config.input_length);
            } else {
                std::memcpy(received[position].data(),
                            recovery.data() + (index - config.k) * config.input_length,
                            config.input_length);
            }
            blocks[position].data = received[position].data();
            blocks[position].row = static_cast<unsigned char>(index);
        }
        std::vector<std::size_t> missing;
        std::vector<bool> present(config.k, false);
        for (const auto index : survivors) {
            if (index < config.k) {
                present[index] = true;
            }
        }
        for (std::size_t index = 0; index < config.k; ++index) {
            if (!present[index]) {
                missing.push_back(index);
            }
        }
        std::vector<double> decode_samples;
        decode_samples.reserve(iterations);
        int decode_status = 0;
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            for (std::size_t position = 0; position < survivors.size(); ++position) {
                const auto index = survivors[position];
                if (index < config.k) {
                    std::memcpy(received[position].data(), data[index].data(),
                                config.input_length);
                } else {
                    std::memcpy(received[position].data(),
                                recovery.data() + (index - config.k) * config.input_length,
                                config.input_length);
                }
            }
            const auto start = std::chrono::steady_clock::now();
            decode_status = cauchy_256_decode(
                static_cast<int>(config.k), static_cast<int>(m), blocks.data(),
                static_cast<int>(config.input_length));
            const auto end = std::chrono::steady_clock::now();
            decode_samples.push_back(std::chrono::duration<double>(end - start).count());
        }
        fill_timing(decode_record, median(decode_samples),
                    missing.size() * config.input_length, config.n * config.input_length);
        decode_record.validated = decode_status == 0;
        for (const auto& block : blocks) {
            if (block.row < config.k) {
                decode_record.validated = decode_record.validated &&
                    std::memcmp(block.data, data[block.row].data(), config.input_length) == 0;
            }
        }
        if (decode_status != 0) {
            decode_record.error = "Longhair decode rejected the survivor set";
        }
        return {encode_record, decode_record};
    }

private:
    static BenchmarkRecord make_record(const BenchmarkCase& config,
                                       const char* operation) {
        BenchmarkRecord record;
        record.codec = "longhair";
        record.backend = "cauchy256";
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

    bool initialized_ = false;
};
#else
class LonghairAdapter final : public CodecAdapter {
public:
    std::string name() const override { return "longhair"; }
    bool available() const override { return false; }
    std::vector<BenchmarkRecord> run(const BenchmarkCase& config) const override {
        std::vector<BenchmarkRecord> records;
        for (const char* operation : {"encode", "decode"}) {
            BenchmarkRecord record;
            record.codec = name();
            record.backend = "external";
            record.operation = operation;
            record.config = config;
            record.error = "Longhair was not found at configure time";
            records.push_back(record);
        }
        return records;
    }
};
#endif

}  // namespace

std::unique_ptr<CodecAdapter> make_longhair_adapter() {
    return std::make_unique<LonghairAdapter>();
}

}

