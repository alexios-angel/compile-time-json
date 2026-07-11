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

// operator[] needs no C++20: keys from for_each and _i indexes are types
using namespace ctjson::literals;

static constexpr auto seq_text = ctll::fixed_string{R"([10, 20, 30])"};
constexpr auto seq = ctjson::parse<seq_text>();
static_assert(seq[1_i].template to<int>() == 20);

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
