#include "depvec/assertion_parser.hpp"

#include "depvec/assertion_ast_builder.hpp"
#include "DepVecAssertionLexer.h"
#include "DepVecAssertionParser.h"
#include "antlr4-runtime.h"

#include <exception>
#include <stdexcept>
#include <string>

namespace depvec {
namespace {

class SyntaxErrorCollector : public antlr4::BaseErrorListener {
public:
  void syntaxError(antlr4::Recognizer *, antlr4::Token *, std::size_t line,
                   std::size_t column, const std::string &message,
                   std::exception_ptr) override {
    if (!error.empty())
      error += "; ";
    error += "line " + std::to_string(line) + ":" +
             std::to_string(column) + " " + message;
  }

  std::string error;
};

} // namespace

FormulaPtr parseAssertion(std::string_view source, const ValueTypes &types) {
  const std::string sourceText(source);
  antlr4::ANTLRInputStream input(sourceText);
  DepVecAssertionLexer lexer(&input);
  SyntaxErrorCollector lexerErrors;
  lexer.removeErrorListeners();
  lexer.addErrorListener(&lexerErrors);

  antlr4::CommonTokenStream tokens(&lexer);
  DepVecAssertionParser parser(&tokens);
  SyntaxErrorCollector parserErrors;
  parser.removeErrorListeners();
  parser.addErrorListener(&parserErrors);

  auto *tree = parser.assertion();
  if (!lexerErrors.error.empty() || !parserErrors.error.empty()) {
    const std::string diagnostic =
        !lexerErrors.error.empty() ? lexerErrors.error : parserErrors.error;
    throw std::runtime_error("invalid assertion: " + diagnostic);
  }

  AssertionAstBuilder builder(types);
  return builder.build(tree).formula;
}

} // namespace depvec
