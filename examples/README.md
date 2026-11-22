# Examples and legacy experiments

`shift_xor_9_6` is the maintained general Shift-XOR example. It uses the public matrix and codec API, generates a deterministic `(9,6)` codeword, removes three systematic blocks, and validates recovery from the three parity rows plus three systematic rows.

The historical programs remain under `legacy/` for comparison with earlier thesis experiments. They are built as separate targets when `SHIFT_XOR_BUILD_LEGACY=ON`.

- `shift_xor_lrc` keeps the scalar LRC construction, repair, and conversion experiment.
- `shift_xor_lrc_avx2` keeps the AVX2 LRC experiment.
- `shift_xor_9_6_simple` and `shift_xor_9_6_avx2` keep the earlier fixed-parameter implementations.

The legacy sources are excluded from the `shiftxor` library and are no longer collected into one executable.
