#include "ControlDependencyAnalysis.h"

#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"

#include <set>
#include <utility>
#include <vector>

namespace depvec {
namespace {

void addControlEdgesForBranch(
    const DependencyGraph &Graph, const llvm::BranchInst &Branch,
    const llvm::BasicBlock *BranchBlock, llvm::BasicBlock *Successor,
    const llvm::BasicBlock *OtherSuccessor, const char *BranchValue, const llvm::DominatorTree &DT,
    const llvm::PostDominatorTree &PDT,
    std::vector<DependencyGraph::DependencyEdge> &Edges) {
  auto AddEdge = [&](const llvm::Instruction &Target) {
    DependencyGraph::DependencyEdge Edge;
    Edge.sources.insert(Graph.getNodeId(&Branch));
    Edge.target = Graph.getNodeId(&Target);
    Edge.kind = "control";
    Edge.values.insert(BranchValue);
    Edges.push_back(std::move(Edge));
  };

  std::vector<llvm::BasicBlock *> Worklist{Successor};
  std::set<const llvm::BasicBlock *> Visited;
  std::set<unsigned> EmittedTargets;
  while (!Worklist.empty()) {
    llvm::BasicBlock *Block = Worklist.back();
    Worklist.pop_back();
    if (!Visited.insert(Block).second ||
        !DT.dominates(BranchBlock, Block) ||
        (PDT.dominates(Block, BranchBlock) && !PDT.dominates(BranchBlock, OtherSuccessor))) 
      continue;

    for (llvm::Instruction &Instruction : *Block) {
      const unsigned TargetId = Graph.getNodeId(&Instruction);
      if (EmittedTargets.insert(TargetId).second)
        AddEdge(Instruction);
    }

    for (llvm::BasicBlock *Next : llvm::successors(Block))
      Worklist.push_back(Next);
  }
}

} // namespace

std::vector<DependencyGraph::DependencyEdge>
ControlDependencyAnalysis::run(const DependencyGraph &Graph,
                               const llvm::DominatorTree &DT,
                               const llvm::PostDominatorTree &PDT) {
  std::vector<DependencyGraph::DependencyEdge> Edges;
  for (const DependencyGraph::Node &Node : Graph.nodes) {
    const auto *Branch = llvm::dyn_cast_or_null<llvm::BranchInst>(Node.instruction);
    if (!Branch || Branch->getNumSuccessors() != 2)
      continue;

    const llvm::BasicBlock *BranchBlock = Branch->getParent();
    llvm::BasicBlock *TrueSuccessor = Branch->getSuccessor(0);
    llvm::BasicBlock *FalseSuccessor = Branch->getSuccessor(1);
    addControlEdgesForBranch(Graph, *Branch, BranchBlock, TrueSuccessor,
                             FalseSuccessor, "true-branch", DT, PDT, Edges);
    addControlEdgesForBranch(Graph, *Branch, BranchBlock, FalseSuccessor,
                             TrueSuccessor, "false-branch", DT, PDT, Edges);
  }
  return Edges;
}

} // namespace depvec
