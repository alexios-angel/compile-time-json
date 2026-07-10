#ifndef CTJSON__HPP
#define CTJSON__HPP

#include "ctll/parser.hpp"
#include "ctjson/json.hpp"
#include "ctjson/types.hpp"
#include "ctjson/actions.hpp"
#include "ctjson/serialize.hpp"
#include "ctjson/dumps.hpp"

// ctjson: compile-time JSON.
//
//   constexpr auto doc = ctjson::parse<R"({"name":"Hana","tags":[1,2,3]})">();
//   static_assert(doc.get<"name">() == "Hana");
//   static_assert(doc.get<"tags">().get<1>().to<int>() == 2);
//   static_assert(ctjson::is_valid<"[1,2,3]">);
//
// The document is parsed while your code compiles - malformed JSON is a
// compile error (or `false` from is_valid) - and the result is a TYPE
// whose accessors are all constexpr. Built on CTLL, the compile-time
// LL(1) parser from the CTRE project.

namespace ctjson {

#if CTLL_CNTTP_COMPILER_CHECK
#define CTJSON_STRING_INPUT ctll::fixed_string
#else
// C++17: pass a constexpr ctll::fixed_string variable with linkage
#define CTJSON_STRING_INPUT const auto &
#endif

// does the input parse as JSON?
CTLL_EXPORT template <CTJSON_STRING_INPUT input> constexpr bool is_valid =
	ctll::parser<json, input, json_actions>::template correct_with<context<>>;

// parse the input into its document value; invalid JSON fails to compile
CTLL_EXPORT template <CTJSON_STRING_INPUT input> constexpr auto parse() noexcept {
#if CTLL_CNTTP_COMPILER_CHECK
	constexpr auto _input = input; // workaround for GCC 9 bug 88092
#else
	constexpr auto & _input = input; // C++17: the argument has linkage
#endif
	using parsed = typename ctll::parser<json, _input, json_actions>::template output<context<>>;
	static_assert(parsed(), "ctjson: the input is not valid JSON");
	return ctll::front(typename parsed::output_type::stack_type{});
}

// like json.loads, for symmetry with dumps: parse compile-time text
#if CTLL_CNTTP_COMPILER_CHECK
CTLL_EXPORT template <ctll::fixed_string input> constexpr auto loads() noexcept {
	return parse<input>();
}
#else
CTLL_EXPORT template <const auto & input> constexpr auto loads() noexcept {
	return parse<input>();
}
#endif

} // namespace ctjson

#endif
