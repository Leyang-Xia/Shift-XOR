#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "shiftxor/backend.h"
#include "shiftxor/status.h"

namespace shiftxor::bench {

struct BenchmarkCase {
    std::size_t n = 9;
    std::size_t k = 6;
    std::size_t input_length = 1024 * 1024;
    std::size_t slice_bytes = 8;
    std::size_t iterations = 10;
    std::vector<std::size_t> survivors;
};

struct BenchmarkRecord {
    std::string codec;
    std::string backend;
    std::string operation;
    BenchmarkCase config;
    std::size_t logical_bytes = 0;
    std::size_t physical_bytes = 0;
    double elapsed_seconds = 0.0;
    double throughput_mib_per_second = 0.0;
    bool available = false;
    bool validated = false;
    std::string error;
};

class CodecAdapter {
public:
    virtual ~CodecAdapter() = default;
    virtual std::string name() const = 0;
    virtual bool available() const = 0;
    virtual std::vector<BenchmarkRecord> run(const BenchmarkCase& config) const = 0;
};

std::unique_ptr<CodecAdapter> make_shiftxor_adapter(BackendKind backend);
std::unique_ptr<CodecAdapter> make_isa_l_adapter();
std::unique_ptr<CodecAdapter> make_longhair_adapter();
std::string record_to_json(const BenchmarkRecord& record);

}
