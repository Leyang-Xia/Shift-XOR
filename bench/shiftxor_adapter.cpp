#include "bench/adapter.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <utility>

#include "shiftxor/codec.h"
#include "shiftxor/matrix.h"

namespace shiftxor::bench {
namespace {

std::string backend_name(BackendKind backend) {
    return backend == BackendKind::kAvx2 ? "avx2" : "scalar";
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    if (values.size() % 2 == 0) {
        return (values[middle - 1] + values[middle]) / 2.0;
    }
    return values[middle];
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        if (character == '\n') {
            escaped += "\\n";
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

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

class ShiftXorAdapter final : public CodecAdapter {
public:
    explicit ShiftXorAdapter(BackendKind backend) : backend_(backend) {}

    std::string name() const override { return "shift-xor"; }
    bool available() const override {
        return backend_ == BackendKind::kScalar || avx2_available();
    }

    std::vector<BenchmarkRecord> run(const BenchmarkCase& config) const override {
        BenchmarkRecord encode_record;
        BenchmarkRecord decode_record;
        initialize_record(encode_record, config, "encode");
        initialize_record(decode_record, config, "decode");
        if (!available()) {
            encode_record.error = "requested backend is not available";
            decode_record.error = encode_record.error;
            return {encode_record, decode_record};
        }

        const auto matrix_result = ShiftMatrix::make_systematic_two_tone(config.n, config.k);
        if (!matrix_result) {
            encode_record.error = matrix_result.status().message();
            decode_record.error = encode_record.error;
            return {encode_record, decode_record};
        }
        const auto codec_result = ShiftXorCodec::create(
            matrix_result.value(),
            {config.input_length, config.slice_bytes, backend_});
        if (!codec_result) {
            encode_record.error = codec_result.status().message();
            decode_record.error = encode_record.error;
            return {encode_record, decode_record};
        }
        const auto& codec = codec_result.value();

        std::vector<std::vector<std::uint8_t>> originals;
        originals.reserve(config.k);
        std::vector<ConstBufferView> data;
        for (std::size_t block = 0; block < config.k; ++block) {
            originals.emplace_back(config.input_length);
            for (std::size_t byte = 0; byte < config.input_length; ++byte) {
                originals.back()[byte] = static_cast<std::uint8_t>(block * 31 + byte);
            }
        }
        for (const auto& block : originals) {
            data.push_back({block.data(), block.size()});
        }

        std::vector<std::vector<std::uint8_t>> code_storage;
        code_storage.reserve(config.n);
        for (const auto row_length : codec.row_lengths()) {
            code_storage.emplace_back(row_length);
        }
        std::vector<BufferView> codeword;
        codeword.reserve(config.n);
        for (auto& block : code_storage) {
            codeword.push_back({block.data(), block.size()});
        }

        codec.encode(data, codeword);
        const auto iterations = std::max<std::size_t>(1, config.iterations);
        codec.encode(data, codeword);
        std::vector<double> encode_samples;
        encode_samples.reserve(iterations);
        Status encode_status;
        for (std::size_t sample = 0; sample < iterations; ++sample) {
            const auto start = std::chrono::steady_clock::now();
            encode_status = codec.encode(data, codeword);
            const auto end = std::chrono::steady_clock::now();
            encode_samples.push_back(
                std::chrono::duration<double>(end - start).count());
        }
        fill_timing(encode_record, median(encode_samples),
                    config.k * config.input_length,
                    std::accumulate(codec.row_lengths().begin(), codec.row_lengths().end(),
                                    std::size_t{0}));
        encode_record.validated = static_cast<bool>(encode_status);
        if (!encode_status) {
            encode_record.error = encode_status.message();
        } else {
            for (std::size_t block = 0; block < config.k; ++block) {
                encode_record.validated = encode_record.validated &&
                    std::memcmp(code_storage[block].data(), originals[block].data(),
                                config.input_length) == 0;
            }
        }

        const auto survivors = config.survivors.empty() ? default_survivors(config)
                                                         : config.survivors;
        std::vector<IndexedConstBufferView> available_blocks;
        for (const auto index : survivors) {
            if (index < code_storage.size()) {
                available_blocks.push_back(
                    {index, {code_storage[index].data(), code_storage[index].size()}});
            }
        }
        std::vector<std::vector<std::uint8_t>> decoded_storage(
            config.k, std::vector<std::uint8_t>(config.input_length));
        std::vector<BufferView> decoded;
        for (auto& block : decoded_storage) {
            decoded.push_back({block.data(), block.size()});
        }
        const auto support_status = codec.supports(survivors);
        if (!support_status) {
            decode_record.error = support_status.message();
            return {encode_record, decode_record};
        }
        std::vector<double> decode_samples;
        decode_samples.reserve(iterations);
        Status decode_status;
        for (std::size_t sample = 0; sample < iterations; ++sample) {
            const auto start = std::chrono::steady_clock::now();
            decode_status = codec.decode(available_blocks, decoded);
            const auto end = std::chrono::steady_clock::now();
            decode_samples.push_back(
                std::chrono::duration<double>(end - start).count());
        }
        std::size_t missing_blocks = 0;
        for (std::size_t block = 0; block < config.k; ++block) {
            if (std::find(survivors.begin(), survivors.end(), block) == survivors.end()) {
                ++missing_blocks;
            }
        }
        fill_timing(decode_record, median(decode_samples),
                    missing_blocks * config.input_length,
                    std::accumulate(codec.row_lengths().begin(), codec.row_lengths().end(),
                                    std::size_t{0}));
        decode_record.validated = static_cast<bool>(decode_status);
        if (!decode_status) {
            decode_record.error = decode_status.message();
        } else {
            for (std::size_t block = 0; block < config.k; ++block) {
                decode_record.validated = decode_record.validated &&
                    decoded_storage[block] == originals[block];
            }
        }
        return {encode_record, decode_record};
    }

private:
    void initialize_record(BenchmarkRecord& record, const BenchmarkCase& config,
                           const char* operation) const {
        record.codec = name();
        record.backend = backend_name(backend_);
        record.operation = operation;
        record.config = config;
        record.available = available();
    }

    static void fill_timing(BenchmarkRecord& record, double elapsed,
                            std::size_t logical_bytes,
                            std::size_t physical_bytes) {
        record.logical_bytes = logical_bytes;
        record.physical_bytes = physical_bytes;
        record.elapsed_seconds = std::max(elapsed, 1e-12);
        record.throughput_mib_per_second =
            static_cast<double>(logical_bytes) / (1024.0 * 1024.0) /
            record.elapsed_seconds;
    }

    BackendKind backend_;
};

}  // namespace

std::unique_ptr<CodecAdapter> make_shiftxor_adapter(BackendKind backend) {
    return std::make_unique<ShiftXorAdapter>(backend);
}

std::string record_to_json(const BenchmarkRecord& record) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(6);
    output << "{\"codec\":\"" << json_escape(record.codec)
           << "\",\"backend\":\"" << json_escape(record.backend)
           << "\",\"operation\":\"" << json_escape(record.operation)
           << "\",\"n\":" << record.config.n
           << ",\"k\":" << record.config.k
           << ",\"input_length\":" << record.config.input_length
           << ",\"slice_bytes\":" << record.config.slice_bytes
           << ",\"iterations\":" << record.config.iterations
           << ",\"logical_bytes\":" << record.logical_bytes
           << ",\"physical_bytes\":" << record.physical_bytes
           << ",\"elapsed_seconds\":" << record.elapsed_seconds
           << ",\"throughput_mib_per_second\":"
           << record.throughput_mib_per_second
           << ",\"available\":" << (record.available ? "true" : "false")
           << ",\"validated\":" << (record.validated ? "true" : "false")
           << ",\"error\":\"" << json_escape(record.error) << "\"}";
    return output.str();
}

}
