#ifndef CTJSON__DUMPS__HPP
#define CTJSON__DUMPS__HPP

#include "types.hpp"
#ifndef CTJSON_IN_A_MODULE
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
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
// Divergences from Python, on purpose: ensure_ascii defaults to false
// (UTF-8 passes through; switch it on for \uXXXX output, surrogate
// pairs included), and the result is a std::string. NaN and infinities
// render as NaN/Infinity/-Infinity exactly like Python's default
// allow_nan=True.

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

struct dumper {
	std::string out;
	dump_options options;

	bool pretty() const noexcept {
		return options.indent >= 0;
	}
	void newline(int depth) {
		out += '\n';
		out.append(static_cast<size_t>(options.indent) * static_cast<size_t>(depth), ' ');
	}
	void item_separator(int depth) {
		if (pretty()) {
			out += ',';
			newline(depth);
		} else {
			out += ", ";
		}
	}

	// --- strings

	void escape_unit(char32_t code_point) {
		constexpr char hex[] = "0123456789abcdef";
		char buffer[7]{'\\', 'u', hex[(code_point >> 12) & 0xF], hex[(code_point >> 8) & 0xF], hex[(code_point >> 4) & 0xF], hex[code_point & 0xF], 0};
		out += buffer;
	}

	void write_string(std::string_view text) {
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

	template <typename T> void write_integer(T value) {
		char buffer[32];
		const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
		out.append(buffer, result.ptr);
	}

	void write_floating(double value) {
		if (std::isnan(value)) {
			out += "NaN"; // like Python's allow_nan=True
			return;
		}
		if (std::isinf(value)) {
			out += value < 0 ? "-Infinity" : "Infinity";
			return;
		}
		char buffer[64];
		const char * begin = buffer;
		const char * end = buffer;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
		end = std::to_chars(buffer, buffer + sizeof(buffer), value).ptr;
#else
		end = buffer + std::snprintf(buffer, sizeof(buffer), "%.17g", value);
#endif
		out.append(begin, end);
		// Python's repr keeps floats visibly floats: 1.0, not 1
		bool looks_integral = true;
		for (const char * it = begin; it != end; ++it) {
			if (*it == '.' || *it == 'e' || *it == 'E') {
				looks_integral = false;
			}
		}
		if (looks_integral) {
			out += ".0";
		}
	}

	// --- the object/array shells (used by containers and documents)

	template <typename WriteItems> void write_array_shell(size_t count, int depth, WriteItems && write_items) {
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

	template <typename WriteItems> void write_object_shell(size_t count, int depth, WriteItems && write_items) {
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
	void write_members(std::vector<std::pair<std::string, std::string>> && members, int depth) {
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

	template <typename Key> std::string render_key(const Key & key) {
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

	template <typename T> std::string render_value(const T & value_to_render, int depth) {
		dumper sub{{}, options};
		sub.value(value_to_render, depth);
		return std::move(sub.out);
	}

	// --- ctjson document values

	template <typename Node> void document(Node node, int depth) {
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

	template <typename T> void value(const T & v, int depth) {
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

// like json.dumps: encode a value as a JSON string
CTLL_EXPORT template <typename T> std::string dumps(const T & value, dump_options options) {
	detail::dumper d{{}, options};
	d.value(value, 0);
	return std::move(d.out);
}

CTLL_EXPORT template <typename T> std::string dumps(const T & value) {
	return dumps(value, dump_options{});
}

CTLL_EXPORT template <typename T> std::string dumps(const T & value, int indent) {
	dump_options options;
	options.indent = indent;
	return dumps(value, options);
}

// like json.dump: encode into a stream
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
