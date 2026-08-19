# Contributing to `fsmc`

Thank you for your interest in contributing to **`fsmc`**!

---

## 1. Development Setup

### Prerequisites
- CMake 3.14+
- Modern C++20 compiler (GCC 10+, Clang 11+, MSVC 2019+)
- `clang-format` and `clang-tidy`

### Build & Run Tests
```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Run full CTest suite (14 targets)
ctest --test-dir build --output-on-failure
```

---

## 2. Code Formatting & Quality

Before submitting a Pull Request, ensure your code complies with our formatting standards:

```bash
# Format code
find src include tests examples -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Verify clang-tidy diagnostics
clang-tidy -p build src/main.cpp
```

---

## 3. Submitting Changes

1. Fork the repository on GitHub.
2. Create a feature branch: `git checkout -b feature/my-new-feature`.
3. Add unit tests for your changes in `tests/`.
4. Commit your changes: `git commit -m "feat: add support for X"`.
5. Push to your branch and open a Pull Request.
