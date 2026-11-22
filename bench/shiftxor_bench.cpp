#include "bench/adapter.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {

using shiftxor::BackendKind;
using shiftxor::bench::BenchmarkCase;
using shiftxor::bench::CodecAdapter;

std::size_t parse_size(const char* option, const std::string& value) {
    try {
        return static_cast<std::size_t>(std::stoull(value));
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid value for ") + option);
    }
}

std::vector<std::size_t> parse_indices(const std::string& value) {
    std::vector<std::size_t> indices;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) {
            throw std::invalid_argument("empty survivor index");
        }
        indices.push_back(parse_size("--survivors", token));
    }
    return indices;
}

void print_usage() {
    std::cout << "shiftxor_bench [options]\n"
              << "  --n N --k K --bytes BYTES --slice BYTES --iterations N\n"
              << "  --backend scalar|avx2 --codec shiftxor|isal|longhair|all\n"
              << "  --survivors I,J,... --format jsonl\n";
}

}  // namespace

int main(int argc, char** argv) {
    BenchmarkCase requested;
    bool explicit_dimensions = false;
    std::string codec_name = "all";
    BackendKind backend = BackendKind::kScalar;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            }
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing value for " + option);
            }
            const std::string value = argv[++index];
            if (option == "--n") {
                requested.n = parse_size("--n", value);
                explicit_dimensions = true;
            } else if (option == "--k") {
                requested.k = parse_size("--k", value);
                explicit_dimensions = true;
            } else if (option == "--bytes") {
                requested.input_length = parse_size("--bytes", value);
            } else if (option == "--slice") {
                requested.slice_bytes = parse_size("--slice", value);
            } else if (option == "--iterations") {
                requested.iterations = parse_size("--iterations", value);
            } else if (option == "--survivors") {
                requested.survivors = parse_indices(value);
            } else if (option == "--backend") {
                if (value == "scalar") {
                    backend = BackendKind::kScalar;
                } else if (value == "avx2") {
                    backend = BackendKind::kAvx2;
                } else {
                    throw std::invalid_argument("backend must be scalar or avx2");
                }
            } else if (option == "--codec") {
                codec_name = value;
            } else if (option == "--format") {
                if (value != "jsonl") {
                    throw std::invalid_argument("only jsonl output is supported");
                }
            } else {
                throw std::invalid_argument("unknown option " + option);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        print_usage();
        return 2;
    }

    std::vector<BenchmarkCase> cases;
    if (explicit_dimensions) {
        cases.push_back(requested);
    } else {
        for (const auto [n, k] : {std::pair<std::size_t, std::size_t>{9, 6},
                                  {15, 10}, {18, 12}, {20, 16}}) {
            auto config = requested;
            config.n = n;
            config.k = k;
            cases.push_back(config);
        }
    }

    for (const auto& config : cases) {
        std::vector<std::unique_ptr<CodecAdapter>> adapters;
        if (codec_name == "shiftxor" || codec_name == "all") {
            adapters.push_back(shiftxor::bench::make_shiftxor_adapter(backend));
        }
        if (codec_name == "isal" || codec_name == "all") {
            adapters.push_back(shiftxor::bench::make_isa_l_adapter());
        }
        if (codec_name == "longhair" || codec_name == "all") {
            adapters.push_back(shiftxor::bench::make_longhair_adapter());
        }
        if (adapters.empty()) {
            std::cerr << "codec must be shiftxor, isal, longhair, or all\n";
            return 2;
        }
        for (const auto& adapter : adapters) {
            for (const auto& record : adapter->run(config)) {
                std::cout << shiftxor::bench::record_to_json(record) << '\n';
            }
        }
    }
    return 0;
}
