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

void collectInterferenceSymbols(const DependencyEdge &edge, SymbolMaps &symbols) {
  for (const InterferenceCheck &check : edge.interferenceChecks) {
    if (check.assertion)
      collectFormulaSymbols(*check.assertion, symbols);
    if (check.weakest_precondition)
      collectFormulaSymbols(*check.weakest_precondition, symbols);
  }
}

// Proved when the violation condition is unsatisfiable.
VerificationResult checkViolation(const Formula &violation, Solver &solver) {
  VerificationResult verification;
  try {
    const SolverResult result = solver.check(violation);
    switch (result.status) {
    case SolverStatus::Unsat:
      verification.status = VerificationStatus::Proved;
      verification.detail = "violation condition is unsatisfiable";
      break;
    case SolverStatus::Sat:
      verification.status = VerificationStatus::Counterexample;
      verification.detail = "violation condition is satisfiable";
      break;
    case SolverStatus::Unknown:
      verification.status = VerificationStatus::Unknown;
      verification.detail = result.reason;
      break;
    }
  } catch (const std::exception &error) {
    verification.status = VerificationStatus::Error;
    verification.detail = error.what();
  }
  return verification;
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

  node.verification = checkViolation(
      *node.precondition && !*node.weakest_precondition, solver);
}

} // namespace

bool allNodesProved(const FunctionDFG &function) {
  for (const GraphNode &graphNode : function.nodes) {
    const bool proved = std::visit([](const auto &node) {
      using Node = std::decay_t<decltype(node)>;
      if constexpr (std::is_same_v<Node, InstructionNode> ||
                    std::is_same_v<Node, BranchNode>)
        return node.verification.status == VerificationStatus::Proved;
      else
        return true;
    }, graphNode);
    if (!proved)
      return false;
  }
  return true;
}

namespace {

FormulaPtr preconditionOf(const FunctionDFG &function, NodeId id) {
  for (const GraphNode &graphNode : function.nodes) {
    FormulaPtr precondition;
    const bool found = std::visit([&](const auto &node) {
      if (node.id != id)
        return false;
      precondition = node.precondition;
      return true;
    }, graphNode);
    if (found)
      return precondition;
  }
  return {};
}

// The interfering node runs while the assertion holds and its own
// precondition holds, so it must preserve the assertion.
void checkInterference(const FunctionDFG &function, InterferenceCheck &check,
                       Solver &solver) {
  check.verification = {};
  const FormulaPtr precondition = preconditionOf(function, check.node);
  if (!static_cast<bool>(precondition) || !static_cast<bool>(check.assertion)) {
    check.verification.detail = "assertion or node precondition is missing";
    return;
  }
  if (!static_cast<bool>(check.weakest_precondition)) {
    check.verification.detail = check.weakest_precondition_status.empty()
                                    ? "weakest precondition is missing"
                                    : check.weakest_precondition_status;
    return;
  }
  check.verification = checkViolation(
      *check.assertion && *precondition && !*check.weakest_precondition, solver);
}

} // namespace

SolverSymbols verificationSymbols(const FunctionDFG &function) {
  SymbolMaps maps;
  for (const GraphNode &graphNode : function.nodes)
    std::visit([&](const auto &node) { collectNodeSymbols(node, maps); },
               graphNode);
  for (const DependencyEdge &edge : function.edges)
    collectInterferenceSymbols(edge, maps);

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

  // Edge assertions are only established once every node is proved, so the
  // interference checks, which assume them, wait for that.
  const bool nodesProved = allNodesProved(function);
  for (DependencyEdge &edge : function.edges) {
    for (InterferenceCheck &check : edge.interferenceChecks) {
      if (nodesProved) {
        checkInterference(function, check, solver);
      } else {
        check.verification = {};
        check.verification.detail = "not every node verification condition is proved";
      }
    }
  }
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
