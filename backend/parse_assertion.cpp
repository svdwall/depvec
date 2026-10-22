#include "depvec/assertion_parser.hpp"

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
          return "%" + node.value.name;
        } else if constexpr (std::is_same_v<Node, depvec::Dereference>) {
          return std::visit(
              [](const auto &pointer) {
                using Pointer = std::decay_t<decltype(pointer)>;
                if constexpr (std::is_same_v<Pointer, depvec::AllocaPointer>)
                  return "(* %" + pointer.allocaSite + ")";
                else
                  return "(* @" + pointer.globalName + ")";
              },
              node.pointer);
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

  try {
    const auto formula = depvec::parseAssertion(source);
    std::cout << "Parsed AST: " << formatFormula(*formula) << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
