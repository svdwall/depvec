#include "smtlib_solver.hpp"

#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace depvec {
namespace {

std::string valueKey(const ValueRef &value) { return "value:" + value.name; }

std::string pointerKey(const Pointer &pointer) {
  return std::visit([](const auto &reference) {
    using Ref = std::decay_t<decltype(reference)>;
    if constexpr (std::is_same_v<Ref, AllocaPointer>)
      return "alloca:" + reference.allocaSite;
    else
      return "global:" + reference.globalName;
  }, pointer);
}

std::string symbolName(std::size_t index) {
  return "depvec_symbol_" + std::to_string(index);
}

std::string freshMarker(std::uint64_t index) {
  return "depvec_response_" + std::to_string(index);
}

std::string encodeExpression(const Expression &expression,
                             const SmtLibSolver &solver);

std::string encodeFormulaNode(const Formula &formula,
                              const SmtLibSolver &solver) {
  return std::visit([&](const auto &node) -> std::string {
    using Node = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Node, BooleanLiteral>) {
      return node.value ? "true" : "false";
    } else if constexpr (std::is_same_v<Node, ComparisonFormula>) {
      const char *op = nullptr;
      switch (node.op) {
      case ComparisonOperator::Equal: op = "="; break;
      case ComparisonOperator::NotEqual: op = "distinct"; break;
      case ComparisonOperator::Less: op = "<"; break;
      case ComparisonOperator::LessEqual: op = "<="; break;
      case ComparisonOperator::Greater: op = ">"; break;
      case ComparisonOperator::GreaterEqual: op = ">="; break;
      }
      if (!op)
        throw std::invalid_argument("unknown comparison operator");
      return "(" + std::string(op) + " " +
             encodeExpression(*node.left, solver) + " " +
             encodeExpression(*node.right, solver) + ")";
    } else if constexpr (std::is_same_v<Node, LogicalFormula>) {
      const char *op = nullptr;
      switch (node.op) {
      case LogicalOperator::And: op = "and"; break;
      case LogicalOperator::Or: op = "or"; break;
      case LogicalOperator::Implies: op = "=>"; break;
      }
      if (!op)
        throw std::invalid_argument("unknown logical operator");
      return "(" + std::string(op) + " " +
             encodeFormulaNode(*node.left, solver) + " " +
             encodeFormulaNode(*node.right, solver) + ")";
    } else if constexpr (std::is_same_v<Node, NegationFormula>) {
      return "(not " + encodeFormulaNode(*node.operand, solver) + ")";
    } else {
      static_assert(!sizeof(Node), "unhandled formula node");
    }
  }, formula.node);
}

std::string encodeExpression(const Expression &expression,
                             const SmtLibSolver &solver) {
  return std::visit([&](const auto &node) -> std::string {
    using Node = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Node, IntegerLiteral>) {
      if (node.value >= 0)
        return std::to_string(node.value);
      // SMT-LIB represents a negative numeral as unary minus.
      const auto magnitude =
          static_cast<std::uint64_t>(-(node.value + 1)) + 1;
      return "(- " + std::to_string(magnitude) + ")";
    } else if constexpr (std::is_same_v<Node, Variable>) {
      return solver.symbolForValue(node.value);
    } else if constexpr (std::is_same_v<Node, Dereference>) {
      return solver.symbolForPointer(node.pointer);
    } else if constexpr (std::is_same_v<Node, ArithmeticUnaryExpression>) {
      if (node.op != ArithmeticUnaryOperator::Negate)
        throw std::invalid_argument("unknown arithmetic unary operator");
      return "(- " + encodeExpression(*node.operand, solver) + ")";
    } else if constexpr (std::is_same_v<Node, ArithmeticBinaryExpression>) {
      const char *op = nullptr;
      switch (node.op) {
      case ArithmeticBinaryOperator::Add: op = "+"; break;
      case ArithmeticBinaryOperator::Subtract: op = "-"; break;
      case ArithmeticBinaryOperator::Multiply: op = "*"; break;
      case ArithmeticBinaryOperator::Divide: op = "div"; break;
      }
      if (!op)
        throw std::invalid_argument("unknown arithmetic binary operator");
      return "(" + std::string(op) + " " +
             encodeExpression(*node.left, solver) + " " +
             encodeExpression(*node.right, solver) + ")";
    } else {
      static_assert(!sizeof(Node), "unhandled expression node");
    }
  }, expression.node);
}

} // namespace

SmtLibSolver::SmtLibSolver(SolverSymbols symbols, SmtLibEngine &engine)
    : Solver(std::move(symbols)), engine_(engine) {
  std::string declarations;
  std::size_t index = 0;
  auto addSymbol = [&](std::string key) {
    if (symbolsByKey_.find(key) != symbolsByKey_.end())
      return;
    const std::string symbol = symbolName(index++);
    symbolsByKey_.emplace(std::move(key), symbol);
    declarations += "(declare-const " + symbol + " Int)\n";
  };

  for (const ValueRef &variable : symbols_.variables)
    addSymbol(valueKey(variable));
  for (const Pointer &pointer : symbols_.memoryLocations)
    addSymbol(pointerKey(pointer));

  if (!declarations.empty()) {
    const std::string marker = freshMarker(nextMarker_++);
    const std::string response = engine_.execute(declarations, marker);
    if (!response.empty())
      throw std::runtime_error("unexpected solver response to declarations: " +
                               response);
  }
}

SolverResult SmtLibSolver::check(const Formula &formula) {
  const std::string marker = freshMarker(nextMarker_++);
  const std::string commands =
      "(push 1)\n(assert " + encodeFormula(formula) +
      ")\n(check-sat)\n(pop 1)\n";
  const std::string response = engine_.execute(commands, marker);

  std::size_t begin = response.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
    throw std::runtime_error("solver returned an empty check-sat response");
  const std::size_t end = response.find_first_of(" \t\r\n", begin);
  const std::string status = response.substr(
      begin, end == std::string::npos ? std::string::npos : end - begin);
  const std::string remainder = end == std::string::npos
                                    ? std::string{}
                                    : response.substr(end);
  if (status == "sat")
    return {SolverStatus::Sat, {}};
  if (status == "unsat")
    return {SolverStatus::Unsat, {}};
  if (status == "unknown")
    return {SolverStatus::Unknown, remainder};
  throw std::runtime_error("unexpected solver response: " + response);
}

std::string SmtLibSolver::encodeFormula(const Formula &formula) const {
  return encodeFormulaNode(formula, *this);
}

std::string SmtLibSolver::symbolForValue(const ValueRef &value) const {
  auto symbol = symbolsByKey_.find(valueKey(value));
  if (symbol == symbolsByKey_.end())
    throw std::invalid_argument("unregistered assertion variable '%" +
                                value.name + "'");
  return symbol->second;
}

std::string SmtLibSolver::symbolForPointer(const Pointer &pointer) const {
  auto symbol = symbolsByKey_.find(pointerKey(pointer));
  if (symbol == symbolsByKey_.end())
    throw std::invalid_argument("unregistered assertion memory location");
  return symbol->second;
}

} // namespace depvec
