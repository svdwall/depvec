#include "depvec/verification_conditions.hpp"

#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace depvec {
namespace {

std::string valueKey(const ValueRef &value) {
  return "value:" + valueText(value);
}

std::string pointerKey(const Pointer &pointer) {
  return std::visit([](const auto &reference) {
    using Ref = std::decay_t<decltype(reference)>;
    if constexpr (std::is_same_v<Ref, AllocaPointer>)
      return "alloca:" + reference.allocaSite;
    else
      return "global:" + reference.globalName;
  }, pointer);
}

// Symbols keyed for deduplication, in a stable order.
struct SymbolMaps {
  std::map<std::string, ValueRef> variables;
  std::map<std::string, ValueRef> booleanVariables;
  std::map<std::string, Pointer> memoryLocations;
};

void collectExpressionSymbols(const Expression &expression, SymbolMaps &symbols) {
  std::visit([&](const auto &node) {
    using Node = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Node, Variable>) {
      symbols.variables.emplace(valueKey(node.value), node.value);
    } else if constexpr (std::is_same_v<Node, Dereference>) {
      symbols.memoryLocations.emplace(pointerKey(node.pointer), node.pointer);
    } else if constexpr (std::is_same_v<Node, ArithmeticUnaryExpression>) {
      collectExpressionSymbols(*node.operand, symbols);
    } else if constexpr (std::is_same_v<Node, ArithmeticBinaryExpression>) {
      collectExpressionSymbols(*node.left, symbols);
      collectExpressionSymbols(*node.right, symbols);
    }
  }, expression.node);
}

void collectFormulaSymbols(const Formula &formula, SymbolMaps &symbols) {
  std::visit([&](const auto &node) {
    using Node = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Node, BooleanVariable>) {
      symbols.booleanVariables.emplace(valueKey(node.value), node.value);
    } else if constexpr (std::is_same_v<Node, ComparisonFormula>) {
      collectExpressionSymbols(*node.left, symbols);
      collectExpressionSymbols(*node.right, symbols);
    } else if constexpr (std::is_same_v<Node, LogicalFormula>) {
      collectFormulaSymbols(*node.left, symbols);
      collectFormulaSymbols(*node.right, symbols);
    } else if constexpr (std::is_same_v<Node, NegationFormula>) {
      collectFormulaSymbols(*node.operand, symbols);
    }
  }, formula.node);
}

template <typename Node>
void collectNodeSymbols(const Node &node, SymbolMaps &symbols) {
  if (node.precondition)
    collectFormulaSymbols(*node.precondition, symbols);
  if (node.weakest_precondition)
    collectFormulaSymbols(*node.weakest_precondition, symbols);
}

template <typename Node>
void checkNode(Node &node, Solver &solver) {
  node.verification = {};
  if (!static_cast<bool>(node.precondition)) {
    node.verification.detail = "precondition is missing";
    return;
  }
  if (!static_cast<bool>(node.weakest_precondition)) {
    node.verification.detail = node.weakest_precondition_status.empty()
                                  ? "weakest precondition is missing"
                                  : node.weakest_precondition_status;
    return;
  }

  try {
    const Formula violation = *node.precondition && !*node.weakest_precondition;
    const SolverResult result = solver.check(violation);
    switch (result.status) {
    case SolverStatus::Unsat:
      node.verification.status = VerificationStatus::Proved;
      node.verification.detail = "violation condition is unsatisfiable";
      break;
    case SolverStatus::Sat:
      node.verification.status = VerificationStatus::Counterexample;
      node.verification.detail = "violation condition is satisfiable";
      break;
    case SolverStatus::Unknown:
      node.verification.status = VerificationStatus::Unknown;
      node.verification.detail = result.reason;
      break;
    }
  } catch (const std::exception &error) {
    node.verification.status = VerificationStatus::Error;
    node.verification.detail = error.what();
  }
}

} // namespace

SolverSymbols verificationSymbols(const FunctionDFG &function) {
  SymbolMaps maps;
  for (const GraphNode &graphNode : function.nodes)
    std::visit([&](const auto &node) { collectNodeSymbols(node, maps); },
               graphNode);

  SolverSymbols symbols;
  for (auto &[key, value] : maps.variables) {
    (void)key;
    symbols.variables.push_back(std::move(value));
  }
  for (auto &[key, value] : maps.booleanVariables) {
    (void)key;
    symbols.booleanVariables.push_back(std::move(value));
  }
  for (auto &[key, pointer] : maps.memoryLocations) {
    (void)key;
    symbols.memoryLocations.push_back(std::move(pointer));
  }
  return symbols;
}

void checkVerificationConditions(FunctionDFG &function, Solver &solver) {
  for (GraphNode &graphNode : function.nodes)
    std::visit([&](auto &node) { checkNode(node, solver); }, graphNode);
}

const char *verificationStatusText(VerificationStatus status) {
  switch (status) {
  case VerificationStatus::NotChecked: return "not checked";
  case VerificationStatus::Proved: return "proved";
  case VerificationStatus::Counterexample: return "counterexample";
  case VerificationStatus::Unknown: return "unknown";
  case VerificationStatus::Error: return "error";
  }
  return "unknown status";
}

} // namespace depvec
