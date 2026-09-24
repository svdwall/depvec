#ifndef DEPVEC_INSTRUCTION_GRAPH_ANALYSIS_H
#define DEPVEC_INSTRUCTION_GRAPH_ANALYSIS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

#include <string>
#include <vector>

namespace llvm {
class Function;
class Instruction;
} // namespace llvm

namespace depvec {

class InstructionGraphAnalysis
    : public llvm::AnalysisInfoMixin<InstructionGraphAnalysis> {
public:
  enum class DependencyEdgeFlavour {
    Data,
  };

  // A dependency hyperedge: a set of instruction nodes points to one target.
  struct DependencyEdge {
    llvm::SmallVector<unsigned, 4> sources;
    unsigned target;
    DependencyEdgeFlavour flavour;
    // For Data edges, this is the LLVM IR register name, including its '%'.
    std::string value;
  };

  struct Result {
    llvm::Function *function = nullptr;
    std::vector<const llvm::Instruction *> nodes;
    llvm::DenseMap<const llvm::Instruction *, unsigned> nodeIds;
    std::vector<DependencyEdge> dependencyEdges;

    unsigned getNodeId(const llvm::Instruction *I) const;

    // Every instruction must belong to this graph. Duplicate sources are
    // removed while preserving their order. Data edge values include '%'.
    void addDependencyEdge(
        llvm::ArrayRef<const llvm::Instruction *> sources,
        const llvm::Instruction *target, DependencyEdgeFlavour flavour,
        llvm::StringRef value = {});
  };

  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &);

private:
  friend llvm::AnalysisInfoMixin<InstructionGraphAnalysis>;
  static llvm::AnalysisKey Key;
};

} // namespace depvec

#endif
