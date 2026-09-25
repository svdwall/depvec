#ifndef DEPVEC_DEPENDENCY_GRAPH_ANALYSIS_H
#define DEPVEC_DEPENDENCY_GRAPH_ANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"

#include <set>
#include <string>
#include <vector>

namespace llvm {
class Function;
class Instruction;
} // namespace llvm

namespace depvec {

struct DependencyGraph {
  struct Node {
    enum class Kind { Init, Instruction, Exit };
    Kind kind;
    const llvm::Instruction *instruction = nullptr;
  };

  struct DependencyEdge {
    std::set<unsigned> sources;
    unsigned target;
    // The dependency kind identifies the edge type. Data edges also carry SSA
    // register names in values; memory edges leave values empty.
    std::string kind;
    std::set<std::string> values;
  };

  llvm::Function *function = nullptr;
  std::vector<Node> nodes;
  llvm::DenseMap<const llvm::Instruction *, unsigned> nodeIds;
  unsigned initNodeId = 0;
  unsigned exitNodeId = 0;
  std::vector<DependencyEdge> edges;

  unsigned getNodeId(const llvm::Instruction *I) const;
};

class DependencyGraphAnalysis
    : public llvm::AnalysisInfoMixin<DependencyGraphAnalysis> {
public:
  using Result = DependencyGraph;
  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM);

private:
  friend llvm::AnalysisInfoMixin<DependencyGraphAnalysis>;
  static llvm::AnalysisKey Key;
};

} // namespace depvec

#endif
