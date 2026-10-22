#ifndef DEPVEC_GRAPH_HPP
#define DEPVEC_GRAPH_HPP

#include "depvec/assertion_ast.hpp"
#include "depvec/instruction.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace depvec {

using NodeId = std::uint64_t;

struct InitNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
};

struct InstructionNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
  Instruction instruction;
};

struct ExitNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
};

using GraphNode = std::variant<InitNode, InstructionNode, ExitNode>;

struct GraphEdge {
  std::string id;
  std::vector<NodeId> sources;
  NodeId target;
  std::string kind;
  std::vector<std::string> values;
  FormulaPtr assertion;
};

struct FunctionGraph {
  std::string name;
  std::string ir;
  std::vector<GraphNode> nodes;
  std::vector<GraphEdge> edges;
};

struct Graph {
  std::string module;
  std::vector<FunctionGraph> functions;
};

} // namespace depvec

#endif
