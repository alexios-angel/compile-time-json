#include <ctjson.hpp>

void empty_symbol() { }

// the string-literal API needs C++20 class-type template parameters;
// tests/cxx17.cpp covers the C++17 variable form
#if CTLL_CNTTP_COMPILER_CHECK

#include <string_view>
using namespace std::literals;

// --- validity
static_assert(ctjson::is_valid<"{}">);
static_assert(ctjson::is_valid<"[]">);
static_assert(ctjson::is_valid<"  [1, 2, 3]  ">);
static_assert(ctjson::is_valid<R"({"a": 1, "b": [true, false, null]})">);
static_assert(ctjson::is_valid<"0">);
static_assert(ctjson::is_valid<"-0.5e+10">);
static_assert(ctjson::is_valid<R"("just a string")">);
static_assert(ctjson::is_valid<"true">);

// rejects (RFC 8259)
static_assert(!ctjson::is_valid<"">);
static_assert(!ctjson::is_valid<"01">);           // leading zero
static_assert(!ctjson::is_valid<"1.">);           // dot needs digits
static_assert(!ctjson::is_valid<".5">);
static_assert(!ctjson::is_valid<"1e">);
static_assert(!ctjson::is_valid<"+1">);
static_assert(!ctjson::is_valid<"-">);
static_assert(!ctjson::is_valid<"[1,2,]">);       // trailing comma
static_assert(!ctjson::is_valid<"{\"a\":}">);
static_assert(!ctjson::is_valid<"{a:1}">);        // unquoted key
static_assert(!ctjson::is_valid<"{\"a\" 1}">);    // missing colon
static_assert(!ctjson::is_valid<"\"unterminated">);
static_assert(!ctjson::is_valid<"[1 2]">);
static_assert(!ctjson::is_valid<"tru">);
static_assert(!ctjson::is_valid<"nulll">);
static_assert(!ctjson::is_valid<"[]  x">);        // trailing garbage
static_assert(!ctjson::is_valid<"\"bad \\q escape\"">);
static_assert(!ctjson::is_valid<"\"\\uD800\"">);  // lone high surrogate
static_assert(!ctjson::is_valid<"\"\\uDC00\"">);  // lone low surrogate

// --- document access
constexpr auto doc = ctjson::parse<R"({
	"name": "Hana",
	"stars": 4700,
	"ratio": -2.5e-1,
	"tags": ["regex", "compile-time"],
	"active": true,
	"parent": null,
	"nested": {"deep": [{"x": 42}]}
})">();

static_assert(doc.size() == 7);
static_assert(doc.contains<"name">());
static_assert(!doc.contains<"missing">());
static_assert(doc.get<"name">() == "Hana"sv);
static_assert(doc.get<"stars">().to<int>() == 4700);
static_assert(doc.get<"stars">().is_integer());
static_assert(doc.get<"ratio">().to<double>() == -0.25);
static_assert(!doc.get<"ratio">().is_integer());
static_assert(doc.get<"tags">().size() == 2);
static_assert(doc.get<"tags">().get<0>() == "regex"sv);
static_assert(doc.get<"tags">().get<1>() == "compile-time"sv);
static_assert(doc.get<"active">());
static_assert(doc.get<"parent">().type == ctjson::kind::null);
static_assert(doc.get<"nested">().get<"deep">().get<0>().get<"x">().to<int>() == 42);

// positional member access
static_assert(doc.key<0>() == "name"sv);
static_assert(doc.value<1>().to<int>() == 4700);

// --- numbers
static_assert(ctjson::parse<"0">().to<int>() == 0);
static_assert(ctjson::parse<"-0">().to<int>() == 0);
static_assert(ctjson::parse<"1e3">().to<int>() == 1000);
static_assert(ctjson::parse<"1.5">().to<double>() == 1.5);
static_assert(ctjson::parse<"12.34e2">().to<double>() == 1234.0);
static_assert(ctjson::parse<"1E+2">().to<int>() == 100);
static_assert(ctjson::parse<"-17">().to<long long>() == -17);
static_assert(ctjson::parse<"1.75">().to<int>() == 1); // truncates, like a cast

// --- strings and escapes
static_assert(ctjson::parse<R"("")">().empty());
static_assert(ctjson::parse<R"("a\nb")">() == "a\nb"sv);
static_assert(ctjson::parse<R"("q\"q")">() == "q\"q"sv);
static_assert(ctjson::parse<R"("back\\slash")">() == "back\\slash"sv);
static_assert(ctjson::parse<R"("sol\/idus")">() == "sol/idus"sv);
static_assert(ctjson::parse<R"("\t\r\b\f")">() == "\t\r\b\f"sv);
static_assert(ctjson::parse<R"("A")">() == "A"sv);
static_assert(ctjson::parse<R"("é")">() == "\xc3\xa9"sv);          // é as utf-8
static_assert(ctjson::parse<R"("€")">() == "\xe2\x82\xac"sv);      // €
static_assert(ctjson::parse<R"("😀")">() == "\xf0\x9f\x98\x80"sv); // 😀 via surrogate pair
static_assert(ctjson::parse<R"("café")">().size() == 5);           // bytes, not code points

// --- structures
static_assert(ctjson::parse<"[]">().empty());
static_assert(ctjson::parse<"{}">().empty());
static_assert(ctjson::parse<"[[1],[2,3]]">().get<1>().get<0>().to<int>() == 2);
static_assert(ctjson::parse<"[true,false,null]">().size() == 3);

// duplicate keys are legal per RFC; get finds the first
static_assert(ctjson::parse<R"({"k":1,"k":2})">().get<"k">().to<int>() == 1);

// --- serialization: any document renders back to minified JSON
static_assert(ctjson::serialize(ctjson::parse<R"({ "a" : [ 1, 2 ] , "b" : null })">()) == R"({"a":[1,2],"b":null})"sv);
static_assert(ctjson::serialize(ctjson::parse<"[]">()) == "[]"sv);
static_assert(ctjson::serialize(ctjson::parse<"{}">()) == "{}"sv);
static_assert(ctjson::serialize(ctjson::parse<"-2.5e-1">()) == "-2.5e-1"sv); // raw spelling kept
static_assert(ctjson::serialize(ctjson::parse<"[true,false]">()) == "[true,false]"sv);
// escapes are re-emitted: named ones by name, other controls as \u00XX
static_assert(ctjson::serialize(ctjson::parse<R"("a\nb\"q\"")">()) == R"("a\nb\"q\"")"sv);
// multi-byte utf-8 passes through
static_assert(ctjson::serialize(ctjson::parse<R"(["caf\u00e9"])">()) == "[\"caf\xc3\xa9\"]"sv);
// round-trip: parse(serialize(x)) is the same type as x
constexpr auto rt = ctjson::parse<R"({"n":[1,{"d":true}]})">();
static_assert(std::is_same_v<decltype(ctjson::parse<ctll::fixed_string{R"({"n":[1,{"d":true}]})"}>()), std::remove_const_t<decltype(rt)>>);

// --- iteration
static_assert([] {
	long long sum = 0;
	ctjson::for_each(ctjson::parse<"[1,2,3,4]">(), [&](auto v) { sum += v.template to<long long>(); });
	return sum;
}() == 10);
static_assert([] {
	size_t keys = 0, numbers = 0;
	ctjson::for_each(ctjson::parse<R"({"a":1,"b":"x","c":2})">(), [&](auto key, auto value) {
		keys += key.size();
		if constexpr (decltype(value)::type == ctjson::kind::number) {
			++numbers;
		}
	});
	return keys * 10 + numbers;
}() == 32);

#endif

// --- operator[] and iterators (see include/ctjson/views.hpp)

#if CTLL_CNTTP_COMPILER_CHECK

namespace bracket_tests {


constexpr auto d = ctjson::parse<R"({"name":"Hana","tags":["regex","ct"],"n":42})">();

// [] takes a plain key or index and returns a uniform view
static_assert(d["name"] == "Hana"sv);
static_assert(d["tags"][1] == "ct"sv);
static_assert(d["n"].to<int>() == 42);
static_assert(d.contains("tags"));
static_assert(!d.contains("missing"));
static_assert(d["missing"][0]["still-missing"].type == ctjson::kind::null);

// keys handed out by for_each work as [] arguments too
static_assert([] {
	size_t named = 0;
	ctjson::for_each(d, [&](auto key, auto value) {
		if (d[key].type == decltype(value)::type) {
			++named;
		}
	});
	return named;
}() == 3);

// begin/end yield uniform views from static storage: range-for works,
// in constexpr evaluation included
static_assert([] {
	size_t key_chars = 0;
	for (const auto & m : d) {
		key_chars += m.key.size();
	}
	return key_chars;
}() == 4 + 4 + 1);

static_assert([] {
	for (const auto & m : d) {
		if (m.key == "tags") {
			// nested containers view their minified serialization
			return m.value.type == ctjson::kind::array && m.value.text == R"(["regex","ct"])";
		}
	}
	return false;
}());

static_assert([] {
	size_t text_chars = 0;
	for (const auto & v : d["tags"]) {
		text_chars += v.text.size(); // strings view their decoded content
	}
	return text_chars;
}() == 5 + 2);

// scalar views: numbers keep their spelling, booleans and null their literals
static_assert([] {
	constexpr auto arr = ctjson::parse<R"([1.5e2, true, null])">();
	auto it = ctjson::begin(arr);
	return it[0].text == "1.5e2" && it[1].text == "true" && it[2].text == "null"
	    && it[1].type == ctjson::kind::boolean;
}());

// empty containers iterate zero times
static_assert(ctjson::begin(ctjson::parse<"[]">()) == ctjson::end(ctjson::parse<"[]">()));

} // namespace bracket_tests

#endif
