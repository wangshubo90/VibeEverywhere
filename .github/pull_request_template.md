## Summary

- what changed
- why it changed

## Validation

- [ ] `cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++`
- [ ] `cmake --build build --target sentrits_tests`
- [ ] `ctest --test-dir build --output-on-failure`

## Risks

- runtime behavior
- auth / pairing
- terminal / PTY behavior
- cross-platform behavior

## Docs

- [ ] updated docs if behavior or architecture changed
