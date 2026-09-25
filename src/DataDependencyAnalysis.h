#ifndef DEPVEC_DATA_DEPENDENCY_ANALYSIS_H
#define DEPVEC_DATA_DEPENDENCY_ANALYSIS_H

#include "DependencyGraphAnalysis.h"

namespace depvec {

// Builds step-one SSA def-use edges and folds them into step-two data
// dependency edges. Node ownership and construction remain with the graph.
class DataDependencyAnalysis {
public:
  static std::vector<DependencyGraph::DependencyEdge>
  run(const DependencyGraph &Graph);
};

} // namespace depvec

#endif
