#include "depvec/assertion_ast_builder.hpp"
#include "DepVecAssertionLexer.h"
#include "DepVecAssertionParser.h"
#include "antlr4-runtime.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>
#include <variant>

namespace {

std::string formatExpression(const depvec::Expression &expression) {
  return std::visit(
      [](const auto &node) -> std::string {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, depvec::IntegerLiteral>) {
          return std::to_string(node.value);
        } else if constexpr (std::is_same_v<Node, depvec::Variable>) {
          return node.name;
        } else if constexpr (std::is_same_v<Node, depvec::Dereference>) {
          return "(* " + node.name + ")";
        } else if constexpr (std::is_same_v<Node, depvec::ArithmeticUnaryExpression>) {
          return "(" + std::string(depvec::operatorText(node.op)) + " " +
                 formatExpression(*node.operand) + ")";
        } else {
          return "(" + formatExpression(*node.left) + " " +
                 std::string(depvec::operatorText(node.op)) + " " +
                 formatExpression(*node.right) + ")";
        }
      },
      expression.node);
}

std::string formatFormula(const depvec::Formula &formula) {
  return std::visit(
      [](const auto &node) -> std::string {
        using Node = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<Node, depvec::BooleanLiteral>) {
          return node.value ? "true" : "false";
        } else if constexpr (std::is_same_v<Node, depvec::ComparisonFormula>) {
          return "(" + formatExpression(*node.left) + " " +
                 std::string(depvec::operatorText(node.op)) + " " +
                 formatExpression(*node.right) + ")";
        } else if constexpr (std::is_same_v<Node, depvec::LogicalFormula>) {
          return "(" + formatFormula(*node.left) + " " +
                 std::string(depvec::operatorText(node.op)) + " " +
                 formatFormula(*node.right) + ")";
        } else {
          return "(! " + formatFormula(*node.operand) + ")";
        }
      },
      formula.node);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: depvec-assertion-parse <expression-file>\n";
    return 2;
  }

  std::ifstream file(argv[1]);
  if (!file) {
    std::cerr << "Could not open expression file: " << argv[1] << '\n';
    return 2;
  }
  const std::string source{std::istreambuf_iterator<char>(file),
                           std::istreambuf_iterator<char>()};

  antlr4::ANTLRInputStream input(source);
  DepVecAssertionLexer lexer(&input);
  antlr4::CommonTokenStream tokens(&lexer);
  DepVecAssertionParser parser(&tokens);
  auto *tree = parser.assertion();

  if (parser.getNumberOfSyntaxErrors() != 0)
    return 1;

  depvec::AssertionAstBuilder builder;
  const depvec::AssertionAst ast = builder.build(tree);
  std::cout << "Parsed AST: " << formatFormula(*ast.formula) << '\n';
  return 0;
}
