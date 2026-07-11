#ifndef CTJSON__VIEWS__HPP
#define CTJSON__VIEWS__HPP

#include "types.hpp"
#include "serialize.hpp"
#ifndef CTJSON_IN_A_MODULE
#include <array>
#include <cstddef>
#include <string_view>
#endif

// Iterators over compile-time documents. Every element of an array
// (every member of an object) has its own TYPE, so no ordinary
// iterator can hand them out one by one; what CAN be uniform is a
// view - the kind plus the text behind it. begin/end yield exactly
// that, out of static storage, so range-for, standard algorithms and
// constexpr loops all work:
//
//   for (const auto & m : doc) { ... m.key, m.value.type, m.value.text ... }
//   for (const auto & v : doc.get<"tags">()) { ... v.text ... }
//
// Strings view their decoded content, numbers their raw spelling,
// booleans and null their literals, and nested containers their
// minified serialization - dispatch on .type when the distinction
// matters. For type-preserving iteration (each element with its own
// accessors), for_each remains the tool.

namespace ctjson {

CTLL_EXPORT struct value_view {
	ctjson::kind type;
	std::string_view text;
};

CTLL_EXPORT struct member_view {
	std::string_view key;
	value_view value;
};

namespace detail {

template <typename Node> constexpr value_view view_of() noexcept {
	if constexpr (Node::type == kind::boolean) {
		return {kind::boolean, Node::value ? std::string_view{"true"} : std::string_view{"false"}};
	} else if constexpr (Node::type == kind::null) {
		return {kind::null, std::string_view{"null"}};
	} else if constexpr (Node::type == kind::string || Node::type == kind::number) {
		return {Node::type, Node::view()};
	} else {
		return {Node::type, ctjson::serialize(Node{})};
	}
}

// one static array per container type, materialized only when iterated
template <typename... Values> struct array_views {
	static constexpr std::array<value_view, sizeof...(Values)> data{view_of<Values>()...};
};

template <typename... Members> struct object_views {
	static constexpr std::array<member_view, sizeof...(Members)> data{
	    member_view{Members::key_type::view(), view_of<typename Members::value_type>()}...};
};

} // namespace detail

CTLL_EXPORT template <typename... Values> constexpr const value_view * begin(array<Values...>) noexcept {
	return detail::array_views<Values...>::data.data();
}
CTLL_EXPORT template <typename... Values> constexpr const value_view * end(array<Values...>) noexcept {
	return detail::array_views<Values...>::data.data() + sizeof...(Values);
}

CTLL_EXPORT template <typename... Members> constexpr const member_view * begin(object<Members...>) noexcept {
	return detail::object_views<Members...>::data.data();
}
CTLL_EXPORT template <typename... Members> constexpr const member_view * end(object<Members...>) noexcept {
	return detail::object_views<Members...>::data.data() + sizeof...(Members);
}

} // namespace ctjson

#endif
