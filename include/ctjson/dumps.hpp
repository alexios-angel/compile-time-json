#ifndef CTJSON__DUMPS__HPP
#define CTJSON__DUMPS__HPP

#include "types.hpp"
#ifndef CTJSON_IN_A_MODULE
#include <algorithm>
#include <cstring>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#if defined(__cpp_lib_bit_cast)
#include <bit>
#endif
#endif

// A runtime encoder in the style of Python's json module:
//
//   ctjson::dumps(std::map<std::string, std::vector<int>>{{"a", {1, 2}}})
//       == R"({"a": [1, 2]})"                       // Python's separators
//   ctjson::dumps(value, 2)                          // indent=2 pretty print
//   ctjson::dumps(value, {.indent=2, .sort_keys=true, .ensure_ascii=true})
//   ctjson::dump(value, stream, ...)                 // like json.dump
//
// What Python's encoder accepts maps to C++ like this:
//
//   dict  -> any map-like container (keys: string-like, or arithmetic,
//            which are quoted like Python does for int/float keys)
//   list  -> any iterable container, std::pair, std::tuple
//   str   -> std::string, std::string_view, const char *, char
//   int   -> integral types        float -> floating point types
//   None  -> nullptr, an empty std::optional
//   plus: std::variant (the active alternative), the ctjson document
//   types themselves (numbers keep their parsed spelling), and any type
//   with an ADL-findable to_json(value) returning something dumpable -
//   the equivalent of Python's default= hook.
//
// dumps is constexpr: with C++20 library support (constexpr std::string
// and std::vector) whole encodings can be static_asserted. To make that
// possible - and to match Python bit for bit - floating point values are
// rendered by a built-in constexpr Dragon4 (shortest round-tripping
// digits, exact big-integer arithmetic) and formatted with float.repr's
// rule: scientific only below 1e-4 or at 1e16 and above, and floats stay
// visibly floats (1.0). NaN and infinities render as
// NaN/Infinity/-Infinity exactly like Python's default allow_nan=True.
// dump() targets a std::ostream and is therefore runtime-only.
//
// One divergence from Python, on purpose: ensure_ascii defaults to false
// (UTF-8 passes through; switch it on for \uXXXX output, surrogate pairs
// included).

// dumps builds std::strings, so it is constexpr exactly where the
// standard library's constexpr string/vector support exists (C++20)
#if defined(__cpp_lib_constexpr_string) && __cpp_lib_constexpr_string >= 201907L \
 && defined(__cpp_lib_constexpr_vector) && __cpp_lib_constexpr_vector >= 201907L
#define CTJSON_DUMPS_CONSTEXPR constexpr
#else
#define CTJSON_DUMPS_CONSTEXPR
#endif

namespace ctjson {

CTLL_EXPORT struct dump_options {
	int indent = -1;          // negative: one line with ", " separators
	bool sort_keys = false;
	bool ensure_ascii = false;
};

namespace detail {

template <typename T, typename = void> struct is_iterable: std::false_type { };
template <typename T> struct is_iterable<T, std::void_t<decltype(std::begin(std::declval<const T &>())), decltype(std::end(std::declval<const T &>()))>>: std::true_type { };

template <typename T, typename = void> struct is_map_like: std::false_type { };
template <typename T> struct is_map_like<T, std::void_t<typename T::key_type, typename T::mapped_type, decltype(std::begin(std::declval<const T &>()))>>: std::true_type { };

template <typename T, typename = void> struct is_tuple_like: std::false_type { };
template <typename T> struct is_tuple_like<T, std::void_t<decltype(std::tuple_size<T>::value)>>: std::true_type { };

template <typename T> struct is_optional: std::false_type { };
template <typename T> struct is_optional<std::optional<T>>: std::true_type { };

template <typename T> struct is_variant: std::false_type { };
template <typename... Ts> struct is_variant<std::variant<Ts...>>: std::true_type { };

template <typename T, typename = void> struct is_document: std::false_type { };
template <typename T> struct is_document<T, std::void_t<decltype(T::type)>>: std::is_same<std::remove_cv_t<decltype(T::type)>, kind> { };

template <typename T, typename = void> struct has_adl_to_json: std::false_type { };
template <typename T> struct has_adl_to_json<T, std::void_t<decltype(to_json(std::declval<const T &>()))>>: std::true_type { };

template <typename> inline constexpr bool not_dumpable = false;

// --- shortest round-tripping float formatting (Dragon4)
//
// Exact big-integer arithmetic over the scaled value and its half-ulp
// boundaries produces the fewest digits that parse back to the same
// double, entirely within constant evaluation. 1280 bits comfortably
// hold every intermediate for IEEE double.

constexpr unsigned long long double_bits(double value) noexcept {
#if defined(__cpp_lib_bit_cast)
	return std::bit_cast<unsigned long long>(value);
#elif defined(__has_builtin) && __has_builtin(__builtin_bit_cast)
	return __builtin_bit_cast(unsigned long long, value);
#else
	// runtime-only fallback for old toolchains
	unsigned long long result = 0;
	std::memcpy(&result, &value, sizeof(result));
	return result;
#endif
}

struct bignum {
	unsigned int limbs[40]{};
	int count = 0;

	constexpr bignum() = default;

	constexpr bignum(unsigned long long value) {
		while (value != 0) {
			limbs[count++] = static_cast<unsigned int>(value & 0xFFFFFFFFu);
			value >>= 32;
		}
	}

	constexpr bool is_zero() const noexcept {
		return count == 0;
	}

	constexpr void shift_left(int bits) {
		const int limb_shift = bits / 32;
		const int bit_shift = bits % 32;
		if (count == 0) {
			return;
		}
		for (int i = count + limb_shift; i >= 0; --i) {
			unsigned long long assembled = 0;
			if (i - limb_shift >= 0 && i - limb_shift < count) {
				assembled = static_cast<unsigned long long>(limbs[i - limb_shift]) << bit_shift;
			}
			if (bit_shift != 0 && i - limb_shift - 1 >= 0 && i - limb_shift - 1 < count) {
				assembled |= limbs[i - limb_shift - 1] >> (32 - bit_shift);
			}
			if (i < 40) {
				limbs[i] = static_cast<unsigned int>(assembled & 0xFFFFFFFFu);
			}
		}
		count += limb_shift + 1;
		if (count > 40) {
			count = 40;
		}
		while (count > 0 && limbs[count - 1] == 0) {
			--count;
		}
	}

	constexpr void multiply_small(unsigned int factor) {
		unsigned long long carry = 0;
		for (int i = 0; i < count; ++i) {
			carry += static_cast<unsigned long long>(limbs[i]) * factor;
			limbs[i] = static_cast<unsigned int>(carry & 0xFFFFFFFFu);
			carry >>= 32;
		}
		while (carry != 0 && count < 40) {
			limbs[count++] = static_cast<unsigned int>(carry & 0xFFFFFFFFu);
			carry >>= 32;
		}
	}

	friend constexpr int compare(const bignum & lhs, const bignum & rhs) noexcept {
		if (lhs.count != rhs.count) {
			return lhs.count < rhs.count ? -1 : 1;
		}
		for (int i = lhs.count - 1; i >= 0; --i) {
			if (lhs.limbs[i] != rhs.limbs[i]) {
				return lhs.limbs[i] < rhs.limbs[i] ? -1 : 1;
			}
		}
		return 0;
	}

	friend constexpr bignum add(const bignum & lhs, const bignum & rhs) {
		bignum result;
		unsigned long long carry = 0;
		const int limit = lhs.count > rhs.count ? lhs.count : rhs.count;
		for (int i = 0; i < limit; ++i) {
			carry += (i < lhs.count ? lhs.limbs[i] : 0u);
			carry += (i < rhs.count ? rhs.limbs[i] : 0u);
			result.limbs[i] = static_cast<unsigned int>(carry & 0xFFFFFFFFu);
			carry >>= 32;
		}
		result.count = limit;
		if (carry != 0 && result.count < 40) {
			result.limbs[result.count++] = static_cast<unsigned int>(carry);
		}
		return result;
	}

	// requires *this >= rhs
	constexpr void subtract(const bignum & rhs) {
		long long borrow = 0;
		for (int i = 0; i < count; ++i) {
			long long diff = static_cast<long long>(limbs[i]) - (i < rhs.count ? rhs.limbs[i] : 0u) - borrow;
			borrow = diff < 0 ? 1 : 0;
			limbs[i] = static_cast<unsigned int>(diff + (borrow << 32));
		}
		while (count > 0 && limbs[count - 1] == 0) {
			--count;
		}
	}
};

struct decimal_digits {
	char digits[20]{};
	int length = 0;
	int point = 0; // value = 0.digits x 10^point
};

// shortest digits for a positive, finite double (Burger & Dybvig style)
constexpr decimal_digits shortest_digits(unsigned long long mantissa, int exponent2, bool boundary_is_closer) {
	const bool even = (mantissa & 1) == 0;

	bignum numerator;   // value = numerator / denominator
	bignum denominator;
	bignum margin_high; // half-gap up, over the same denominator
	bignum margin_low;  // half-gap down

	if (exponent2 >= 0) {
		if (!boundary_is_closer) {
			numerator = bignum(mantissa);
			numerator.shift_left(exponent2 + 1);
			denominator = bignum(2);
			margin_high = bignum(1);
			margin_high.shift_left(exponent2);
			margin_low = margin_high;
		} else {
			numerator = bignum(mantissa);
			numerator.shift_left(exponent2 + 2);
			denominator = bignum(4);
			margin_high = bignum(1);
			margin_high.shift_left(exponent2 + 1);
			margin_low = bignum(1);
			margin_low.shift_left(exponent2);
		}
	} else {
		if (!boundary_is_closer) {
			numerator = bignum(mantissa);
			numerator.shift_left(1);
			denominator = bignum(1);
			denominator.shift_left(1 - exponent2);
			margin_high = bignum(1);
			margin_low = bignum(1);
		} else {
			numerator = bignum(mantissa);
			numerator.shift_left(2);
			denominator = bignum(1);
			denominator.shift_left(2 - exponent2);
			margin_high = bignum(2);
			margin_low = bignum(1);
		}
	}

	decimal_digits result;

	// scale so that the first extracted digit is the first significant one
	const auto high_hits = [&](const bignum & sum, const bignum & against) {
		const int c = compare(sum, against);
		return even ? c >= 0 : c > 0;
	};
	while (high_hits(add(numerator, margin_high), denominator)) {
		denominator.multiply_small(10);
		++result.point;
	}
	for (;;) {
		bignum scaled = add(numerator, margin_high);
		scaled.multiply_small(10);
		const int c = compare(scaled, denominator);
		if (even ? c <= 0 : c < 0) {
			numerator.multiply_small(10);
			margin_high.multiply_small(10);
			margin_low.multiply_small(10);
			--result.point;
		} else {
			break;
		}
	}

	// digit loop: emit until the value is inside both half-gap bounds
	for (;;) {
		numerator.multiply_small(10);
		margin_high.multiply_small(10);
		margin_low.multiply_small(10);

		int digit = 0;
		while (compare(numerator, denominator) >= 0) {
			numerator.subtract(denominator);
			++digit;
		}

		const int low_compare = compare(numerator, margin_low);
		const bool low = even ? low_compare <= 0 : low_compare < 0;
		const bool high = high_hits(add(numerator, margin_high), denominator);

		if (!low && !high) {
			result.digits[result.length++] = static_cast<char>('0' + digit);
			continue;
		}

		if (low && !high) {
			// round down
		} else if (high && !low) {
			++digit; // round up
		} else {
			// both bounds hit: round to nearest, ties to even digit
			bignum doubled = numerator;
			doubled.multiply_small(2);
			const int c = compare(doubled, denominator);
			if (c > 0 || (c == 0 && (digit & 1) != 0)) {
				++digit;
			}
		}
		result.digits[result.length++] = static_cast<char>('0' + digit);
		break;
	}

	// a final round-up may carry through stored digits
	int index = result.length - 1;
	while (index >= 0 && result.digits[index] == '9' + 1) {
		result.digits[index] = '0';
		if (index == 0) {
			// 999... became 1000...: one leading digit, exponent bump
			result.digits[0] = '1';
			result.length = 1;
			++result.point;
			return result;
		}
		++result.digits[index - 1];
		--index;
	}
	while (result.length > 1 && result.digits[result.length - 1] == '0') {
		--result.length;
	}
	return result;
}

template <typename> inline constexpr bool no_constexpr_float_path = false;

struct dumper {
	std::string out;
	dump_options options;

	CTJSON_DUMPS_CONSTEXPR bool pretty() const noexcept {
		return options.indent >= 0;
	}
	CTJSON_DUMPS_CONSTEXPR void newline(int depth) {
		out += '\n';
		out.append(static_cast<size_t>(options.indent) * static_cast<size_t>(depth), ' ');
	}
	CTJSON_DUMPS_CONSTEXPR void item_separator(int depth) {
		if (pretty()) {
			out += ',';
			newline(depth);
		} else {
			out += ", ";
		}
	}

	// --- strings

	CTJSON_DUMPS_CONSTEXPR void escape_unit(char32_t code_point) {
		constexpr char hex[] = "0123456789abcdef";
		const char buffer[7]{'\\', 'u', hex[(code_point >> 12) & 0xF], hex[(code_point >> 8) & 0xF], hex[(code_point >> 4) & 0xF], hex[code_point & 0xF], 0};
		out.append(buffer, 6);
	}

	CTJSON_DUMPS_CONSTEXPR void write_string(std::string_view text) {
		out += '"';
		for (size_t i = 0; i < text.size(); ++i) {
			const char c = text[i];
			const auto byte = static_cast<unsigned char>(c);
			switch (c) {
				case '"': out += "\\\""; continue;
				case '\\': out += "\\\\"; continue;
				case '\b': out += "\\b"; continue;
				case '\f': out += "\\f"; continue;
				case '\n': out += "\\n"; continue;
				case '\r': out += "\\r"; continue;
				case '\t': out += "\\t"; continue;
				default: break;
			}
			if (byte < 0x20) {
				escape_unit(byte);
			} else if (byte < 0x80 || !options.ensure_ascii) {
				out += c;
			} else {
				// ensure_ascii: decode the UTF-8 sequence and emit \uXXXX
				// (a surrogate pair above the BMP); a byte that is not
				// valid UTF-8 is escaped on its own
				size_t continuation = byte >= 0xF0 ? 3 : byte >= 0xE0 ? 2 : byte >= 0xC0 ? 1 : 0;
				char32_t code_point = byte & (byte >= 0xF0 ? 0x07 : byte >= 0xE0 ? 0x0F : byte >= 0xC0 ? 0x1F : 0xFF);
				bool valid = continuation != 0 && i + continuation < text.size();
				for (size_t k = 1; valid && k <= continuation; ++k) {
					const auto follow = static_cast<unsigned char>(text[i + k]);
					if ((follow & 0xC0) != 0x80) {
						valid = false;
					} else {
						code_point = (code_point << 6) | (follow & 0x3F);
					}
				}
				if (!valid) {
					escape_unit(byte);
				} else if (code_point < 0x10000) {
					escape_unit(code_point);
					i += continuation;
				} else {
					escape_unit(static_cast<char32_t>(0xD800 + ((code_point - 0x10000) >> 10)));
					escape_unit(static_cast<char32_t>(0xDC00 + ((code_point - 0x10000) & 0x3FF)));
					i += continuation;
				}
			}
		}
		out += '"';
	}

	// --- numbers

	template <typename T> CTJSON_DUMPS_CONSTEXPR void write_integer(T value) {
		char buffer[24]{};
		int length = 0;
		auto magnitude = static_cast<unsigned long long>(value);
		if constexpr (std::is_signed_v<T>) {
			if (value < 0) {
				out += '-';
				magnitude = 0ull - magnitude;
			}
		}
		do {
			buffer[length++] = static_cast<char>('0' + magnitude % 10);
			magnitude /= 10;
		} while (magnitude != 0);
		while (length > 0) {
			out += buffer[--length];
		}
	}

	CTJSON_DUMPS_CONSTEXPR void write_floating(double value) {
		const unsigned long long bits = double_bits(value);
		const bool negative = (bits >> 63) != 0;
		const auto biased_exponent = static_cast<int>((bits >> 52) & 0x7FF);
		const unsigned long long fraction = bits & 0xFFFFFFFFFFFFFull;

		if (biased_exponent == 0x7FF) {
			// like Python's allow_nan=True
			if (fraction != 0) {
				out += "NaN";
			} else {
				out += negative ? "-Infinity" : "Infinity";
			}
			return;
		}
		if (negative) {
			out += '-';
		}
		if (biased_exponent == 0 && fraction == 0) {
			out += "0.0";
			return;
		}

		unsigned long long mantissa = fraction;
		int exponent2 = 0;
		bool boundary_is_closer = false;
		if (biased_exponent == 0) {
			exponent2 = 1 - 1075; // denormal
		} else {
			mantissa |= 1ull << 52;
			exponent2 = biased_exponent - 1075;
			// the gap below is halved right above a power of two
			boundary_is_closer = fraction == 0 && biased_exponent > 1;
		}

		const decimal_digits d = shortest_digits(mantissa, exponent2, boundary_is_closer);

		// Python repr's formatting: scientific only below 1e-4 and from
		// 1e16 up; fixed floats keep a visible ".0"
		const int leading_exponent = d.point - 1;
		if (leading_exponent < -4 || leading_exponent >= 16) {
			out += d.digits[0];
			if (d.length > 1) {
				out += '.';
				out.append(d.digits + 1, static_cast<size_t>(d.length - 1));
			}
			out += 'e';
			out += leading_exponent < 0 ? '-' : '+';
			const int magnitude = leading_exponent < 0 ? -leading_exponent : leading_exponent;
			if (magnitude < 10) {
				out += '0';
			}
			write_integer(magnitude);
		} else if (d.point <= 0) {
			out += "0.";
			out.append(static_cast<size_t>(-d.point), '0');
			out.append(d.digits, static_cast<size_t>(d.length));
		} else if (d.point >= d.length) {
			out.append(d.digits, static_cast<size_t>(d.length));
			out.append(static_cast<size_t>(d.point - d.length), '0');
			out += ".0";
		} else {
			out.append(d.digits, static_cast<size_t>(d.point));
			out += '.';
			out.append(d.digits + d.point, static_cast<size_t>(d.length - d.point));
		}
	}

	// --- the object/array shells (used by containers and documents)

	template <typename WriteItems> CTJSON_DUMPS_CONSTEXPR void write_array_shell(size_t count, int depth, WriteItems && write_items) {
		if (count == 0) {
			out += "[]";
			return;
		}
		out += '[';
		if (pretty()) {
			newline(depth + 1);
		}
		write_items();
		if (pretty()) {
			newline(depth);
		}
		out += ']';
	}

	template <typename WriteItems> CTJSON_DUMPS_CONSTEXPR void write_object_shell(size_t count, int depth, WriteItems && write_items) {
		if (count == 0) {
			out += "{}";
			return;
		}
		out += '{';
		if (pretty()) {
			newline(depth + 1);
		}
		write_items();
		if (pretty()) {
			newline(depth);
		}
		out += '}';
	}

	// members arrive as already-rendered (key token, value) strings so
	// sort_keys can reorder them regardless of the source container
	CTJSON_DUMPS_CONSTEXPR void write_members(std::vector<std::pair<std::string, std::string>> && members, int depth) {
		if (options.sort_keys) {
			std::sort(members.begin(), members.end(), [](const auto & a, const auto & b) { return a.first < b.first; });
		}
		write_object_shell(members.size(), depth, [&] {
			bool first = true;
			for (const auto & entry : members) {
				if (!first) {
					item_separator(depth + 1);
				}
				first = false;
				out += entry.first;
				out += ": ";
				out += entry.second;
			}
		});
	}

	template <typename Key> CTJSON_DUMPS_CONSTEXPR std::string render_key(const Key & key) {
		dumper sub{{}, options};
		if constexpr (std::is_convertible_v<const Key &, std::string_view>) {
			sub.write_string(std::string_view(key));
		} else if constexpr (std::is_same_v<std::remove_cv_t<Key>, char>) {
			sub.write_string(std::string_view(&key, 1));
		} else if constexpr (std::is_arithmetic_v<Key> && !std::is_same_v<std::remove_cv_t<Key>, bool>) {
			// Python quotes numeric keys: {1: "x"} dumps as {"1": "x"}
			sub.out += '"';
			sub.value(key, 0);
			sub.out += '"';
		} else {
			static_assert(not_dumpable<Key>, "ctjson::dumps: object keys must be strings or numbers");
		}
		return std::move(sub.out);
	}

	template <typename T> CTJSON_DUMPS_CONSTEXPR std::string render_value(const T & value_to_render, int depth) {
		dumper sub{{}, options};
		sub.value(value_to_render, depth);
		return std::move(sub.out);
	}

	// --- ctjson document values

	template <typename Node> CTJSON_DUMPS_CONSTEXPR void document(Node node, int depth) {
		if constexpr (Node::type == kind::object) {
			std::vector<std::pair<std::string, std::string>> members;
			for_each(node, [&](auto key, auto member_value) {
				members.emplace_back(render_key(key.view()), render_value(member_value, depth + 1));
			});
			write_members(std::move(members), depth);
		} else if constexpr (Node::type == kind::array) {
			write_array_shell(Node::size(), depth, [&] {
				bool first = true;
				for_each(node, [&](auto element) {
					if (!first) {
						item_separator(depth + 1);
					}
					first = false;
					value(element, depth + 1);
				});
			});
		} else if constexpr (Node::type == kind::string) {
			write_string(Node::view());
		} else if constexpr (Node::type == kind::number) {
			out += Node::view(); // the spelling it was parsed with
		} else if constexpr (Node::type == kind::boolean) {
			out += Node::value ? "true" : "false";
		} else {
			out += "null";
		}
	}

	// --- the dispatcher

	template <typename T> CTJSON_DUMPS_CONSTEXPR void value(const T & v, int depth) {
		using D = std::remove_cv_t<std::remove_reference_t<T>>;
		if constexpr (has_adl_to_json<D>::value) {
			value(to_json(v), depth);
		} else if constexpr (is_document<D>::value) {
			document(v, depth);
		} else if constexpr (std::is_same_v<D, std::nullptr_t>) {
			out += "null";
		} else if constexpr (std::is_same_v<D, bool>) {
			out += v ? "true" : "false";
		} else if constexpr (std::is_same_v<D, char>) {
			write_string(std::string_view(&v, 1));
		} else if constexpr (std::is_integral_v<D>) {
			write_integer(v);
		} else if constexpr (std::is_floating_point_v<D>) {
			write_floating(static_cast<double>(v));
		} else if constexpr (std::is_convertible_v<const D &, std::string_view>) {
			write_string(std::string_view(v));
		} else if constexpr (is_optional<D>::value) {
			if (v) {
				value(*v, depth);
			} else {
				out += "null";
			}
		} else if constexpr (is_variant<D>::value) {
			std::visit([&](const auto & alternative) { value(alternative, depth); }, v);
		} else if constexpr (is_map_like<D>::value) {
			std::vector<std::pair<std::string, std::string>> members;
			for (const auto & entry : v) {
				members.emplace_back(render_key(entry.first), render_value(entry.second, depth + 1));
			}
			write_members(std::move(members), depth);
		} else if constexpr (is_iterable<D>::value) {
			size_t count = 0;
			for (const auto & element : v) {
				(void)element;
				++count;
			}
			write_array_shell(count, depth, [&] {
				bool first = true;
				for (const auto & element : v) {
					if (!first) {
						item_separator(depth + 1);
					}
					first = false;
					value(element, depth + 1);
				}
			});
		} else if constexpr (is_tuple_like<D>::value) {
			write_array_shell(std::tuple_size<D>::value, depth, [&] {
				bool first = true;
				std::apply([&](const auto &... elements) {
					(((!first ? item_separator(depth + 1) : void()), first = false, value(elements, depth + 1)), ...);
				}, v);
			});
		} else {
			static_assert(not_dumpable<D>, "ctjson::dumps: this type is not JSON serializable; provide a to_json(const T &) found by ADL");
		}
	}
};

} // namespace detail

// like json.dumps: encode a value as a JSON string (constexpr with
// C++20 library support for constexpr std::string/std::vector)
CTLL_EXPORT template <typename T> CTJSON_DUMPS_CONSTEXPR std::string dumps(const T & value, dump_options options) {
	detail::dumper d{{}, options};
	d.value(value, 0);
	return std::move(d.out);
}

CTLL_EXPORT template <typename T> CTJSON_DUMPS_CONSTEXPR std::string dumps(const T & value) {
	return dumps(value, dump_options{});
}

CTLL_EXPORT template <typename T> CTJSON_DUMPS_CONSTEXPR std::string dumps(const T & value, int indent) {
	dump_options options;
	options.indent = indent;
	return dumps(value, options);
}

// like json.dump: encode into a stream (runtime: streams cannot be
// constant evaluated)
CTLL_EXPORT template <typename T> void dump(const T & value, std::ostream & stream, dump_options options) {
	stream << dumps(value, options);
}

CTLL_EXPORT template <typename T> void dump(const T & value, std::ostream & stream) {
	dump(value, stream, dump_options{});
}

CTLL_EXPORT template <typename T> void dump(const T & value, std::ostream & stream, int indent) {
	dump_options options;
	options.indent = indent;
	dump(value, stream, options);
}

} // namespace ctjson

#endif
