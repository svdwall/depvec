#include "MemoryOrderAnalysis.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace depvec {
namespace {

using EdgeKey = std::tuple<unsigned, unsigned, std::string>;

bool isMemoryEvent(const llvm::Instruction &I) {
  return I.mayReadOrWriteMemory() || llvm::isa<llvm::FenceInst>(I);
}

llvm::AtomicOrdering getOrdering(const llvm::Instruction &I) {
  if (const auto *Load = llvm::dyn_cast<llvm::LoadInst>(&I))
    return Load->getOrdering();
  if (const auto *Store = llvm::dyn_cast<llvm::StoreInst>(&I))
    return Store->getOrdering();
  if (const auto *RMW = llvm::dyn_cast<llvm::AtomicRMWInst>(&I))
    return RMW->getOrdering();
  if (const auto *CmpXchg = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(&I))
    return CmpXchg->getSuccessOrdering();
  if (const auto *Fence = llvm::dyn_cast<llvm::FenceInst>(&I))
    return Fence->getOrdering();
  return llvm::AtomicOrdering::NotAtomic;
}

bool hasReleaseSemantics(llvm::AtomicOrdering Ordering) {
  return Ordering == llvm::AtomicOrdering::Release ||
         Ordering == llvm::AtomicOrdering::AcquireRelease ||
         Ordering == llvm::AtomicOrdering::SequentiallyConsistent;
}

bool hasAcquireSemantics(llvm::AtomicOrdering Ordering) {
  return Ordering == llvm::AtomicOrdering::Acquire ||
         Ordering == llvm::AtomicOrdering::AcquireRelease ||
         Ordering == llvm::AtomicOrdering::SequentiallyConsistent;
}

bool isSequentiallyConsistent(llvm::AtomicOrdering Ordering) {
  return Ordering == llvm::AtomicOrdering::SequentiallyConsistent;
}

bool hasReleaseSemantics(const llvm::Instruction &I) {
  return hasReleaseSemantics(getOrdering(I));
}

bool hasAcquireSemantics(const llvm::Instruction &I) {
  if (const auto *CmpXchg = llvm::dyn_cast<llvm::AtomicCmpXchgInst>(&I))
    return hasAcquireSemantics(CmpXchg->getSuccessOrdering()) ||
           hasAcquireSemantics(CmpXchg->getFailureOrdering());
  return hasAcquireSemantics(getOrdering(I));
}

bool isSequentiallyConsistent(const llvm::Instruction &I) {
  return isSequentiallyConsistent(getOrdering(I));
}

void traverseDominatorTree(
    const llvm::DomTreeNode *Node, const DependencyGraph &Graph,
    std::vector<unsigned> &DominatingAcquires,
    std::vector<unsigned> &DominatingMemoryEvents, std::set<EdgeKey> &Edges) {
  const llvm::BasicBlock *BB = Node->getBlock();
  if (!BB)
    return;

  const size_t AcquireCount = DominatingAcquires.size();
  const size_t MemoryCount = DominatingMemoryEvents.size();

  for (const llvm::Instruction &I : *BB) {
    if (!isMemoryEvent(I))
      continue;

    const unsigned NodeId = Graph.getNodeId(&I);
    const bool IsSC = isSequentiallyConsistent(I);

    // Every acquire event on the current dominator path orders this event.
    for (unsigned AcquireId : DominatingAcquires) {
      const llvm::Instruction *AcquireInst =
          Graph.nodes[AcquireId].instruction;
      const bool AcquireIsSC = isSequentiallyConsistent(*AcquireInst);
      Edges.emplace(AcquireId, NodeId,
                    AcquireIsSC ? "seq_cst" : "acquire");
    }

    // A release event is ordered after all earlier memory events on the
    // current dominator path, including earlier events in this block.
    if (hasReleaseSemantics(getOrdering(I))) {
      for (unsigned MemoryId : DominatingMemoryEvents)
        Edges.emplace(MemoryId, NodeId, IsSC ? "seq_cst" : "release");
    }

    DominatingMemoryEvents.push_back(NodeId);
    if (hasAcquireSemantics(I))
      DominatingAcquires.push_back(NodeId);
  }

  for (const llvm::DomTreeNode *Child : Node->children())
    traverseDominatorTree(Child, Graph, DominatingAcquires,
                          DominatingMemoryEvents, Edges);

  // Sibling subtrees are not dominated by this block's descendants.
  DominatingAcquires.resize(AcquireCount);
  DominatingMemoryEvents.resize(MemoryCount);
}

} // namespace

std::vector<DependencyGraph::DependencyEdge> MemoryOrderAnalysis::run(
    const DependencyGraph &Graph, const llvm::DominatorTree &DT) {
  std::set<EdgeKey> Edges;
  std::vector<unsigned> DominatingAcquires;
  std::vector<unsigned> DominatingMemoryEvents;
  if (const llvm::DomTreeNode *Root = DT.getRootNode())
    traverseDominatorTree(Root, Graph, DominatingAcquires,
                          DominatingMemoryEvents, Edges);

  MemoryOrderGraph MemOrderGraph;
  MemOrderGraph.edges.reserve(Edges.size());
  for (const EdgeKey &Edge : Edges)
    MemOrderGraph.edges.push_back({std::get<0>(Edge), std::get<1>(Edge),
                                  std::get<2>(Edge)});

  std::vector<DependencyGraph::DependencyEdge> Result;
  Result.reserve(MemOrderGraph.edges.size());
  for (const MemoryOrderEdge &MemoryEdge : MemOrderGraph.edges) {
    DependencyGraph::DependencyEdge DependencyEdge;
    DependencyEdge.sources.insert(MemoryEdge.source);
    DependencyEdge.target = MemoryEdge.target;
    DependencyEdge.kind = "memory (" + MemoryEdge.label + ")";
    Result.push_back(std::move(DependencyEdge));
  }
  return Result;
}

} // namespace depvec
