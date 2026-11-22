# Shift-XOR experiment library redesign

## Intent

Turn the current collection of fixed `(9,6)` and LRC experiment programs into a small C++ library plus reproducible benchmark tools. The library will express the Shift-XOR definition from the thesis directly through a runtime shift matrix. The benchmark layer will compare Shift-XOR implementations with public Reed-Solomon libraries under the same `(n,k,L)` and workload parameters.

## Goals

- Represent a general shift-XOR system with `n` rows and `k` input sequences.
- Support finite non-negative shifts and an explicit omitted-entry value for `z^âˆž`.
- Derive parity length and storage overhead from the matrix instead of compile-time constants.
- Provide systematic encoding for arbitrary byte buffers whose length is divisible by the configured slice width.
- Decode every selected `k`-row subset for arbitrary valid Two-Tone matrices, including non-systematic `k¡Ák` systems and systematic `[I; ¦µ]` matrices. Non-Two-Tone matrices remain encodable but return a structured unsupported status for decode.
- Keep scalar execution portable and provide an AVX2 backend when the compiler and CPU support it.
- Expose the same benchmark case to Shift-XOR, ISA-L, and Longhair through adapters. External libraries remain optional build features.
- Emit machine-readable benchmark records with codec name, parameters, operation, logical input bytes, physical stored bytes, elapsed time, throughput, validation status, and backend metadata. Throughput is based on logical input bytes so Shift-XOR and GF(2^8) adapters have the same denominator, while physical storage including shifted tails remains visible.
- Preserve runnable examples for the existing `(9,6)` system and LRC experiments while moving their shared logic to the library.

## Non-goals for the first migration

- Implementing arbitrary LRC conversion as part of the base `(n,k)` codec API.
- Making ISA-L or Longhair dependencies mandatory for a default build.
- Claiming that an arbitrary shift matrix has the Two-Tone MDS property. The library will validate the Two-Tone structural preconditions and report unsupported erasure patterns.
- Changing the mathematical definition of the thesis or silently truncating the shifted tail.

## Anti-goals

- More global buffers, fixed missing indices, or duplicated encode/decode loops.
- A benchmark that reports a number without recording the exact code parameters and validation result.
- A build that globs every source file into one target. The current glob causes multiple `main` definitions and duplicate symbols.

## Constraints and decisions

The existing project is C++17 with CMake. The redesign will stay in C++17 and keep the source tree buildable on Linux and Windows where the selected compiler provides the requested ISA. Buffers are byte-oriented because the existing implementations operate on byte slices while the thesis defines the operation bitwise. A shift count moves a whole slice and zero-fills the prefix. The maximum finite shift in each row determines that output row's length, so parity buffers include the derived tail.

The public API will use non-owning `std::span`-like views only if the project can provide a C++17-compatible implementation without adding a dependency. Otherwise it will use a small `BufferView` structure with pointer and length. The owning test and benchmark harness will use `std::vector<std::uint8_t>`.

The core codec will separate three concerns. `ShiftMatrix` owns the full `nÃ—k` dimensions and shift entries. `ShiftXorCodec` validates a matrix and performs encode and decode. Backend implementations provide the inner XOR and shifted-copy operation. Decode accepts exactly `k` indexed available code blocks and writes all `k` original data blocks. The API exposes per-row output lengths, including a zero length for a row whose entries are all `z^âˆž`. Such a row is valid for encoding but cannot be selected as a decode survivor because it carries no information. Source buffers have equal length `L`, and the configured positive slice width must divide `L`.

The benchmark adapter interface will normalize the common Reed-Solomon shape to `k` source blocks, `m = n-k` parity blocks, and equal block length. ISA-L uses GF(2^8) matrix encoding and recovery, while Longhair uses Cauchy Reed-Solomon blocks with `bytes` divisible by eight. The benchmark will label these schemes separately because their arithmetic and storage layout differ from Shift-XOR.

## Approaches considered

The selected approach is a core C++ library with backend adapters and a benchmark CLI. It keeps the performance-critical path native, gives the experiments a stable API, and allows optional public libraries without coupling the core to their build systems.

A smaller extraction of helper functions would preserve more of the current files, but would leave each experiment with its own parameter and decode semantics. A Python orchestration layer would make result collection convenient, but it would add a second language boundary to the hot path and complicate reproducible builds on the current project.

## Proposed repository shape

- `include/shiftxor/` contains public matrix, buffer, codec, backend, and benchmark interfaces.
- `src/shiftxor/` contains the portable implementation and optional AVX2 kernels.
- `bench/` contains the parameterized benchmark CLI and public-library adapters.
- `examples/` contains the migrated `(9,6)` and LRC demonstrations.
- `tests/` contains matrix, encoding, decoding, validation, and adapter-contract tests.
- `docs/` contains this design, build instructions, and benchmark methodology.

The current standalone source files will either become thin compatibility executables or move under `examples/`. Their algorithms will not remain independent copies of the core implementation. General Shift-XOR is the primary deliverable. LRC and code conversion remain upper layers built on top of the core in later work. The default benchmark cases are `(9,6)`, `(15,10)`, `(18,12)`, and `(20,16)` with a 1 MiB file and 8-byte slices. The CLI can override dimensions, file size, slice width, iteration counts, and erasure pattern. Each case uses a deterministic `k`-row survivor set and can optionally run all `k`-row subsets for correctness.

## Verification required before implementation is considered complete

- CMake configure and build succeed with external backends disabled.
- Unit tests cover zero shifts, non-zero shifts, omitted entries, non-square matrices, tail bytes, invalid dimensions, and unsupported erasure patterns.
- Round-trip tests recover every supported erasure pattern for generated and user-supplied Two-Tone cases.
- Scalar and AVX2 results are byte-identical for the same input and matrix.
- Benchmark output validates every decoded block before writing a throughput record.
- External adapters are only compiled and run when their dependencies are found, and their availability is recorded in the benchmark output.
- The original `(9,6)` and LRC examples still run through named targets.

## Builder calls

- Use the thesis Two-Tone generator as the default matrix factory, while accepting user-supplied arbitrary Two-Tone matrices through the same public decode path.
- Treat decode support as a capability query. Return a structured error for a matrix or erasure pattern that the selected decoder cannot solve. Do not add a general polynomial-matrix inverse in this migration.
- Use warm-up iterations and a monotonic clock in benchmarks, and report median throughput over repeated samples.
- Keep external library setup out of the default build. Document `ISA-L_ROOT` and `LONGHAIR_ROOT` or equivalent CMake package hints when adapters are enabled.

