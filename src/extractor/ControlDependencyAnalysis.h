#ifndef DEPVEC_CONTROL_DEPENDENCY_ANALYSIS_H
#define DEPVEC_CONTROL_DEPENDENCY_ANALYSIS_H

#include "DependencyGraphAnalysis.h"

namespace llvm {
class DominatorTree;
class PostDominatorTree;
} // namespace llvm

namespace depvec {

// Builds control dependency edges from branch instructions to the first
// instruction in each successor block, and through the region dominated by
// the branch until its post-dominator. Edge values identify the branch arm.
class ControlDependencyAnalysis {
public:
  static std::vector<DependencyGraph::DependencyEdge>
  run(const DependencyGraph &Graph, const llvm::DominatorTree &DT,
      const llvm::PostDominatorTree &PDT);
};

} // namespace depvec

#endif
