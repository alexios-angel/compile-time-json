#ifndef CTJSON__VIEWS__HPP
#define CTJSON__VIEWS__HPP

#include "types.hpp"
#include "serialize.hpp"
#ifndef CTJSON_IN_A_MODULE
#include <array>
#include <cstddef>
#include <string_view>
#endif

// The static storage behind the uniform views (the value_view type
// itself lives in types.hpp, next to the operator[] that returns it).
// Every element of a array (every member of a object) has its own
// TYPE, so neither a runtime lookup nor an ordinary iterator can hand
// out elements themselves; what CAN be uniform is a view - the kind,
// the text, and spans over the children. One constexpr array per
// container type is materialized here, only when navigated or
// iterated, and each container's view points at its children's
// arrays, so views chain to any depth:
//
//   static_assert(doc["users"][0]["name"] == "hana");
//   for (const auto & m : doc) { ... m.key, m.value.type, m.value.text ... }
//   for (const auto & v : doc["tags"]) { ... v.text ... }
//   for (const auto & m : doc["license"].items()) { ... }
//
// Strings view their content, numbers their raw spelling, booleans
// and null their literals, and containers their flow-style
// serialization - dispatch on .type when the distinction matters. For
// type-preserving iteration (each element with its own accessors),
// for_each remains the tool.

namespace ctjson {

namespace detail {

template <typename... Values> struct array_views;
template <typename... Members> struct object_views;

template <typename Node> struct view_maker {
	static constexpr value_view make() noexcept {
		if constexpr (Node::type == kind::boolean) {
			return {kind::boolean, Node::value ? std::string_view{"true"} : std::string_view{"false"}};
		} else if constexpr (Node::type == kind::null) {
			return {kind::null, std::string_view{"null"}};
		} else {
			// strings view their content, numbers their spelling
			return {Node::type, Node::view()};
		}
	}
};
template <typename... Values> struct view_maker<array<Values...>> {
	static constexpr value_view make() noexcept {
		return {kind::array, ctjson::serialize(array<Values...>{}), nullptr,
		        array_views<Values...>::data.data(), sizeof...(Values)};
	}
};
template <typename... Members> struct view_maker<object<Members...>> {
	static constexpr value_view make() noexcept {
		return {kind::object, ctjson::serialize(object<Members...>{}),
		        object_views<Members...>::data.data(), nullptr, sizeof...(Members)};
	}
};

template <typename Node> constexpr value_view view_of() noexcept {
	return view_maker<Node>::make();
}

// one static array per container type, materialized only on use
template <typename... Values> struct array_views {
	static constexpr std::array<value_view, sizeof...(Values)> data{view_of<Values>()...};
};

template <typename... Members> struct object_views {
	static constexpr std::array<member_view, sizeof...(Members)> data{
	    member_view{Members::key_type::view(), view_of<typename Members::value_type>()}...};
};

} // namespace detail

// iteration over the typed containers: begin/end yield the same views,
// so range-for, algorithms and constexpr loops work on a document
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
