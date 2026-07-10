// runtime tests for the Python-style encoder (ctjson::dumps builds
// std::strings, so unlike the rest of the suite these assertions run);
// executed by ctest, or directly: compile and run this file

#include <ctjson.hpp>
#undef NDEBUG // the assertions ARE the test; keep them in release builds
#include <cassert>
#include <cstdio>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

// the equivalent of Python's default= hook: ADL to_json
namespace geo {
	struct point { double x, y; };
	inline auto to_json(const point & p) {
		return std::map<std::string, double>{{"x", p.x}, {"y", p.y}};
	}
}

int main() {
	// scalars, Python spellings
	assert(ctjson::dumps(42) == "42");
	assert(ctjson::dumps(-7L) == "-7");
	assert(ctjson::dumps(true) == "true");
	assert(ctjson::dumps(false) == "false");
	assert(ctjson::dumps(nullptr) == "null");
	assert(ctjson::dumps(1.0) == "1.0");   // floats stay visibly floats
	assert(ctjson::dumps(2.5) == "2.5");
	assert(ctjson::dumps("hi") == "\"hi\"");
	assert(ctjson::dumps("a\nb"sv) == "\"a\\nb\"");
	assert(ctjson::dumps('x') == "\"x\"");
	assert(ctjson::dumps(0.0 / 0.0) == "NaN"); // allow_nan behaviour
	assert(ctjson::dumps(1.0 / 0.0) == "Infinity");

	// containers get Python's default separators (", " and ": ")
	assert(ctjson::dumps(std::vector<int>{1, 2, 3}) == "[1, 2, 3]");
	assert(ctjson::dumps(std::map<std::string, int>{{"a", 1}, {"b", 2}}) == R"({"a": 1, "b": 2})");
	assert(ctjson::dumps(std::vector<int>{}) == "[]");
	assert(ctjson::dumps(std::map<std::string, int>{}) == "{}");

	// tuples and pairs are arrays, like Python tuples
	assert(ctjson::dumps(std::tuple{1, "a", true}) == R"([1, "a", true])");
	assert(ctjson::dumps(std::pair{"k", 2}) == R"(["k", 2])");

	// optional is None-like, variant dumps its active alternative
	assert(ctjson::dumps(std::optional<int>{}) == "null");
	assert(ctjson::dumps(std::optional<int>{5}) == "5");
	assert(ctjson::dumps(std::variant<int, std::string>{"text"s}) == "\"text\"");

	// numeric keys are quoted, like Python
	assert(ctjson::dumps(std::map<int, std::string>{{1, "one"}}) == R"({"1": "one"})");

	// nesting
	assert(ctjson::dumps(std::map<std::string, std::vector<int>>{{"a", {1, 2}}}) == R"({"a": [1, 2]})");

	// indent: Python's exact layout
	assert(ctjson::dumps(std::map<std::string, std::vector<int>>{{"a", {1, 2}}}, 2)
		== "{\n  \"a\": [\n    1,\n    2\n  ]\n}");
	assert(ctjson::dumps(std::vector<int>{}, 2) == "[]"); // empties stay flat

	// sort_keys makes unordered containers deterministic
	{
		ctjson::dump_options options;
		options.sort_keys = true;
		std::unordered_map<std::string, int> unordered{{"b", 2}, {"a", 1}, {"c", 3}};
		assert(ctjson::dumps(unordered, options) == R"({"a": 1, "b": 2, "c": 3})");
	}

	// ensure_ascii escapes beyond ASCII, surrogate pairs included
	{
		ctjson::dump_options options;
		options.ensure_ascii = true;
		assert(ctjson::dumps("caf\xc3\xa9", options) == R"("caf\u00e9")");
		assert(ctjson::dumps("\xf0\x9f\x98\x80", options) == R"("\ud83d\ude00")");
	}
	assert(ctjson::dumps("caf\xc3\xa9") == "\"caf\xc3\xa9\""); // default passes UTF-8 through

#if CTLL_CNTTP_COMPILER_CHECK
	// parsed documents dump too; numbers keep their parsed spelling
	constexpr auto doc = ctjson::parse<R"({"n":[1,2.50],"s":"x"})">();
	assert(ctjson::dumps(doc) == R"({"n": [1, 2.50], "s": "x"})");
	assert(ctjson::dumps(doc, 2) == "{\n  \"n\": [\n    1,\n    2.50\n  ],\n  \"s\": \"x\"\n}");
#endif

	// the default= equivalent: ADL to_json
	assert(ctjson::dumps(geo::point{1.5, -2.0}) == R"({"x": 1.5, "y": -2.0})");
	assert(ctjson::dumps(std::vector<geo::point>{{1, 2}}) == R"([{"x": 1.0, "y": 2.0}])");

	// dump writes to a stream, like json.dump
	{
		std::ostringstream stream;
		ctjson::dump(std::vector<int>{1, 2}, stream);
		assert(stream.str() == "[1, 2]");
	}

#if CTLL_CNTTP_COMPILER_CHECK
	// loads is parse, for naming symmetry
	static_assert(ctjson::loads<"[1,2]">().get<1>().to<int>() == 2);
#endif

	std::puts("all dumps tests passed");
	return 0;
}

// dumps is constexpr: with C++20 constexpr std::string/std::vector
// support, whole encodings can be static_asserted - floats included,
// thanks to the built-in constexpr Dragon4
#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L \
 && defined(__cpp_lib_constexpr_vector) && __cpp_lib_constexpr_vector >= 201907L \
 && defined(__cpp_lib_bit_cast)

static_assert(ctjson::dumps(42) == "42");
static_assert(ctjson::dumps(true) == "true");
static_assert(ctjson::dumps(nullptr) == "null");
static_assert(ctjson::dumps("hi\n") == "\"hi\\n\"");
static_assert(ctjson::dumps(std::vector<int>{1, 2, 3}) == "[1, 2, 3]");
static_assert(ctjson::dumps(std::tuple{1, "a"}, 2) == "[\n  1,\n  \"a\"\n]");

// floating point at compile time, matching Python's repr exactly
static_assert(ctjson::dumps(1.0) == "1.0");
static_assert(ctjson::dumps(-0.0) == "-0.0");
static_assert(ctjson::dumps(0.1) == "0.1");
static_assert(ctjson::dumps(2.5) == "2.5");
static_assert(ctjson::dumps(0.30000000000000004) == "0.30000000000000004");
static_assert(ctjson::dumps(1e-4) == "0.0001");
static_assert(ctjson::dumps(1e-5) == "1e-05");   // Python's sci threshold
static_assert(ctjson::dumps(1e15) == "1000000000000000.0");
static_assert(ctjson::dumps(1e16) == "1e+16");
static_assert(ctjson::dumps(5e-324) == "5e-324"); // smallest denormal
static_assert(ctjson::dumps(1.7976931348623157e308) == "1.7976931348623157e+308");
static_assert(ctjson::dumps(3.141592653589793) == "3.141592653589793");

#if CTLL_CNTTP_COMPILER_CHECK
// a parsed document dumps inside a constant expression too, sort_keys
// included (std::map itself is not constexpr, but documents are)
static_assert(ctjson::dumps(ctjson::parse<R"({"n": [1, 2.50]})">()) == R"({"n": [1, 2.50]})");
static_assert([] {
	ctjson::dump_options options;
	options.sort_keys = true;
	return ctjson::dumps(ctjson::parse<R"({"b":2,"a":1})">(), options) == R"({"a": 1, "b": 2})";
}());
#endif

#endif
