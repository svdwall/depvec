#ifndef DEPVEC_TEST_UNROLLING_ORACLE_H
#define DEPVEC_TEST_UNROLLING_ORACLE_H

#include "InterferenceModel.h"

#include <cstddef>
#include <set>
#include <utility>
#include <vector>

namespace depvec::test {

// Decides Section 3 of INTERFERENCE.md directly on unrollings. Returns every
// pair (edge index, candidate node) such that, in some path that visits each
// block at most MaxVisits times, an event of the node outside Excl(o) is
// neither before σ(o) nor after o for an occurrence o of the edge. The edges
// must be well formed.
std::set<std::pair<size_t, unsigned>>
unseparatedPairs(const InterferenceProgram &Program,
                 const std::vector<InterferenceEdge> &Edges,
                 unsigned MaxVisits);

} // namespace depvec::test

#endif
