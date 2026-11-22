# Benchmark methodology

The benchmark reports one JSON object per operation and one object per codec. Each record stores the `(n,k)` parameters, input length, slice width, iteration count, logical bytes, physical stored bytes, elapsed time, throughput, dependency availability, and validation status.

Encode throughput uses `k * L` logical source bytes. Decode throughput uses the number of missing systematic blocks multiplied by `L`. Physical bytes include the full Shift-XOR row lengths with shifted tails. Reed-Solomon adapters use equal-sized physical blocks and retain the same logical denominator.

The default cases are `(9,6)`, `(15,10)`, `(18,12)`, and `(20,16)` with `L = 1 MiB` and 8-byte slices. An explicit `--n` and `--k` selects one case. The default survivor set keeps the first `k-(n-k)` systematic blocks and all parity blocks when that set is possible. This produces a non-trivial data recovery workload for the thesis configurations.

Each adapter performs one warm-up operation, then records the median of the requested timed iterations using `std::chrono::steady_clock`. A record is emitted only after decoded buffers are compared with the original input. Optional ISA-L and Longhair adapters report `available=false` when their headers or libraries were not found at configure time.

ISA-L provides optimized Reed-Solomon style encoding over GF(2^8) through its erasure-code API. Longhair provides Cauchy Reed-Solomon block encoding with block sizes divisible by eight.

## Optional external libraries

Pass `-DSHIFT_XOR_ENABLE_ISAL=ON` and set `ISA_L_ROOT` to an ISA-L install prefix when it is outside the system search path. Pass `-DSHIFT_XOR_ENABLE_LONGHAIR=ON` and set `LONGHAIR_ROOT` to a Longhair install prefix. The benchmark records unavailable adapters instead of silently substituting another codec.
