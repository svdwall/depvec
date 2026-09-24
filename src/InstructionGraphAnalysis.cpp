#include "InstructionGraphAnalysis.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"

#include <cassert>
#include <utility>

namespace depvec {

llvm::AnalysisKey InstructionGraphAnalysis::Key;

unsigned InstructionGraphAnalysis::Result::getNodeId(
    const llvm::Instruction *I) const {
  auto It = nodeIds.find(I);
  assert(It != nodeIds.end() && "instruction is not a node in this graph");
  return It->second;
}

void InstructionGraphAnalysis::Result::addDependencyEdge(
    llvm::ArrayRef<const llvm::Instruction *> sources,
    const llvm::Instruction *target, DependencyEdgeFlavour flavour,
    llvm::StringRef value) {
  DependencyEdge Edge;
  Edge.target = getNodeId(target);
  Edge.flavour = flavour;
  Edge.value = value.str();

  for (const llvm::Instruction *Source : sources) {
    unsigned SourceId = getNodeId(Source);
    if (!llvm::is_contained(Edge.sources, SourceId))
      Edge.sources.push_back(SourceId);
  }

  dependencyEdges.push_back(std::move(Edge));
}

InstructionGraphAnalysis::Result
InstructionGraphAnalysis::run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &) {
  Result Graph;
  Graph.function = &F;

  // Include every instruction, including terminators. IDs are deterministic
  // in the function's block and instruction order.
  for (llvm::BasicBlock &BB : F) {
    for (llvm::Instruction &I : BB) {
      unsigned Id = static_cast<unsigned>(Graph.nodes.size());
      Graph.nodes.push_back(&I);
      Graph.nodeIds[&I] = Id;
    }
  }

  return Graph;
}

} // namespace depvec
