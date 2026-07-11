#ifndef CTJSON__GRAMMAR__HPP
#define CTJSON__GRAMMAR__HPP

#include "../ctlark.hpp"

// The grammar layer: JSON (RFC 8259) written in lark's grammar
// language and parsed by ctlark. The grammar below is DATA - ctlark
// parses this string while your code compiles, builds the parse
// tables, and runs an Earley parse of your document over them. The
// binder (bind.hpp) then lowers the lark tree into ctjson's document
// types.
//
// The terminals are the RFC's productions verbatim: strings take the
// exact escape set (\" \\ \/ \b \f \n \r \t \uXXXX) and no raw control
// characters; numbers forbid leading zeros, bare dots and dangling
// exponents; whitespace is the RFC's four characters. What a regular
// terminal cannot check - surrogate pairing in \uXXXX - the binder
// validates when it decodes.

namespace ctjson::detail {

inline constexpr ctll::fixed_string json_grammar = R"x(
?value: object
      | array
      | STRING -> string
      | NUMBER -> number
      | "true"  -> true
      | "false" -> false
      | "null"  -> null

object: "{" [pair ("," pair)*] "}"
pair: STRING ":" value
array: "[" [value ("," value)*] "]"

STRING: /"([^"\\\x00-\x1f]|\\(["\\\/bfnrt]|u[0-9a-fA-F]{4}))*"/
NUMBER: /-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?/

%ignore /[ \x09\x0a\x0d]+/
)x";

inline constexpr ctll::fixed_string json_start = "value";

static_assert(ctlark::grammar_valid<json_grammar>,
              "ctjson: internal error - the JSON grammar failed to compile");

} // namespace ctjson::detail

#endif
