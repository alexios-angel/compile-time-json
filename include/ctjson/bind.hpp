#ifndef CTJSON__BIND__HPP
#define CTJSON__BIND__HPP

#include "grammar.hpp"
#include "types.hpp"
#ifndef CTJSON_IN_A_MODULE
#include <cstddef>
#include <string_view>
#include <utility>
#endif

// Lowering a ctlark parse tree into ctjson's document types. The tree
// shapes are fixed by grammar.hpp: object(pair...), pair(STRING value),
// array(value...), string(STRING), number(NUMBER), true/false/null.
//
// Strings decode here - escapes become bytes, \uXXXX becomes UTF-8,
// surrogate pairs combine - and decoding carries the one JSON rule a
// regular terminal cannot express: a high surrogate must be followed
// by a low one, and a lone low surrogate is an error. bind<Tree>::ok
// folds those checks over the whole document; is_valid includes it.

namespace ctjson::detail {

using bt_object = ctlark::text<'o', 'b', 'j', 'e', 'c', 't'>;
using bt_pair = ctlark::text<'p', 'a', 'i', 'r'>;
using bt_array = ctlark::text<'a', 'r', 'r', 'a', 'y'>;
using bt_string = ctlark::text<'s', 't', 'r', 'i', 'n', 'g'>;
using bt_number = ctlark::text<'n', 'u', 'm', 'b', 'e', 'r'>;
using bt_true = ctlark::text<'t', 'r', 'u', 'e'>;
using bt_false = ctlark::text<'f', 'a', 'l', 's', 'e'>;
using bt_null = ctlark::text<'n', 'u', 'l', 'l'>;

// --- decoding a raw STRING token (still quoted, escapes intact)

constexpr int bind_hexval(char c) noexcept {
	if (c >= '0' && c <= '9') { return c - '0'; }
	if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
	return c - 'A' + 10;
}

template <typename Text> struct decode_string {
	struct out_t {
		char buf[Text::size() + 1]{};
		size_t len = 0;
		bool ok = true;
	};

	static constexpr out_t compute() noexcept {
		out_t o{};
		constexpr std::string_view raw = Text::view();
		size_t i = 1;                     // the grammar guarantees the
		const size_t end = raw.size() - 1; // surrounding quotes
		while (i < end) {
			const char c = raw[i];
			if (c != '\\') {
				o.buf[o.len++] = c;
				++i;
				continue;
			}
			const char e = raw[i + 1];
			if (e == 'u') {
				unsigned long cp = 0;
				for (size_t k = i + 2; k < i + 6; ++k) {
					cp = cp * 16 + static_cast<unsigned long>(bind_hexval(raw[k]));
				}
				i += 6;
				if (cp >= 0xD800 && cp <= 0xDBFF) {
					// a high surrogate must pair with a low one
					if (i + 6 <= end && raw[i] == '\\' && raw[i + 1] == 'u') {
						unsigned long lo = 0;
						for (size_t k = i + 2; k < i + 6; ++k) {
							lo = lo * 16 + static_cast<unsigned long>(bind_hexval(raw[k]));
						}
						if (lo >= 0xDC00 && lo <= 0xDFFF) {
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
							i += 6;
						} else {
							o.ok = false;
							return o;
						}
					} else {
						o.ok = false;
						return o;
					}
				} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
					o.ok = false; // lone low surrogate
					return o;
				}
				if (cp < 0x80) {
					o.buf[o.len++] = static_cast<char>(cp);
				} else if (cp < 0x800) {
					o.buf[o.len++] = static_cast<char>(0xC0 | (cp >> 6));
					o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
				} else if (cp < 0x10000) {
					o.buf[o.len++] = static_cast<char>(0xE0 | (cp >> 12));
					o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
				} else {
					o.buf[o.len++] = static_cast<char>(0xF0 | (cp >> 18));
					o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
					o.buf[o.len++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					o.buf[o.len++] = static_cast<char>(0x80 | (cp & 0x3F));
				}
				continue;
			}
			switch (e) {
				case 'b': o.buf[o.len++] = '\b'; break;
				case 'f': o.buf[o.len++] = '\f'; break;
				case 'n': o.buf[o.len++] = '\n'; break;
				case 'r': o.buf[o.len++] = '\r'; break;
				case 't': o.buf[o.len++] = '\t'; break;
				default: o.buf[o.len++] = e; break; // " \ /
			}
			i += 2;
		}
		return o;
	}

	static constexpr out_t data = compute();
	static constexpr bool ok = data.ok;

	template <size_t... I> static constexpr auto lift(std::index_sequence<I...>) noexcept {
		return ctjson::string<data.buf[I]...>{};
	}
	using type = decltype(lift(std::make_index_sequence<data.len>{}));
};

// --- numbers keep their raw spelling

template <typename Text> struct make_number;
template <auto... Cs> struct make_number<ctlark::text<Cs...>> {
	using type = ctjson::number<Cs...>;
};

// --- the binder

template <typename Node> struct bind;

template <typename... Pairs> struct bind<ctlark::tree<bt_object, Pairs...>> {
	using type = ctjson::object<typename bind<Pairs>::type...>;
	static constexpr bool ok = (bind<Pairs>::ok && ... && true);
};

template <typename Key, typename Value> struct bind<ctlark::tree<bt_pair, Key, Value>> {
	using decoded_key = decode_string<typename Key::value_type>;
	using type = ctjson::member<typename decoded_key::type, typename bind<Value>::type>;
	static constexpr bool ok = decoded_key::ok && bind<Value>::ok;
};

template <typename... Values> struct bind<ctlark::tree<bt_array, Values...>> {
	using type = ctjson::array<typename bind<Values>::type...>;
	static constexpr bool ok = (bind<Values>::ok && ... && true);
};

template <typename Token> struct bind<ctlark::tree<bt_string, Token>> {
	using decoded = decode_string<typename Token::value_type>;
	using type = typename decoded::type;
	static constexpr bool ok = decoded::ok;
};

template <typename Token> struct bind<ctlark::tree<bt_number, Token>> {
	using type = typename make_number<typename Token::value_type>::type;
	static constexpr bool ok = true;
};

template <> struct bind<ctlark::tree<bt_true>> {
	using type = ctjson::boolean<true>;
	static constexpr bool ok = true;
};
template <> struct bind<ctlark::tree<bt_false>> {
	using type = ctjson::boolean<false>;
	static constexpr bool ok = true;
};
template <> struct bind<ctlark::tree<bt_null>> {
	using type = ctjson::null;
	static constexpr bool ok = true;
};

} // namespace ctjson::detail

#endif
