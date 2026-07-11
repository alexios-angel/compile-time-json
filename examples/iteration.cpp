// Brackets and iteration: operator[] is get spelled the familiar way -
// the key or index rides in the argument's TYPE - and begin/end give
// every container uniform views (kind + text) out of static storage,
// so range-for and <algorithm> work over a document whose elements all
// have different types.
//
// Build: make iteration

#include <ctjson.hpp>
#include <algorithm>
#include <iostream>

using namespace ctjson::literals;

constexpr auto repo = ctjson::parse<R"({
    "name":     "ctjson",
    "stars":    4700,
    "archived": false,
    "topics":   ["json", "constexpr", "compile-time"],
    "license":  {"spdx": "Apache-2.0", "osi": true}
})">();

// --- operator[]: get, spelled with brackets, chains included

static_assert(repo["name"_k] == "ctjson");
static_assert(repo["topics"_k][2_i] == "compile-time");
static_assert(repo["license"_k]["spdx"_k] == "Apache-2.0");
static_assert(repo["stars"_k].to<int>() == 4700);

// --- iteration: uniform views make a document an ordinary range

static_assert(std::count_if(begin(repo), end(repo),
    [](const ctjson::member_view & m) { return m.value.type == ctjson::kind::string; }) == 1);

constexpr auto longest = *std::max_element(begin(repo), end(repo),
    [](const ctjson::member_view & a, const ctjson::member_view & b) { return a.key.size() < b.key.size(); });
static_assert(longest.key == "archived");

// range-for in constant evaluation: a named constexpr function (gcc 10
// mishandles this loop inside a constexpr lambda)
constexpr size_t topic_chars() noexcept {
	size_t total = 0;
	for (const auto & v : repo["topics"_k]) {
		total += v.text.size(); // strings view their decoded content
	}
	return total;
}
static_assert(topic_chars() == 4 + 9 + 12);

constexpr const char * kind_names[]{"object", "array", "string", "number", "boolean", "null"};

int main() {
	// dump any document as a table: views are plain kinds and string_views
	for (const auto & m : repo) {
		std::cout << m.key << " (" << kind_names[static_cast<int>(m.value.type)] << "): "
		          << m.value.text << "\n";
	}

	std::cout << "\ntopics:";
	for (const auto & v : repo["topics"_k]) {
		std::cout << ' ' << v.text;
	}
	std::cout << "\n";
}
