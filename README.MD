# Othello (C17)

## Build

- `make` builds `out/othello`.
- `make test` builds and runs the unit tests.
- CMake: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`.

The original game function implementations are split across task-oriented modules.
Shared types and declarations are in `include/common.h`.
