#include "InterferenceAnalysis.h"

#include "InterferenceModel.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/ErrorHandling.h"

#include <string>
#include <variant>
#include <vector>

namespace depvec {
namespace {

[[noreturn]] void reportUnsupported(const DependencyGraph &Graph,
                                    const std::string &Message) {
  llvm::report_fatal_error(
      llvm::Twine("DepVec cannot compute interferences in function '") +
      Graph.function->getName() + "': " + Message);
}

std::string describeEdge(const DependencyGraph &Graph, unsigned Index) {
  const DependencyGraph::DependencyEdge &Edge = Graph.edges[Index];
  std::string Description = "the " + Edge.kind + " edge {";
  bool First = true;
  for (unsigned Source : Edge.sources) {
    Description += (First ? "n" : ", n") + std::to_string(Source);
    First = false;
  }
  return Description + "} -> n" + std::to_string(Edge.target);
}

} // namespace

void InterferenceAnalysis::run(DependencyGraph &Graph) {
  // Only blocks reachable from the entry can run.
  llvm::DenseMap<const llvm::BasicBlock *, unsigned> BlockIds;
  std::vector<const llvm::BasicBlock *> Blocks;
  std::vector<const llvm::BasicBlock *> Worklist{
      &Graph.function->getEntryBlock()};
  while (!Worklist.empty()) {
    const llvm::BasicBlock *Block = Worklist.back();
    Worklist.pop_back();
    if (!BlockIds.try_emplace(Block, Blocks.size()).second)
      continue;
    Blocks.push_back(Block);
    for (const llvm::BasicBlock *Next : llvm::successors(Block))
      Worklist.push_back(Next);
  }

  InterferenceProgram Program;
  Program.numNodes = static_cast<unsigned>(Graph.nodes.size());
  Program.entry = 0;
  Program.candidate.assign(Graph.nodes.size(), false);
  // Each non-void instruction assigns its own register, named by its node ID.
  // Arguments and constants are never assigned and add no order.
  Program.uses.resize(Graph.nodes.size());
  Program.defs.resize(Graph.nodes.size());
  for (const llvm::BasicBlock *Block : Blocks) {
    std::vector<unsigned> &Code = Program.code.emplace_back();
    for (const llvm::Instruction &Instruction : *Block) {
      const unsigned Id = Graph.getNodeId(&Instruction);
      Code.push_back(Id);
      Program.candidate[Id] = Graph.nodes[Id].kind ==
                              DependencyGraph::Node::Kind::Instruction;
      if (!Instruction.getType()->isVoidTy())
        Program.defs[Id].push_back(Id);
      for (const llvm::Value *Operand : Instruction.operands())
        if (const auto *Def = llvm::dyn_cast<llvm::Instruction>(Operand))
          Program.uses[Id].push_back(Graph.getNodeId(Def));
    }
    std::vector<unsigned> &Succ = Program.succ.emplace_back();
    for (const llvm::BasicBlock *Next : llvm::successors(Block))
      Succ.push_back(BlockIds.lookup(Next));
  }

  // Edges into unreachable blocks never occur and keep no interferences.
  std::vector<InterferenceEdge> Edges;
  std::vector<unsigned> EdgeIndices;
  for (unsigned Index = 0; Index < Graph.edges.size(); ++Index) {
    const DependencyGraph::DependencyEdge &Edge = Graph.edges[Index];
    const llvm::Instruction *Target = Graph.nodes[Edge.target].instruction;
    if (!BlockIds.contains(Target->getParent()))
      continue;

    InterferenceEdge &Model = Edges.emplace_back();
    EdgeIndices.push_back(Index);
    Model.target = Edge.target;
    const auto *Phi = llvm::dyn_cast<llvm::PHINode>(Target);
    if (Edge.kind == "data" && Phi) {
      Model.selectionBlock = BlockIds.lookup(Phi->getParent());
      for (unsigned K = 0; K < Phi->getNumIncomingValues(); ++K) {
        auto Pred = BlockIds.find(Phi->getIncomingBlock(K));
        if (Pred == BlockIds.end())
          continue;
        const llvm::Value *Value = Phi->getIncomingValue(K);
        unsigned Source;
        if (const auto *Def = llvm::dyn_cast<llvm::Instruction>(Value))
          Source = Graph.getNodeId(Def);
        else if (llvm::isa<llvm::Argument>(Value))
          Source = Graph.initNodeId;
        else
          reportUnsupported(Graph, describeEdge(Graph, Index) +
                                       " is a phi with a constant incoming "
                                       "value, which is not supported");
        Model.selection[Pred->second] = Source;
      }
    } else if (Edge.sources.size() == 1) {
      Model.plainSource = *Edge.sources.begin();
    } else {
      reportUnsupported(Graph, describeEdge(Graph, Index) +
                                   " has more than one source but is not a "
                                   "phi data edge");
    }
  }

  auto Result = computeInterferences(Program, Edges);
  if (const auto *Error = std::get_if<InterferenceError>(&Result)) {
    if (Error->edge)
      reportUnsupported(Graph, describeEdge(Graph, EdgeIndices[*Error->edge]) +
                                   ": " + Error->message);
    reportUnsupported(Graph, Error->message);
  }

  const auto &Interferences =
      std::get<std::vector<std::set<unsigned>>>(Result);
  for (size_t K = 0; K < Edges.size(); ++K)
    Graph.edges[EdgeIndices[K]].interferences = Interferences[K];
}

} // namespace depvec
