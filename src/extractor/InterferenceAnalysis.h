#ifndef DEPVEC_INTERFERENCE_ANALYSIS_H
#define DEPVEC_INTERFERENCE_ANALYSIS_H

#include "DependencyGraphAnalysis.h"

namespace depvec {

// Fills the interference set of every edge, as defined in INTERFERENCE.md.
// A phi data edge is guarded by the phi's block; every other edge is plain.
// An edge outside that model is a fatal error.
class InterferenceAnalysis {
public:
  static void run(DependencyGraph &Graph);
};

} // namespace depvec

#endif
