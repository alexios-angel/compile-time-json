#ifndef CTJSON__HPP
#define CTJSON__HPP

#include "ctlark.hpp"
#include "ctjson/grammar.hpp"
#include "ctjson/types.hpp"
#include "ctjson/bind.hpp"
#include "ctjson/serialize.hpp"
#include "ctjson/views.hpp"
#include "ctjson/dumps.hpp"
#include "ctjson/load.hpp"

// ctjson: compile-time JSON.
//
//   constexpr auto doc = ctjson::parse<R"({"name":"Hana","tags":[1,2,3]})">();
//   static_assert(doc.get<"name">() == "Hana");
//   static_assert(doc.get<"tags">().get<1>().to<int>() == 2);
//   static_assert(ctjson::is_valid<"[1,2,3]">);
//
// The document is parsed while your code compiles - malformed JSON is a
// compile error (or `false` from is_valid) - and the result is a TYPE
// whose accessors are all constexpr. The grammar layer is ctlark
// (compile-time Lark): the JSON grammar is a lark grammar string
// (grammar.hpp), parsed and compiled to tables at compile time, and
// the document goes through ctlark's constexpr Earley parser before
// bind.hpp lowers the tree into the document types.

namespace ctjson {

#if CTLL_CNTTP_COMPILER_CHECK
#define CTJSON_STRING_INPUT ctll::fixed_string
#else
// C++17: pass a constexpr ctll::fixed_string variable with linkage
#define CTJSON_STRING_INPUT const auto &
#endif

namespace detail {

// grammar validity is a given (static_assert in grammar.hpp); input
// validity is the parse plus the binder's surrogate checks
template <CTJSON_STRING_INPUT input> constexpr bool valid_document() noexcept {
	if constexpr (!ctlark::is_valid<json_grammar, input, json_start>) {
		return false;
	} else {
		return bind<decltype(ctlark::parse<json_grammar, input, json_start>())>::ok;
	}
}

} // namespace detail

// does the input parse as JSON?
CTLL_EXPORT template <CTJSON_STRING_INPUT input> constexpr bool is_valid =
	detail::valid_document<input>();

// parse the input into its document value; invalid JSON fails to compile
CTLL_EXPORT template <CTJSON_STRING_INPUT input> constexpr auto parse() noexcept {
	static_assert(is_valid<input>, "ctjson: the input is not valid JSON");
	if constexpr (is_valid<input>) {
		using bound = detail::bind<decltype(ctlark::parse<detail::json_grammar, input, detail::json_start>())>;
		return typename bound::type{};
	} else {
		return null{};
	}
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
