#ifndef DEPVEC_DFG_HPP
#define DEPVEC_DFG_HPP

#include "depvec/assertion_ast.hpp"
#include "depvec/instruction.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace depvec {

using NodeId = std::uint64_t;

enum class VerificationStatus {
  NotChecked,
  Proved,
  Counterexample,
  Unknown,
  Error
};

struct VerificationResult {
  VerificationStatus status = VerificationStatus::NotChecked;
  std::string detail;
};

// Conditions are derived by the graph reader after all edges are parsed:
// precondition is the conjunction of incoming edge assertions; ordinary node
// postcondition is the conjunction of outgoing edge assertions. Branch nodes
// keep separate postconditions for their true and false control edges.
struct InitNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
  FormulaPtr precondition;
  FormulaPtr postcondition;
  FormulaPtr weakest_precondition;
  std::string weakest_precondition_status;
  VerificationResult verification;
};

struct InstructionNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
  Instruction instruction;
  FormulaPtr precondition;
  FormulaPtr postcondition;
  FormulaPtr weakest_precondition;
  std::string weakest_precondition_status;
  VerificationResult verification;
};

struct BranchNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
  BranchInstruction instruction;
  FormulaPtr precondition;
  FormulaPtr true_postcondition;
  FormulaPtr false_postcondition;
  FormulaPtr weakest_precondition;
  std::string weakest_precondition_status;
  VerificationResult verification;
};

struct ExitNode {
  NodeId id;
  std::int64_t line;
  std::string instructionText;
  FormulaPtr precondition;
  FormulaPtr postcondition;
  FormulaPtr weakest_precondition;
  std::string weakest_precondition_status;
  VerificationResult verification;
};

using GraphNode = std::variant<InitNode, InstructionNode, BranchNode, ExitNode>;

// The assertion of a phi data hyperedge for the path from one predecessor
// block, whose incoming value source defines.
struct IncomingAssertion {
  std::string predecessor;
  NodeId source;
  FormulaPtr assertion;
};

// Whether an interfering node preserves an edge assertion: the violation is
// assertion && node precondition && !weakest_precondition, where the weakest
// precondition is the node's for the assertion as its postcondition. A phi
// edge has one check per incoming assertion, named by predecessor; other
// edges leave predecessor empty.
struct InterferenceCheck {
  NodeId node;
  std::string predecessor;
  FormulaPtr assertion;
  FormulaPtr weakest_precondition;
  std::string weakest_precondition_status;
  VerificationResult verification;
};

// A data edge into a phi carries one incoming assertion per predecessor, and
// its assertion is true. All other edges have no incoming assertions.
// interferenceChecks is filled by generateWeakestPreconditions.
struct DependencyEdge {
  std::string id;
  std::vector<NodeId> sources;
  NodeId target;
  std::string kind;
  std::vector<std::string> values;
  FormulaPtr assertion;
  std::vector<IncomingAssertion> incomingAssertions;
  std::vector<NodeId> interferences;
  std::vector<InterferenceCheck> interferenceChecks;
};

inline constexpr const char *TrueBranchValue = "true-branch";
inline constexpr const char *FalseBranchValue = "false-branch";
inline constexpr const char *UnconditionalBranchValue = "unconditional-branch";

struct FunctionDFG {
  std::string name;
  std::string ir;
  std::vector<GraphNode> nodes;
  std::vector<DependencyEdge> edges;
};

struct DFG {
  std::string module;
  std::vector<FunctionDFG> functions;
};

} // namespace depvec

#endif
