#include <cassert>
#include <string>

#include "bench/adapter.h"

int main() {
    const auto adapter = shiftxor::bench::make_shiftxor_adapter(shiftxor::BackendKind::kScalar);
    assert(adapter);
    assert(adapter->available());
    const shiftxor::bench::BenchmarkCase config{9, 6, 64 * 1024, 8, 2,
                                                 {0, 1, 2, 6, 7, 8}};
    const auto records = adapter->run(config);
    assert(records.size() == 2);
    for (const auto& record : records) {
        assert(record.available);
        assert(record.validated);
        assert(record.logical_bytes > 0);
        assert(record.physical_bytes > 0);
        assert(record.elapsed_seconds > 0.0);
        assert(record.throughput_mib_per_second > 0.0);
        const auto json = shiftxor::bench::record_to_json(record);
        assert(json.find("\"logical_bytes\"") != std::string::npos);
        assert(json.find("\"physical_bytes\"") != std::string::npos);
        assert(json.find("\"validated\":true") != std::string::npos);
    }

    const auto longhair = shiftxor::bench::make_longhair_adapter();
    if (longhair->available()) {
        const auto external_records = longhair->run(config);
        assert(external_records.size() == 2);
        for (const auto& record : external_records) {
            assert(record.available);
            assert(record.validated);
        }
    }
    return 0;
}
