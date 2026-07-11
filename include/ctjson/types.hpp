#ifndef CTJSON__TYPES__HPP
#define CTJSON__TYPES__HPP

#include "../ctll/fixed_string.hpp"
#ifndef CTJSON_IN_A_MODULE
#include <cstddef>
#include <limits>
#include <string_view>
#include <type_traits>
#endif

// The document types a parse produces. The whole document is a TYPE -
// every scalar, key and nesting level is encoded in template
// parameters - so the values here are empty structs whose accessors are
// all constexpr and static.
//
// String content is stored as UTF-8 bytes (double-quote escapes,
// \uXXXX and \UXXXXXXXX included, are decoded during parsing); numbers
// keep their raw spelling - decimal, hex, octal, float or .inf/.nan -
// and convert on demand. Mapping keys are strings: JSON's tag
// resolution is applied to values, not keys, so `get<"true">()` finds
// the key spelled true.

namespace ctjson {

CTLL_EXPORT enum class kind {
	object,
	array,
	string,
	number,
	boolean,
	null
};

namespace detail {

// the conversion behind number::to and value_view::to: JSON
// schema spellings - sign, .inf/.nan, hex, octal, decimal with
// fraction and exponent - parsed from their text
template <typename T> constexpr T to_arithmetic(std::string_view text) noexcept {
		size_t i = 0;
		bool negative = false;
		if (text[0] == '+' || text[0] == '-') {
			negative = text[0] == '-';
			++i;
		}
		// .inf and .nan (the sign, if any, was already consumed)
		if (i < text.size() && text[i] == '.' && i + 1 < text.size()
		    && (text[i + 1] < '0' || text[i + 1] > '9')) {
			if constexpr (std::is_integral_v<T>) {
				return T{}; // no integral infinity; converting is the caller's choice
			} else {
				if (text[i + 1] == 'n' || text[i + 1] == 'N') {
					return std::numeric_limits<T>::quiet_NaN();
				}
				return negative ? -std::numeric_limits<T>::infinity()
				                : std::numeric_limits<T>::infinity();
			}
		}
		// hex and octal integers
		if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'o')) {
			const unsigned base = text[i + 1] == 'x' ? 16u : 8u;
			unsigned long long value = 0;
			for (size_t k = i + 2; k < text.size(); ++k) {
				const char c = text[k];
				unsigned digit = 0;
				if (c >= '0' && c <= '9') {
					digit = static_cast<unsigned>(c - '0');
				} else if (c >= 'a' && c <= 'f') {
					digit = static_cast<unsigned>(c - 'a') + 10u;
				} else {
					digit = static_cast<unsigned>(c - 'A') + 10u;
				}
				value = value * base + digit;
			}
			return static_cast<T>(negative ? -static_cast<long long>(value) : static_cast<long long>(value));
		}
		// decimal: mantissa digits, fraction shifting the exponent down
		unsigned long long mantissa = 0;
		int exponent10 = 0;
		for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
			mantissa = mantissa * 10 + static_cast<unsigned long long>(text[i] - '0');
		}
		if (i < text.size() && text[i] == '.') {
			++i;
			for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
				mantissa = mantissa * 10 + static_cast<unsigned long long>(text[i] - '0');
				--exponent10;
			}
		}
		if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
			++i;
			bool exp_negative = false;
			if (text[i] == '+' || text[i] == '-') {
				exp_negative = text[i] == '-';
				++i;
			}
			int exp_value = 0;
			for (; i < text.size(); ++i) {
				exp_value = exp_value * 10 + (text[i] - '0');
			}
			exponent10 += exp_negative ? -exp_value : exp_value;
		}

		if constexpr (std::is_integral_v<T>) {
			long long value = static_cast<long long>(mantissa);
			for (; exponent10 > 0; --exponent10) {
				value *= 10;
			}
			for (; exponent10 < 0; ++exponent10) {
				value /= 10; // truncates fractions, like a cast would
			}
			return static_cast<T>(negative ? -value : value);
		} else {
			long double value = static_cast<long double>(mantissa);
			for (; exponent10 > 0; --exponent10) {
				value *= 10.0L;
			}
			for (; exponent10 < 0; ++exponent10) {
				value /= 10.0L;
			}
			return static_cast<T>(negative ? -value : value);
		}
	}

} // namespace detail

// --- string

CTLL_EXPORT template <auto... Chars> struct string {
	static constexpr kind type = kind::string;

	// null-terminated so c_str()/data() work as C strings; size() excludes it
	static constexpr char storage[sizeof...(Chars) + 1]{static_cast<char>(Chars)..., '\0'};

	static constexpr const char * c_str() noexcept {
		return storage;
	}

	static constexpr size_t size() noexcept {
		return sizeof...(Chars);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Chars) == 0;
	}
	static constexpr std::string_view view() noexcept {
		return std::string_view{storage, sizeof...(Chars)};
	}
	constexpr operator std::string_view() const noexcept {
		return view();
	}
	template <auto... Rhs> constexpr bool operator==(string<Rhs...>) const noexcept {
		return view() == string<Rhs...>::view();
	}
	friend constexpr bool operator==(string, std::string_view rhs) noexcept {
		return view() == rhs;
	}
	friend constexpr bool operator==(std::string_view lhs, string) noexcept {
		return lhs == view();
	}
};

// --- number (raw spelling, converted on demand; JSON schema)

CTLL_EXPORT template <auto... Chars> struct number {
	static constexpr kind type = kind::number;

	// null-terminated so c_str() works as a C string; view() excludes it
	static constexpr char storage[sizeof...(Chars) + 1]{static_cast<char>(Chars)..., '\0'};

	static constexpr const char * c_str() noexcept {
		return storage;
	}

	static constexpr std::string_view view() noexcept {
		return std::string_view{storage, sizeof...(Chars)};
	}

	static constexpr bool is_integer() noexcept {
		constexpr std::string_view text = view();
		// hex and octal are integers whatever letters they contain
		if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'o')) {
			return true;
		}
		for (const char c : text) {
			if (c == '.' || c == 'e' || c == 'E') {
				return false;
			}
		}
		return true;
	}

	template <typename T> static constexpr T to() noexcept {
		return detail::to_arithmetic<T>(view());

	}
};

// --- boolean and null

CTLL_EXPORT template <bool Value> struct boolean {
	static constexpr kind type = kind::boolean;
	static constexpr bool value = Value;
	constexpr operator bool() const noexcept {
		return Value;
	}
};

CTLL_EXPORT struct null {
	static constexpr kind type = kind::null;
};

// --- uniform views: the type behind doc["key"] and seq[1]
//
// The elements of one container all have different types, so a runtime
// key or index cannot select an element ITSELF - operator[] hands back
// a VIEW: the kind, the text, and spans over the children, everything
// pointing into static storage (views.hpp builds it). Views chain -
// doc["users"][0]["name"] - a miss is a null view rather than an error
// (the null-object pattern of any dynamic API), and it is all still
// constexpr. get<...>() remains the typed accessor.

CTLL_EXPORT struct member_view;
CTLL_EXPORT struct member_range;

CTLL_EXPORT struct value_view {
	ctjson::kind type = kind::null;
	std::string_view text{"null"};
	const member_view * member_data = nullptr;
	const value_view * element_data = nullptr;
	size_t child_count = 0;

	constexpr size_t size() const noexcept {
		return child_count;
	}
	constexpr bool empty() const noexcept {
		return child_count == 0;
	}
	constexpr std::string_view view() const noexcept {
		return text;
	}

	// numbers convert on demand, exactly like the typed number::to
	template <typename T> constexpr T to() const noexcept {
		return detail::to_arithmetic<T>(text);
	}

	constexpr value_view operator[](std::string_view key) const noexcept; // objects
	constexpr value_view operator[](size_t index) const noexcept;        // arrays (objects: positional)
	constexpr bool contains(std::string_view key) const noexcept;
	constexpr member_range items() const noexcept; // a object view's key/value pairs

	// the elements of a array view (use items() for a object view)
	constexpr const value_view * begin() const noexcept {
		return element_data;
	}
	constexpr const value_view * end() const noexcept {
		return element_data + (element_data ? child_count : 0);
	}

	friend constexpr bool operator==(value_view lhs, std::string_view rhs) noexcept {
		return lhs.text == rhs;
	}
	friend constexpr bool operator==(std::string_view lhs, value_view rhs) noexcept {
		return lhs == rhs.text;
	}
#if __cplusplus < 202002L
	friend constexpr bool operator!=(value_view lhs, std::string_view rhs) noexcept {
		return lhs.text != rhs;
	}
	friend constexpr bool operator!=(std::string_view lhs, value_view rhs) noexcept {
		return lhs != rhs.text;
	}
#endif
};

CTLL_EXPORT struct member_view {
	std::string_view key;
	value_view value;
};

CTLL_EXPORT struct member_range {
	const member_view * from = nullptr;
	const member_view * upto = nullptr;
	constexpr const member_view * begin() const noexcept {
		return from;
	}
	constexpr const member_view * end() const noexcept {
		return upto;
	}
	constexpr size_t size() const noexcept {
		return static_cast<size_t>(upto - from);
	}
	constexpr bool empty() const noexcept {
		return from == upto;
	}
};

constexpr value_view value_view::operator[](std::string_view key) const noexcept {
	if (type == kind::object) {
		for (size_t i = 0; i < child_count; ++i) {
			if (member_data[i].key == key) {
				return member_data[i].value;
			}
		}
	}
	return value_view{};
}

constexpr value_view value_view::operator[](size_t index) const noexcept {
	if (type == kind::array && index < child_count) {
		return element_data[index];
	}
	if (type == kind::object && index < child_count) {
		return member_data[index].value;
	}
	return value_view{};
}

constexpr bool value_view::contains(std::string_view key) const noexcept {
	if (type != kind::object) {
		return false;
	}
	for (size_t i = 0; i < child_count; ++i) {
		if (member_data[i].key == key) {
			return true;
		}
	}
	return false;
}

constexpr member_range value_view::items() const noexcept {
	return {member_data, member_data + (member_data ? child_count : 0)};
}

namespace detail {

// the view of one document node, out of static storage (views.hpp)
template <typename Node> constexpr value_view view_of() noexcept;

} // namespace detail

// --- array

CTLL_EXPORT template <typename... Values> struct array {
	static constexpr kind type = kind::array;

	static constexpr size_t size() noexcept {
		return sizeof...(Values);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Values) == 0;
	}

	template <size_t Index> static constexpr auto get() noexcept {
		static_assert(Index < sizeof...(Values), "ctjson: array index out of range");
		return nth<Index, Values...>();
	}

	// get, spelled with brackets: seq[1] descends into a uniform VIEW of
	// the element (chains and misses are null-safe; get<N>() stays typed)
	constexpr value_view operator[](size_t index) const noexcept {
		return detail::view_of<array>()[index];
	}

private:
	template <size_t Index, typename Head, typename... Tail> static constexpr auto nth() noexcept {
		if constexpr (Index == 0) {
			return Head{};
		} else {
			return nth<Index - 1, Tail...>();
		}
	}
};

// --- object

CTLL_EXPORT template <typename Key, typename Value> struct member {
	using key_type = Key;
	using value_type = Value;
};

CTLL_EXPORT template <typename... Members> struct object {
	static constexpr kind type = kind::object;

	static constexpr size_t size() noexcept {
		return sizeof...(Members);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Members) == 0;
	}

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Name> static constexpr bool contains() noexcept {
		return (key_matches<Name, Members>() || ...);
	}

	// the value of the member with this key; a missing key is a
	// compile-time error (check contains<Name>() first when unsure)
	template <ctll::fixed_string Name> static constexpr auto get() noexcept {
		static_assert((key_matches<Name, Members>() || ...), "ctjson: no member with this key");
		return find<Name, Members...>();
	}
#else
	// C++17: the key is a ctll::fixed_string variable with linkage
	template <const auto & Name> static constexpr bool contains() noexcept {
		return (key_matches<Name, Members>() || ...);
	}
	template <const auto & Name> static constexpr auto get() noexcept {
		static_assert((key_matches<Name, Members>() || ...), "ctjson: no member with this key");
		return find<Name, Members...>();
	}
#endif

	// get, spelled with brackets: doc["key"] descends into a uniform VIEW
	// of the value (chains and misses are null-safe; get<"key">() stays
	// typed); an index is positional, like value<N>()
	constexpr value_view operator[](std::string_view key) const noexcept {
		return detail::view_of<object>()[key];
	}
	constexpr value_view operator[](size_t index) const noexcept {
		return detail::view_of<object>()[index];
	}

	// runtime keys; contains<"key">() is the typed twin
	constexpr bool contains(std::string_view key) const noexcept {
		return detail::view_of<object>().contains(key);
	}

	// positional access, for iterating members
	template <size_t Index> static constexpr auto key() noexcept {
		static_assert(Index < sizeof...(Members), "ctjson: member index out of range");
		return typename decltype(nth<Index, Members...>())::key_type{};
	}
	template <size_t Index> static constexpr auto value() noexcept {
		static_assert(Index < sizeof...(Members), "ctjson: member index out of range");
		return typename decltype(nth<Index, Members...>())::value_type{};
	}

private:
#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Name, typename Member> static constexpr bool key_matches() noexcept {
#else
	template <const auto & Name, typename Member> static constexpr bool key_matches() noexcept {
#endif
		constexpr auto key_view = Member::key_type::view();
		if (Name.size() != key_view.size()) {
			return false;
		}
		for (size_t i = 0; i < key_view.size(); ++i) {
			if (static_cast<char32_t>(static_cast<unsigned char>(key_view[i])) != Name[i]) {
				return false;
			}
		}
		return true;
	}

#if CTLL_CNTTP_COMPILER_CHECK
	template <ctll::fixed_string Name, typename Head, typename... Tail> static constexpr auto find() noexcept {
#else
	template <const auto & Name, typename Head, typename... Tail> static constexpr auto find() noexcept {
#endif
		if constexpr (key_matches<Name, Head>()) {
			return typename Head::value_type{};
		} else {
			return find<Name, Tail...>();
		}
	}

	template <size_t Index, typename Head, typename... Tail> static constexpr auto nth() noexcept {
		if constexpr (Index == 0) {
			return Head{};
		} else {
			return nth<Index - 1, Tail...>();
		}
	}
};

// compile-time iteration: the callable is invoked once per element
// (arrays) or once per key/value pair (objects), each with its own type
CTLL_EXPORT template <typename F, typename... Values> constexpr void for_each(array<Values...>, F && f) {
	(f(Values{}), ...);
}

CTLL_EXPORT template <typename F, typename... Members> constexpr void for_each(object<Members...>, F && f) {
	(f(typename Members::key_type{}, typename Members::value_type{}), ...);
}

} // namespace ctjson

#endif
