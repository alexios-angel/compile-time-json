> **Attribution:** this library is built on the CTLL compile-time LL(1)
> parser from [CTRE](https://github.com/hanickadot/compile-time-regular-expressions)
> by Hana Dusíková, via the [notre](https://github.com/alexios-angel/notre)
> fork. Apache License 2.0 with LLVM Exceptions; see [NOTICE](NOTICE).

# ctjson — compile-time JSON

JSON parsed while your code compiles. The document is a *type*: malformed
JSON is a compile error, lookups are resolved at compile time, and every
accessor is `constexpr` — usable in `static_assert`, as template
arguments, or at runtime with zero parsing cost.

```c++
#include <ctjson.hpp>

constexpr auto doc = ctjson::parse<R"({
    "name":  "Hana",
    "stars": 4700,
    "ratio": -2.5e-1,
    "tags":  ["regex", "compile-time"],
    "extra": {"active": true, "parent": null}
})">();

static_assert(doc.get<"name">() == "Hana");
static_assert(doc.get<"stars">().to<int>() == 4700);
static_assert(doc.get<"ratio">().to<double>() == -0.25);
static_assert(doc.get<"tags">().get<1>() == "compile-time");
static_assert(doc.get<"extra">().get<"active">());
static_assert(!doc.contains<"missing">());

static_assert(ctjson::is_valid<"[1, 2, 3]">);
static_assert(!ctjson::is_valid<"[1, 2,]">);   // RFC 8259, strictly
```

## API

```c++
// validity as a bool (never a compile error):
template <ctll::fixed_string input> constexpr bool ctjson::is_valid;

// the parsed document; invalid JSON fails the build:
template <ctll::fixed_string input> constexpr auto ctjson::parse();
```

`parse` returns one of the document types, all in namespace `ctjson`:

| Type | Accessors |
|------|-----------|
| `object<members...>` | `get<"key">()`, `["key"]`, `contains<"key">()`, `size()`, `empty()`, positional `key<N>()` / `value<N>()`, range-for over member views |
| `array<values...>` | `get<N>()`, `[N]`, `size()`, `empty()`, range-for over value views |
| `string<chars...>` | `view()`, `c_str()` (null-terminated), `size()`, `empty()`, `==` with `std::string_view` |
| `number<chars...>` | `to<T>()` for any arithmetic `T`, `is_integer()`, `view()` (raw spelling), `c_str()` |
| `boolean<B>` | `value`, `operator bool` |
| `null` | — |

Every type carries `static constexpr ctjson::kind type` for
introspection (`kind::object`, `kind::array`, ...).

Two free functions round out the API:

```c++
// render any document value back to minified JSON, in static storage:
static_assert(ctjson::serialize(ctjson::parse<R"({ "a" : [ 1, 2 ] })">()) == R"({"a":[1,2]})");

// compile-time iteration (elements, or key/value pairs):
ctjson::for_each(doc.get<"tags">(), [](auto value) { /* each has its own type */ });
ctjson::for_each(doc, [](auto key, auto value) { ... });
```

`serialize` re-emits strings with the mandatory escapes and numbers with
the spelling they were parsed with; the result is null-terminated.

Brackets and iteration:

```c++
doc["name"];             // get<"name">(), spelled with brackets
doc["tags"][1];          // plain keys and indexes; chains work to any depth
doc.contains("tags");    // runtime keys work too

// begin/end yield uniform views (kind + text) from static storage, so
// range-for and algorithms work - in constexpr evaluation included:
for (const auto & m : doc) {
    m.key;          // std::string_view
    m.value.type;   // ctjson::kind
    m.value.text;   // decoded strings, raw number spellings, minified containers
}
```

Elements have distinct types, so a runtime key or index cannot return the
element itself. `operator[]` returns a uniform `value_view`, which remains
constexpr and can be chained; misses produce a null view. `get<...>()`
remains the typed accessor.
The records are `value_view` and `member_view`
([`views.hpp`](include/ctjson/views.hpp)), and
[`examples/iteration.cpp`](examples/iteration.cpp) is a runnable tour.

## Debugging

When `is_valid` says `false`, the reason is one query away, computed at
compile time:

```c++
constexpr auto info = ctjson::error_info<"[1,2,]">();
// info.kind (lex/parse/...), info.position, info.line, info.column,
// info.expected[0..expected_count)

constexpr auto why = ctjson::error_message<"[1,2,]">();
//   ctlark: lexical error at line 1, column 6: no expected terminal matches
//     [1,2,]
//          ^
//   expected: STRING, NUMBER, 'true', 'false', 'null', '{', '['

// documents that PARSE can still break the \u surrogate rules; the
// binder names the offending string:
constexpr auto be = ctjson::bind_error<R"(["\uD800"])">();
// be.reason == ctjson::bind_reason::bad_surrogate, be.where == R"("\uD800")"
```

A failed `parse<>()` names the failing stage and the query to run in its
`static_assert` message. `ctjson::debug` bundles the [ctlark debugging
toolbox](../compile-time-lark#debugging) with the JSON grammar baked in:
`traced_parse<input>()` (a recorded event log, also runnable at runtime
under a debugger), `parse_runtime(text)` (runtime inputs against the
compile-time tables), `dump_tokens<input>()` and `dump_grammar()`. The
runtime `loads`/`load` errors carry `line`/`column` alongside
`position`, and `to_string(load_error)` renders them.

## Python-style runtime API

`ctjson::dumps` is `json.dumps` for ordinary C++ values — and it is
`constexpr`, so whole encodings can be `static_assert`ed (needs C++20's
constexpr `std::string`/`std::vector`; `dump` to a stream is inherently
runtime). Floats are rendered by a built-in constexpr Dragon4 (shortest
round-tripping digits via exact big-integer arithmetic) with Python
repr's formatting rule, and the output is verified byte-identical
against CPython across 100,000 fuzzed doubles plus every edge case:

```c++
static_assert(ctjson::dumps(std::vector<int>{1, 2, 3}) == "[1, 2, 3]");
static_assert(ctjson::dumps(0.1) == "0.1");
static_assert(ctjson::dumps(1e16) == "1e+16");     // Python's sci threshold
static_assert(ctjson::dumps(5e-324) == "5e-324");  // denormals exact
```

```c++
ctjson::dumps(std::map<std::string, std::vector<int>>{{"a", {1, 2}}});
//  -> {"a": [1, 2]}                        (Python's separators)
ctjson::dumps(value, 2);                    // indent=2 pretty printing
ctjson::dumps(value, {.indent = 2, .sort_keys = true, .ensure_ascii = true});
ctjson::dump(value, stream);                // json.dump, onto a std::ostream
ctjson::loads<"[1, 2]">();                  // parse, under its Python name
```

The encoder accepts what Python's does: map-likes become objects
(arithmetic keys are quoted, `{1: "x"}` style), iterables plus
`std::pair`/`std::tuple` become arrays, string-likes and `char` become
strings, integral and floating-point values become numbers (floats stay
visibly floats: `1.0`, and NaN/Infinity render like Python's
`allow_nan=True`), `nullptr` and empty `std::optional` become `null`,
`std::variant` dumps its active alternative, and parsed ctjson
documents dump directly (numbers keeping their parsed spelling). For
anything else, define a `to_json(const T &)` findable by ADL returning
something dumpable — the `default=` hook. One deliberate divergence:
`ensure_ascii` defaults to **false** (UTF-8 passes through); switch it
on for Python's `\uXXXX` output, surrogate pairs included.

Details:

* String content is stored as UTF-8 bytes. All escapes are decoded at
  parse time, including `\uXXXX` and UTF-16 surrogate pairs
  (`"😀"` becomes the four UTF-8 bytes of 😀); lone
  surrogates are rejected.
* Numbers keep their raw spelling; `to<T>()` converts on demand
  (integral conversions truncate fractions, like a cast).
* Parsing is strict RFC 8259: no trailing commas, no leading zeros, no
  unquoted keys, no `'` strings, nothing after the root value.
  Duplicate keys are accepted (the RFC allows them); `get` finds the
  first.

## C++17

With a pre-C++20 compiler, inputs and keys are `constexpr
ctll::fixed_string` variables with linkage instead of string literals:

```c++
static constexpr auto text = ctll::fixed_string{R"({"n":42})"};
static constexpr ctll::fixed_string n_key = "n";

constexpr auto doc = ctjson::parse<text>();
static_assert(doc.template get<n_key>().template to<int>() == 42);
```

## Runtime parsing

`ctjson::loads(text)` and `ctjson::load(stream)` are `json.loads`/`json.load`
for strings that only exist at runtime. They return
`std::optional<ctjson::value>` — a dynamic document mirroring the
compile-time accessors — with `std::nullopt` (plus a byte offset through
the `load_error` overloads) on malformed input; the library stays
exception free:

```c++
if (auto doc = ctjson::loads(request_body)) {
    (*doc)["users"][0]["name"].view();   // chains are null-safe
    (*doc)["retries"].to<int>();
    ctjson::dumps(*doc, 2);              // the encoder accepts values
}
```

Numbers keep Python's int/float distinction (`is_integer()`), `NaN` and
the infinities parse like Python's `parse_constant`, and missing lookups
follow the null-object pattern (`find`/`contains` distinguish absent
from null). Documented divergences from Python: integers beyond
`long long` become doubles, duplicate keys are all kept (first wins,
like the compile-time parser), and lone `\u` surrogates are rejected.

## How it works

The grammar layer is
[ctlark](https://github.com/alexios-angel/compile-time-lark)
(compile-time Lark): the RFC 8259 grammar is a *lark grammar string*
([`grammar.hpp`](include/ctjson/grammar.hpp)) that ctlark parses and
compiles to constexpr tables while your code compiles, then runs its
constexpr Earley parser over your document. The binder
([`bind.hpp`](include/ctjson/bind.hpp)) lowers the resulting lark tree
into the document types, decoding string escapes (including `\uXXXX`
and surrogate pairs) to UTF-8 on the way; surrogate pairing - the one
JSON rule a regular terminal cannot express - is checked there and
folded into `is_valid`.

Because that work happens in headers, a **precompiled header** makes
it a one-time cost: `make pch` (done automatically by the test build)
compiles `ctjson.hpp` once - grammar parse, table build and all - and
every translation unit that includes it afterwards starts from the
baked result (about 3.4s down to 0.15s per TU with clang). The CMake
tests and examples use `target_precompile_headers` the same way
(`CTJSON_PCH`, default ON).

An Earley parse needs a raised constexpr budget; the CMake interface
target carries the compiler-specific limit flags automatically
(`CTJSON_CONSTEXPR_LIMITS`, default ON) and the Makefiles set them:

```
clang:  -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=2048
gcc:    -fconstexpr-ops-limit=3000000000 -fconstexpr-loop-limit=10000000 -fconstexpr-depth=1024
```

ctlark (and the CTLL layer under it) comes in as a git submodule
(`external/compile-time-lark` — clone with `--recurse-submodules` or
run `git submodule update --init`); never edit it here. The only
generated parse table is ctlark's own `lark.hpp` (the grammar of the
Lark grammar language), regenerated in that repo with its
`make regrammar` after editing `lark.gram`.

## Building and integrating

Header-only. Pick whichever fits your project:

**CMake, as a subdirectory or via FetchContent:**

```cmake
add_subdirectory(compile-time-json)   # or FetchContent_MakeAvailable(ctjson)
target_link_libraries(your-target PRIVATE ctjson::ctjson)
```

**CMake, installed** (`cmake -B build && cmake --install build`):

```cmake
find_package(ctjson 0.1 REQUIRED)
target_link_libraries(your-target PRIVATE ctjson::ctjson)
```

The install also ships a `pkg-config` file (`ctjson.pc`). Tests and
examples build only when ctjson is the top-level project
(`CTJSON_BUILD_TESTS`, `CTJSON_BUILD_EXAMPLES`); `CTJSON_CXX_STANDARD`
selects the advertised standard (default 20). CPack can produce
TGZ/ZIP archives (plus DEB/RPM where the tooling exists), and
`-DCTJSON_MODULE=ON` builds `ctjson.cppm` as a named C++ module
(experimental; needs CMake 3.30+, a modules-capable toolchain and
`import std`).

**No build system:** add `include/` plus the submodule's
`external/compile-time-lark/include` (and its `ctlark`/`ctll`
subdirectories, so the headers' relative `"../ctlark.hpp"`-style
includes resolve) to your include path, or copy the amalgamated
[`single-header/ctjson.hpp`](single-header/ctjson.hpp)
(regenerate with `make single-header`, which needs the
[quom](https://pypi.org/project/quom/) tool).

Requires C++17 (C++20 for the string-literal API). Runnable demos live
in [`examples/`](examples/).

Run the tests (compilation is the test — the suite is `static_assert`s):

```bash
git submodule update --init            # ctlark + ctll (once, after cloning)
make CXX=clang++                       # C++20
make CXX=clang++ CXX_STANDARD=17
# or through CMake/CTest:
cmake -B build && cmake --build build && ctest --test-dir build
```

## License

Apache License 2.0 with LLVM Exceptions (see [LICENSE](LICENSE)).
The CTLL parser is Hana Dusíková's work, via notre; see
[NOTICE](NOTICE).
