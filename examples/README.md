# Examples

Self-contained programs, each compilable against `../include` (or the
single header). Build with `make`, build and run with `make run`; they
also build and run as tests through CMake/CTest.

| File | Shows |
|------|-------|
| [`config.cpp`](config.cpp) | a configuration baked into the binary: compile-time requirement checks, values as array sizes, zero-cost runtime reads |
| [`validation.cpp`](validation.cpp) | `is_valid` as a bool, strict RFC 8259 rejections, schema-style shape asserts with `kind` |
| [`introspection.cpp`](introspection.cpp) | a generic recursive visitor over any document: positional iteration, kind dispatch, re-serialization |
| [`serialize.cpp`](serialize.cpp) | `ctjson::serialize` — canonical minified output, escape re-emission, fixed-point round-trip |
| [`tables.cpp`](tables.cpp) | JSON as a data table: an array of objects turned into a constexpr `std::array` of structs, queried at compile time |
| [`localization.cpp`](localization.cpp) | a message catalog with compile-time fallback via `contains<>` and `if constexpr`, iterated with `for_each` |
| [`python-style.cpp`](python-style.cpp) | the runtime encoder: `dumps`/`dump` on native C++ values, indent and sort_keys, the `to_json` hook |
| [`cxx17-syntax.cpp`](cxx17-syntax.cpp) | the C++17 API: `fixed_string` variables for inputs and keys (built with `-std=c++17`) |

All examples build in C++20 mode except `cxx17-syntax.cpp`.
