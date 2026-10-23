#include "depvec/weakest_precondition.hpp"

#include "depvec/graph_json_reader.hpp"

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
      return "%" + node.value.name;
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

} // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: depvec <annotated-graph.json>\n";
    return 2;
  }

  try {
    depvec::DFG dfg = depvec::readGraphFile(argv[1]);
    depvec::generateWeakestPreconditions(dfg);
    std::cout << "DFG module: " << dfg.module << '\n';
    for (const auto &function : dfg.functions) {
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
      }
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
