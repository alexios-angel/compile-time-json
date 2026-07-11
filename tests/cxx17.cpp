#include <ctjson.hpp>
#include <string_view>
using namespace std::literals;

// C++17: inputs and keys are fixed_string variables with linkage
static constexpr auto doc_text = ctll::fixed_string{R"({"name":"Hana","n":42})"};
static constexpr auto bad_text = ctll::fixed_string{"{oops"};
static constexpr ctll::fixed_string name_key = "name";
static constexpr ctll::fixed_string n_key = "n";

static_assert(ctjson::is_valid<doc_text>);
static_assert(!ctjson::is_valid<bad_text>);

constexpr auto doc = ctjson::parse<doc_text>();
static_assert(doc.template get<name_key>() == "Hana"sv);
static_assert(doc.template get<n_key>().template to<int>() == 42);

void empty_symbol() { }

// operator[] takes plain runtime keys and indexes in C++17

static constexpr auto seq_text = ctll::fixed_string{R"([10, 20, 30])"};
constexpr auto seq = ctjson::parse<seq_text>();
static_assert(seq[1].template to<int>() == 20);

static_assert([] {
	int hits = 0;
	ctjson::for_each(doc, [&](auto key, auto) {
		if (doc[key].type != ctjson::kind::null) {
			++hits;
		}
	});
	return hits;
}() == 2);

// iteration: uniform views, range-for, constexpr
static_assert([] {
	size_t n = 0;
	for (const auto & m : doc) {
		n += m.key.size() + m.value.text.size();
	}
	return n;
}() == (4 + 4) + (1 + 2));

// diagnostics through the variable-form API
static_assert(ctjson::error_info<doc_text>().ok());
constexpr auto bad_info = ctjson::error_info<bad_text>(); // "{oops"
static_assert(bad_info.kind == ctlark::error_kind::lex);
static_assert(bad_info.line == 1 && bad_info.column == 2);
static_assert(!ctjson::error_message<bad_text>().empty());
static_assert(ctjson::bind_error<doc_text>().ok());
constexpr auto traced = ctjson::debug::traced_parse<bad_text>();
static_assert(!traced.ok && traced.log.events > 0);
static_assert(!ctjson::debug::dump_tokens<doc_text>().empty());
