#include "depvec/weakest_precondition.hpp"
#include "depvec/verification_conditions.hpp"

#include "depvec/graph_json_reader.hpp"
#include <nlohmann/json.hpp>
#include "smtlib_solver.hpp"
#include "z3_smtlib_engine.hpp"

#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <variant>

namespace {

std::string formatExpression(const depvec::Expression &expression) {
  return std::visit([](const auto &node) -> std::string {
    using Node = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Node, depvec::IntegerLiteral>) {
      return std::to_string(node.value);
    } else if constexpr (std::is_same_v<Node, depvec::Variable>) {
      return "%" + depvec::valueText(node.value);
    } else if constexpr (std::is_same_v<Node, depvec::Dereference>) {
      return std::visit([](const auto &pointer) {
        using Pointer = std::decay_t<decltype(pointer)>;
        if constexpr (std::is_same_v<Pointer, depvec::AllocaPointer>)
          return "(* %" + pointer.allocaSite + ")";
        else
          return "(* @" + pointer.globalName + ")";
      }, node.pointer);
    } else if constexpr (std::is_same_v<
                             Node, depvec::ArithmeticUnaryExpression>) {
      return "(" + std::string(depvec::operatorText(node.op)) + " " +
             formatExpression(*node.operand) + ")";
    } else {
      return "(" + formatExpression(*node.left) + " " +
             std::string(depvec::operatorText(node.op)) + " " +
             formatExpression(*node.right) + ")";
    }
  }, expression.node);
}

std::string formatFormula(const depvec::Formula &formula) {
  return std::visit([](const auto &node) -> std::string {
    using Node = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Node, depvec::BooleanLiteral>) {
      return node.value ? "true" : "false";
    } else if constexpr (std::is_same_v<Node, depvec::BooleanVariable>) {
      return "%" + depvec::valueText(node.value);
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
  }, formula.node);
}

std::string formatFormula(const depvec::FormulaPtr &formula) {
  return formula ? formatFormula(*formula) : "<missing>";
}

nlohmann::json verificationJson(const depvec::VerificationResult &result) {
  return {{"status", depvec::verificationStatusText(result.status)},
          {"detail", result.detail}};
}

// The verification results of every function, for Graph Studio.
nlohmann::json resultsJson(const depvec::DFG &dfg) {
  nlohmann::json functions = nlohmann::json::array();
  for (const auto &function : dfg.functions) {
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto &graphNode : function.nodes) {
      std::visit([&](const auto &node) {
        nlohmann::json entry = verificationJson(node.verification);
        entry["id"] = node.id;
        nodes.push_back(std::move(entry));
      }, graphNode);
    }
    nlohmann::json edges = nlohmann::json::array();
    for (const auto &edge : function.edges) {
      nlohmann::json checks = nlohmann::json::array();
      for (const auto &check : edge.interferenceChecks) {
        nlohmann::json entry = verificationJson(check.verification);
        entry["node"] = check.node;
        entry["predecessor"] = check.predecessor;
        checks.push_back(std::move(entry));
      }
      edges.push_back({{"id", edge.id}, {"interferenceChecks", std::move(checks)}});
    }
    functions.push_back({{"name", function.name},
                         {"nodesProved", depvec::allNodesProved(function)},
                         {"nodes", std::move(nodes)},
                         {"edges", std::move(edges)}});
  }
  return {{"format", "depvec-results-v1"}, {"functions", std::move(functions)}};
}

} // namespace

int main(int argc, char **argv) {
  const bool json = argc == 3 && std::strcmp(argv[1], "--json") == 0;
  if (argc != 2 && !json) {
    std::cerr << "Usage: depvec [--json] <annotated-graph.json>\n";
    return 2;
  }

  try {
    depvec::DFG dfg = depvec::readGraphFile(argv[json ? 2 : 1]);
    depvec::generateWeakestPreconditions(dfg);
    for (auto &function : dfg.functions) {
      depvec::Z3SmtLibEngine solverEngine;
      depvec::SmtLibSolver solver(depvec::verificationSymbols(function),
                                  solverEngine);
      depvec::checkVerificationConditions(function, solver);
    }
    if (json) {
      std::cout << resultsJson(dfg).dump(2) << '\n';
      return 0;
    }

    std::cout << "DFG module: " << dfg.module << '\n';
    for (auto &function : dfg.functions) {

      std::cout << "\nFunction: " << function.name << "\nLLVM IR:\n"
                << function.ir << "\nNodes:\n";
      for (const auto &graphNode : function.nodes) {
        std::visit([&](const auto &node) {
          using Node = std::decay_t<decltype(node)>;
          const char *kind;
          if constexpr (std::is_same_v<Node, depvec::InitNode>)
            kind = "init";
          else if constexpr (std::is_same_v<Node, depvec::InstructionNode>)
            kind = "instruction";
          else if constexpr (std::is_same_v<Node, depvec::BranchNode>)
            kind = "branch";
          else
            kind = "exit";
          std::cout << "  n" << node.id << " [" << kind << "] "
                    << node.instructionText << '\n'
                    << "    precondition: " << formatFormula(node.precondition)
                    << '\n';
          if constexpr (std::is_same_v<Node, depvec::BranchNode>) {
            std::cout << "    true postcondition: "
                      << formatFormula(node.true_postcondition) << '\n'
                      << "    false postcondition: "
                      << formatFormula(node.false_postcondition) << '\n';
          } else {
            std::cout << "    postcondition: "
                      << formatFormula(node.postcondition) << '\n';
          }
          std::cout << "    weakest precondition: ";
          if (node.weakest_precondition)
            std::cout << formatFormula(node.weakest_precondition);
          else
            std::cout << '<' << node.weakest_precondition_status << '>';
          std::cout << '\n';
          std::cout << "    verification: "
                    << depvec::verificationStatusText(node.verification.status);
          if (!node.verification.detail.empty())
            std::cout << " (" << node.verification.detail << ')';
          std::cout << '\n';
        }, graphNode);
      }
      std::cout << "Edges:\n";
      for (const auto &edge : function.edges) {
        std::cout << "  " << edge.id << " [";
        for (std::size_t index = 0; index < edge.sources.size(); ++index) {
          if (index != 0)
            std::cout << ", ";
          std::cout << 'n' << edge.sources[index];
        }
        std::cout << "] -> n" << edge.target << " [" << edge.kind
                  << "] values: [";
        for (std::size_t index = 0; index < edge.values.size(); ++index) {
          if (index != 0)
            std::cout << ", ";
          std::cout << edge.values[index];
        }
        std::cout << "] assertion: " << formatFormula(edge.assertion) << '\n';
        for (const auto &path : edge.incomingAssertions)
          std::cout << "    from " << path.predecessor << " (n" << path.source
                    << "): " << formatFormula(path.assertion) << '\n';
        for (const auto &check : edge.interferenceChecks) {
          std::cout << "    interference n" << check.node;
          if (!check.predecessor.empty())
            std::cout << " on the path from " << check.predecessor;
          std::cout << ": "
                    << depvec::verificationStatusText(check.verification.status);
          if (!check.verification.detail.empty())
            std::cout << " (" << check.verification.detail << ')';
          std::cout << '\n';
        }
      }
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
