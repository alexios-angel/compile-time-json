// The runtime encoder, shaped like Python's json module: dumps() takes
// ordinary C++ values - maps, vectors, tuples, optionals, numbers,
// strings - and renders them with Python's exact formatting (verified
// byte-identical against CPython's json.dumps).
//
// Build: make python-style

#include <ctjson.hpp>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

// the default= equivalent: any type with an ADL-visible to_json
struct sensor {
	std::string id;
	std::optional<double> reading; // nullopt dumps as null, like None
};
auto to_json(const sensor & s) {
	return std::map<std::string, std::variant<std::string, std::optional<double>>>{
		{"id", s.id},
		{"reading", s.reading},
	};
}

int main() {
	// json.dumps(obj)
	std::map<std::string, std::vector<int>> data{{"a", {1, 2}}, {"b", {}}};
	std::cout << ctjson::dumps(data) << "\n\n";

	// json.dumps(obj, indent=2)
	std::cout << ctjson::dumps(data, 2) << "\n\n";

	// json.dumps(obj, indent=2, sort_keys=True) on an unsorted source
	ctjson::dump_options options;
	options.indent = 2;
	options.sort_keys = true;
	std::cout << ctjson::dumps(std::map<std::string, int>{{"zeta", 26}, {"alpha", 1}}, options) << "\n\n";

	// user types through to_json, containers of them included
	std::vector<sensor> sensors{{"s1", 22.5}, {"s2", std::nullopt}};
	std::cout << ctjson::dumps(sensors, 2) << "\n\n";

	// a compile-time document pretty-printed at runtime
	constexpr auto doc = ctjson::parse<R"({"answer":42,"exact":2.50})">();
	ctjson::dump(doc, std::cout, 2); // json.dump(obj, fp, indent=2)
	std::cout << "\n";
}
