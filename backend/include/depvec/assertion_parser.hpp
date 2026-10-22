#ifndef DEPVEC_ASSERTION_PARSER_HPP
#define DEPVEC_ASSERTION_PARSER_HPP

#include "depvec/assertion_ast.hpp"

#include <string_view>

namespace depvec {

// Parse one complete assertion and return its formula AST. Throws
// std::runtime_error with the lexer/parser diagnostic on invalid input.
FormulaPtr parseAssertion(std::string_view source);

} // namespace depvec

#endif
