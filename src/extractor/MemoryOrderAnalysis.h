#ifndef DEPVEC_MEMORY_ORDER_ANALYSIS_H
#define DEPVEC_MEMORY_ORDER_ANALYSIS_H

#include "DependencyGraphAnalysis.h"

namespace llvm {
class DominatorTree;
} // namespace llvm

namespace depvec {

struct MemoryOrderEdge {
  unsigned source;
  unsigned target;
  std::string label;
};

struct MemoryOrderGraph {
  std::vector<MemoryOrderEdge> edges;
};

class MemoryOrderAnalysis {
public:
  static std::vector<DependencyGraph::DependencyEdge>
  run(const DependencyGraph &Graph, const llvm::DominatorTree &DT);
};

} // namespace depvec

#endif
