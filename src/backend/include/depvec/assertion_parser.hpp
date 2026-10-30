#ifndef DEPVEC_ASSERTION_PARSER_HPP
#define DEPVEC_ASSERTION_PARSER_HPP

#include "depvec/assertion_ast.hpp"
#include "depvec/value_types.hpp"

#include <string_view>

namespace depvec {

// Parse one complete assertion and return its formula AST, typing local values
// by types. Throws std::runtime_error with the lexer/parser diagnostic on
// invalid input, or on a type error.
FormulaPtr parseAssertion(std::string_view source, const ValueTypes &types = {});

} // namespace depvec

#endif
