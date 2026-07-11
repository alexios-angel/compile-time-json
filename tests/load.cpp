// runtime tests for the runtime parser (json.load / json.loads style)

#include <ctjson.hpp>
#undef NDEBUG // the assertions ARE the test; keep them in release builds
#include <cassert>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

using namespace std::string_view_literals;

int main() {
	// scalars
	assert(ctjson::loads("42")->to<int>() == 42);
	assert(ctjson::loads("42")->is_integer());
	assert(ctjson::loads("2.5")->to<double>() == 2.5);
	assert(!ctjson::loads("2.5")->is_integer());
	assert(ctjson::loads("true")->boolean());
	assert(ctjson::loads("null")->is_null());
	assert(ctjson::loads(R"("text")")->view() == "text");

	// structures and chained lookup
	{
		const auto doc = ctjson::loads(R"({
			"users": [{"name": "Hana", "stars": 4700}],
			"active": true
		})");
		assert(doc.has_value());
		assert(doc->is_object());
		assert((*doc)["users"][0]["name"].view() == "Hana");
		assert((*doc)["users"][0]["stars"].to<long>() == 4700);
		assert(doc->get("active").boolean());
		assert(doc->contains("users"));
		assert(!doc->contains("missing"));
		// the null-object pattern: absent paths chain safely
		assert((*doc)["missing"]["deep"][7].is_null());
		assert(doc->find("missing") == nullptr);
	}

	// iteration over the underlying containers
	{
		const auto doc = ctjson::loads("[1, 2, 3]");
		long sum = 0;
		for (const auto & element : doc->as_array()) {
			sum += element.to<long>();
		}
		assert(sum == 6);
	}

	// escapes decode exactly like the compile-time parser
	assert(ctjson::loads(R"("a\nb")")->view() == "a\nb");
	assert(ctjson::loads(R"("A")")->view() == "A");
	assert(ctjson::loads(R"("😀")")->view() == "\xf0\x9f\x98\x80"); // 😀
	assert(!ctjson::loads(R"("\ud800")").has_value()); // lone surrogate

	// numbers: int/float split, overflow behaviours
	assert(ctjson::loads("9223372036854775807")->is_integer());
	assert(!ctjson::loads("9223372036854775808")->is_integer()); // beyond long long: double
	assert(ctjson::loads("1e999")->to<double>() > 0 && std::isinf(ctjson::loads("1e999")->to<double>()));
	assert(std::isnan(ctjson::loads("NaN")->to<double>()));      // Python's parse_constant
	assert(std::isinf(ctjson::loads("-Infinity")->to<double>()));

	// strictness matches RFC 8259 (and the compile-time parser)
	assert(!ctjson::loads("").has_value());
	assert(!ctjson::loads("[1, 2,]").has_value());
	assert(!ctjson::loads("{'k': 1}").has_value());
	assert(!ctjson::loads("{k: 1}").has_value());
	assert(!ctjson::loads("01").has_value());
	assert(!ctjson::loads("1.").has_value());
	assert(!ctjson::loads("+1").has_value());
	assert(!ctjson::loads("[1] []").has_value());
	assert(!ctjson::loads("\"unterminated").has_value());
	assert(!ctjson::loads("tru").has_value());

	// error reporting carries the byte offset, line and column
	{
		ctjson::load_error error;
		assert(!ctjson::loads("[1, 2,]", error).has_value());
		assert(error.position == 6);
		assert(error.message[0] != '\0');
		assert(error.line == 1 && error.column == 7);
	}
	{
		ctjson::load_error error;
		assert(!ctjson::loads("[1,\n 2,]", error).has_value());
		assert(error.position == 7); // the ']' after the trailing comma
		assert(error.line == 2 && error.column == 4);
		const std::string rendered = ctjson::to_string(error);
		assert(rendered.rfind("line 2, column 4: ", 0) == 0);
		assert(rendered.size() > sizeof("line 2, column 4: "));
	}

	// deep nesting is rejected, not a stack overflow
	{
		std::string bomb(100000, '[');
		assert(!ctjson::loads(bomb).has_value());
	}

	// equality is structural
	assert(*ctjson::loads("[1, {\"a\": null}]") == *ctjson::loads("[ 1 , { \"a\" : null } ]"));
	assert(*ctjson::loads("[1]") != *ctjson::loads("[2]"));

	// the encoder accepts runtime documents, nested in native containers too
	{
		const auto doc = ctjson::loads(R"({"n": [1, 2.5], "s": "x"})");
		assert(ctjson::dumps(*doc) == R"({"n": [1, 2.5], "s": "x"})");
		assert(ctjson::dumps(*doc, 2) == "{\n  \"n\": [\n    1,\n    2.5\n  ],\n  \"s\": \"x\"\n}");
		std::vector<ctjson::value> wrapped{*doc};
		assert(ctjson::dumps(wrapped) == R"([{"n": [1, 2.5], "s": "x"}])");
	}

	// like Python, runtime numbers are values, not spellings
	assert(ctjson::dumps(*ctjson::loads("2.50")) == "2.5");
	assert(ctjson::dumps(*ctjson::loads("1e2")) == "100.0");

	// json.load: from a stream
	{
		std::istringstream stream(R"({"from": "stream"})");
		const auto doc = ctjson::load(stream);
		assert(doc.has_value());
		assert((*doc)["from"].view() == "stream");
	}

#if CTLL_CNTTP_COMPILER_CHECK
	// the runtime and compile-time parsers agree, judged by the encoder
	#define AGREE(text) assert(ctjson::dumps(*ctjson::loads(text)) == ctjson::dumps(ctjson::parse<text>()))
	AGREE(R"({"a": [1, 2.5, true, null], "b": {"c": "d\ne"}})");
	AGREE(R"([[], {}, "", 0, -17])");
	AGREE(R"("café 😀")");
	#undef AGREE

	// and they reject the same inputs
	#define BOTH_REJECT(text) do { static_assert(!ctjson::is_valid<text>); assert(!ctjson::loads(text).has_value()); } while (false)
	BOTH_REJECT("[1,,2]");
	BOTH_REJECT(R"({"a" 1})");
	BOTH_REJECT("00");
	BOTH_REJECT(".5");
	BOTH_REJECT("1e");
	BOTH_REJECT(R"("\q")");
	#undef BOTH_REJECT
#endif

	std::puts("all load tests passed");
	return 0;
}
