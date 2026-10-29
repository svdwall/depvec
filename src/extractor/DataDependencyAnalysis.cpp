#include "DataDependencyAnalysis.h"

#include "llvm/IR/Argument.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace depvec {
namespace {

struct DefUseEdge {
  unsigned source;
  unsigned target;
  std::string value;
};

} // namespace

std::vector<DependencyGraph::DependencyEdge>
DataDependencyAnalysis::run(const DependencyGraph &Graph) {
  // Step 1: construct the single-source SSA def-use graph over the already
  // constructed graph nodes.
  std::vector<DefUseEdge> DefUseEdges;
  for (unsigned TargetId = 0; TargetId < Graph.nodes.size(); ++TargetId) {
    const DependencyGraph::Node &TargetNode = Graph.nodes[TargetId];
    if (TargetNode.kind == DependencyGraph::Node::Kind::Init)
      continue;

    const llvm::Instruction *Target = TargetNode.instruction;
    for (const llvm::Use &Operand : Target->operands()) {
      const llvm::Value *Value = Operand.get();
      unsigned SourceId;
      if (const auto *Source = llvm::dyn_cast<llvm::Instruction>(Value)) {
        if (Source->getFunction() != Graph.function)
          continue;
        SourceId = Graph.getNodeId(Source);
      } else if (llvm::isa<llvm::Argument>(Value)) {
        SourceId = Graph.initNodeId;
      } else {
        // Constants and basic-block operands do not define SSA registers.
        continue;
      }

      std::string RegisterName;
      llvm::raw_string_ostream OS(RegisterName);
      Value->printAsOperand(OS, false);
      OS.flush();
      DefUseEdges.push_back({SourceId, TargetId, std::move(RegisterName)});
    }
  }

  // Step 2: one data edge per target and SSA value. Repeated uses of the same
  // value, such as `add %x, %x`, share one edge. A phi instead gets a single
  // hyperedge whose sources are all of its incoming values.
  std::map<std::pair<unsigned, std::string>, DependencyGraph::DependencyEdge>
      ByTargetAndValue;
  for (const DefUseEdge &Edge : DefUseEdges) {
    const bool TargetIsPhi =
        llvm::isa<llvm::PHINode>(Graph.nodes[Edge.target].instruction);
    auto [It, Inserted] = ByTargetAndValue.try_emplace(
        {Edge.target, TargetIsPhi ? std::string() : Edge.value});
    DependencyGraph::DependencyEdge &DataEdge = It->second;
    if (Inserted) {
      DataEdge.target = Edge.target;
      DataEdge.kind = "data";
    }
    DataEdge.sources.insert(Edge.source);
    DataEdge.values.insert(Edge.value);
  }

  std::vector<DependencyGraph::DependencyEdge> Result;
  Result.reserve(ByTargetAndValue.size());
  for (auto &[Key, Edge] : ByTargetAndValue) {
    (void)Key;
    Result.push_back(std::move(Edge));
  }
  return Result;
}

} // namespace depvec
