# Shift-XOR library migration implementation plan

## File map

- Create `include/shiftxor/buffer.h` for non-owning byte-buffer views.
- Create `include/shiftxor/status.h` for structured operation results and error codes.
- Create `include/shiftxor/matrix.h` and `src/shiftxor/matrix.cpp` for validated full shift matrices, row lengths, overhead, and systematic Two-Tone construction.
- Create `include/shiftxor/codec.h` and `src/shiftxor/codec.cpp` for the public codec API and portable encode/decode implementation.
- Create `include/shiftxor/backend.h`, `src/shiftxor/backend_scalar.cpp`, and optional `src/shiftxor/backend_avx2.cpp` for the XOR kernels.
- Create `tests/test_matrix.cpp`, `tests/test_codec.cpp`, and `tests/test_backend.cpp` with a small dependency-free test runner shared by the files.
- Replace `CMakeLists.txt` with explicit library, test, benchmark, and example targets. Keep legacy sources outside the default source glob.
- Create `bench/adapter.h`, `bench/shiftxor_adapter.cpp`, `bench/isa_l_adapter.cpp`, `bench/longhair_adapter.cpp`, and `bench/shiftxor_bench.cpp` for one benchmark contract and JSONL output.
- Create `examples/shift_xor_9_6.cpp` as the migrated `(9,6)` demonstration and `examples/README.md` documenting the legacy LRC targets.
- Create `docs/benchmark-methodology.md` with the case list, throughput denominator, warm-up policy, and external dependency setup.
- Move the current standalone programs to `legacy/` without changing their contents so they remain available for historical comparison but are no longer linked together.

## Review focus

- A matrix with `z^∞` entries must never read or write the corresponding source slice.
- A shifted row must expose and allocate the tail implied by its maximum finite shift.
- Any selected `k` survivor rows of a generated Two-Tone matrix must recover the original data, including survivors that are not the first `k` rows.
- Decode must reject duplicate, out-of-range, or incorrectly sized survivor buffers before touching output buffers.
- AVX2 and scalar backends must produce byte-identical codewords for non-zero shifts and tail padding.

### Task 1: Establish explicit targets and core value types

**Files:**
- Move `src/*.cpp` to `legacy/` while preserving source text.
- Modify `CMakeLists.txt`.
- Create `include/shiftxor/buffer.h`, `include/shiftxor/status.h`, and `include/shiftxor/backend.h`.
- Test `tests/test_matrix.cpp`.

**Interfaces:**
- `BufferView { std::uint8_t* data; std::size_t size; }`.
- `ConstBufferView { const std::uint8_t* data; std::size_t size; }`.
- `IndexedConstBufferView { std::size_t index; ConstBufferView buffer; }`.
- `enum class StatusCode { kOk, kInvalidArgument, kInvalidMatrix, kUnsupported, kInsufficientBlocks, kBufferTooSmall }`.
- `class Status { StatusCode code() const; const std::string& message() const; explicit operator bool() const; }`.
- `enum class BackendKind { kScalar, kAvx2 }`.

- [ ] **Step 1: Write the failing build and matrix-construction test**

  Add a test that includes the new public headers, constructs a `3×2` matrix with shifts `{0, -1, 1, 2, 3, 0}`, and asserts that the dimensions, infinity marker, row maxima, and status types are available.

- [ ] **Step 2: Run the test to verify it fails**

  Run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target shiftxor_tests && ctest --test-dir build --output-on-failure`.

  Expected result is a compile failure because the new headers and target do not exist.

- [ ] **Step 3: Add explicit CMake targets and the value-type headers**

  Define a `shiftxor` static library target with C++17 include paths, a `shiftxor_tests` executable, and separate named legacy/example targets. Do not use `file(GLOB ...)`. Keep AVX2 and external backends behind options.

- [ ] **Step 4: Run the test to verify it passes**

  Re-run the command from Step 2. The test must compile and pass.

- [ ] **Step 5: Commit**

  Commit with `build: split shift-xor targets and add core value types`.

### Task 2: Implement matrix validation and Two-Tone generation

**Files:**
- Create `include/shiftxor/matrix.h` and `src/shiftxor/matrix.cpp`.
- Modify `tests/test_matrix.cpp`.

**Interfaces:**
- `class ShiftMatrix` with `static Result<ShiftMatrix> create(std::size_t rows, std::size_t cols, std::vector<std::int32_t> shifts)`.
- `std::size_t rows() const`, `std::size_t cols() const`, and `std::int32_t at(std::size_t row, std::size_t col) const`.
- `std::int32_t row_max_shift(std::size_t row) const`, `std::size_t row_length(std::size_t row, std::size_t input_length, std::size_t slice_bytes) const`.
- `std::size_t storage_overhead(std::size_t slice_bytes) const`.
- `bool is_systematic() const` and `bool is_two_tone() const`.
- `static Result<ShiftMatrix> make_systematic_two_tone(std::size_t n, std::size_t k)`.

- [ ] **Step 1: Write failing matrix tests**

  Cover invalid dimensions, wrong shift-vector size, shifts below `-1`, negative finite shifts, row lengths for zero and non-zero shifts, all-infinity rows, the systematic identity prefix, the default divide, and the strict Two-Tone predicate.

- [ ] **Step 2: Run `ctest --test-dir build --output-on-failure` and confirm the new assertions fail**.

- [ ] **Step 3: Implement `ShiftMatrix` and the thesis Two-Tone factory**

  Store shifts row-major with `-1` representing `z^∞`. Use `row_length = input_length + max_finite_shift * slice_bytes` and `row_length = 0` when a row has no finite entry. Generate the full systematic matrix with an identity prefix and the reflected two-tone parity rows used by the thesis.

- [ ] **Step 4: Run the focused matrix tests and the full test target**

  Run `ctest --test-dir build --output-on-failure`. Expected result is all matrix tests passing.

- [ ] **Step 5: Commit**

  Commit with `feat: add validated shift matrices and two-tone factory`.

### Task 3: Implement the scalar codec and supported decode contract

**Files:**
- Create `include/shiftxor/codec.h`, `src/shiftxor/codec.cpp`, and `src/shiftxor/backend_scalar.cpp`.
- Modify `tests/test_codec.cpp` and `tests/test_matrix.cpp`.

**Interfaces:**
- `struct CodecConfig { std::size_t input_length; std::size_t slice_bytes; BackendKind backend; }`.
- `class ShiftXorCodec { static Result<ShiftXorCodec> create(ShiftMatrix matrix, CodecConfig config); Status encode(std::vector<ConstBufferView> data, std::vector<BufferView> codeword) const; Status decode(std::vector<IndexedConstBufferView> available, std::vector<BufferView> data_out) const; std::vector<std::size_t> row_lengths() const; }`.
- `Status supports(std::vector<std::size_t> survivor_indices) const` is a pure preflight with the same stable status codes as `decode`.

- [ ] **Step 1: Write failing encode tests**

  Use a `3×2` matrix with a zero row, a finite shift, and an omitted entry. Assert exact bytes for the shifted XOR output, including zero-filled prefix and tail. Add a test that input length not divisible by `slice_bytes` is rejected.

- [ ] **Step 2: Run the focused test and confirm it fails for the missing codec**

  Run `cmake --build build --target shiftxor_tests && ctest --test-dir build --output-on-failure`.

- [ ] **Step 3: Implement scalar encode and validation**

  Clear each output row, iterate source slices, skip `-1` entries, and XOR into `(slice_index + shift) * slice_bytes`. Validate source count, output count, buffer sizes, and null pointers before mutation.

- [ ] **Step 4: Write failing decode tests**

  Generate arbitrary non-systematic and systematic Two-Tone codewords for `(9,6)` and `(15,10)`, select multiple deterministic `k`-row survivor subsets including subsets that omit systematic rows, and assert all original data blocks are recovered. Add duplicate/out-of-range/short-buffer tests and an unsupported non-Two-Tone matrix test.

- [ ] **Step 5: Run the decode tests and confirm the expected failures**.

- [ ] **Step 6: Implement Two-Tone elimination**

  Select the caller-provided survivor rows, use the corresponding `k×k` Two-Tone submatrix, back-substitute available sequence slices in increasing source order, and recover the pivot source sequence for each slice before substituting it into the remaining rows. Return `kInsufficientBlocks` unless exactly `k` unique rows are supplied, and return `kUnsupported` when the selected submatrix is not Two-Tone or a pivot is outside the finite shifted region.

- [ ] **Step 7: Run the focused and full tests**

  Run `ctest --test-dir build --output-on-failure`. Expected result is successful round trips and explicit status failures for unsupported inputs.

- [ ] **Step 8: Commit**

  Commit with `feat: generalize scalar shift-xor encode and decode`.

### Task 4: Add AVX2 backend with scalar fallback

**Files:**
- Modify `include/shiftxor/backend.h`, `src/shiftxor/codec.cpp`, and `CMakeLists.txt`.
- Create `src/shiftxor/backend_avx2.cpp`.
- Modify `tests/test_backend.cpp`.

**Interfaces:**
- `bool avx2_available()`.
- `Status xor_shifted(ConstBufferView source, std::size_t shift_bytes, BufferView destination, std::size_t slice_bytes)`.

- [ ] **Step 1: Write the backend equivalence test**

  Encode the same random data and matrix through scalar and AVX2 when available, then compare every row byte-for-byte. The test skips only the AVX2 branch when runtime support is absent.

- [ ] **Step 2: Run the test and confirm the AVX2 implementation is missing or fails**.

- [ ] **Step 3: Implement aligned-width AVX2 XOR and runtime dispatch**

  Use 32-byte loads and stores for AVX2, handle the configured slice width by batching complete slices, and delegate unsupported widths or unavailable CPUs to the scalar kernel. Never read past the source or row buffer.

- [ ] **Step 4: Run backend and full tests**.

- [ ] **Step 5: Commit**

  Commit with `feat: add optional avx2 shift-xor backend`.

### Task 5: Build the benchmark adapter layer and CLI

**Files:**
- Create `bench/adapter.h`, `bench/shiftxor_adapter.cpp`, `bench/isa_l_adapter.cpp`, `bench/longhair_adapter.cpp`, and `bench/shiftxor_bench.cpp`.
- Create `docs/benchmark-methodology.md`.
- Modify `CMakeLists.txt`.
- Test `tests/test_benchmark.cpp`.

**Interfaces:**
- `struct BenchmarkCase { std::size_t n; std::size_t k; std::size_t input_length; std::size_t slice_bytes; std::size_t iterations; std::vector<std::size_t> survivors; }`.
- `struct BenchmarkRecord { std::string codec; std::string backend; std::string operation; BenchmarkCase config; std::size_t logical_bytes; std::size_t physical_bytes; double elapsed_seconds; double throughput_mib_per_second; bool validated; }`.
- `class CodecAdapter { virtual Status encode(...); virtual Status decode(...); virtual bool available() const; virtual std::string name() const; }`.

- [ ] **Step 1: Write a failing CLI contract test**

  Run the benchmark with `--n 9 --k 6 --bytes 65536 --slice 8 --iterations 2 --format jsonl --backend scalar`, parse one record per operation, and assert that parameter fields, logical and physical byte fields, validation, and non-zero throughput are present.

- [ ] **Step 2: Run the test and confirm the CLI target is missing**.

- [ ] **Step 3: Implement the adapter contract and Shift-XOR adapter**

  Use one deterministic survivor set for the default case, warm up once, time repeated encode/decode calls with a monotonic clock, validate decoded buffers before emitting records, and report median throughput across measured samples. Keep JSONL output stable and include backend availability.

- [ ] **Step 4: Implement optional ISA-L and Longhair adapters**

  Guard each adapter with a CMake option and dependency check. ISA-L uses its expanded GF(2^8) encode matrix and recovery path. Longhair uses `cauchy_256_encode` and `cauchy_256_decode` with equal block sizes divisible by eight. When a dependency is absent, the CLI records `available=false` and does not attempt a run.

- [ ] **Step 5: Add default cases and methodology documentation**

  Document `(9,6)`, `(15,10)`, `(18,12)`, and `(20,16)` with a 1 MiB file, 8-byte slices, warm-up, repeated measurements, MiB/s denominators, and the distinction between logical input bytes and physical Shift-XOR storage including shifted tails.

- [ ] **Step 6: Run the CLI test and full test suite**

  Run `ctest --test-dir build --output-on-failure` and one manual benchmark invocation with external backends disabled.

- [ ] **Step 7: Commit**

  Commit with `feat: add reproducible codec benchmark adapters`.

### Task 6: Migrate examples and retain LRC compatibility

**Files:**
- Create `examples/shift_xor_9_6.cpp` and `examples/README.md`.
- Move `src/shift_xor_lrc.cpp` and `src/shift_xor_lrc_avx2.cpp` to `legacy/`.
- Modify `CMakeLists.txt`.
- Test `tests/test_examples.cpp`.

**Interfaces:**
- The `(9,6)` example uses `make_systematic_two_tone(9, 6)` and the public codec API.
- Legacy LRC programs remain separately named targets and are excluded from the core library target.

- [ ] **Step 1: Write an example smoke test**

  Invoke the migrated `(9,6)` example with a small deterministic input and assert that it reports successful round-trip validation.

- [ ] **Step 2: Run the smoke test and confirm the migrated example is missing**.

- [ ] **Step 3: Implement the example and explicit legacy targets**

  Keep LRC global/local parity, repair, and conversion behavior in their existing programs until a second design covers their API. Document which targets are legacy and which use the new library.

- [ ] **Step 4: Run example, legacy, and full tests**

  Run `cmake --build build --target shift_xor_9_6 shift_xor_lrc shift_xor_lrc_avx2` and `ctest --test-dir build --output-on-failure`.

- [ ] **Step 5: Commit**

  Commit with `refactor: migrate examples and isolate legacy lrc experiments`.

## Final verification

Run the complete command sequence below from a clean build directory.

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSHIFT_XOR_ENABLE_ISAL=OFF -DSHIFT_XOR_ENABLE_LONGHAIR=OFF
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/bin/shift_xor_bench --n 9 --k 6 --bytes 65536 --slice 8 --iterations 3 --format jsonl --backend scalar
```

The build must finish with exit code 0, every test must pass, the benchmark must emit validated encode and decode records, and the migrated example must report a successful recovery.

