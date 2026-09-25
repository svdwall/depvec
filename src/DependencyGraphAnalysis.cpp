#include "DependencyGraphAnalysis.h"

#include "DataDependencyAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"

#include <cassert>

namespace depvec {

llvm::AnalysisKey DependencyGraphAnalysis::Key;

unsigned DependencyGraph::getNodeId(const llvm::Instruction *I) const {
  auto It = nodeIds.find(I);
  assert(It != nodeIds.end() && "instruction is not a node in this graph");
  return It->second;
}

DependencyGraphAnalysis::Result
DependencyGraphAnalysis::run(llvm::Function &F,
                             llvm::FunctionAnalysisManager &) {
  DependencyGraph Graph;
  Graph.function = &F;

  Graph.initNodeId = 0;
  Graph.nodes.push_back({DependencyGraph::Node::Kind::Init, nullptr});
  unsigned ReturnCount = 0;
  for (llvm::BasicBlock &BB : F) {
    for (llvm::Instruction &I : BB) {
      unsigned Id = static_cast<unsigned>(Graph.nodes.size());
      bool IsReturn = llvm::isa<llvm::ReturnInst>(I);
      Graph.nodes.push_back({IsReturn ? DependencyGraph::Node::Kind::Exit
                                     : DependencyGraph::Node::Kind::Instruction,
                             &I});
      Graph.nodeIds[&I] = Id;
      if (IsReturn) {
        ++ReturnCount;
        Graph.exitNodeId = Id;
      }
    }
  }

  if (ReturnCount != 1)
    llvm::report_fatal_error(llvm::Twine("DepVec requires exactly one return "
                                         "instruction in function '") +
                             F.getName() + "'");

  Graph.edges = DataDependencyAnalysis::run(Graph);
  return Graph;
}

} // namespace depvec
