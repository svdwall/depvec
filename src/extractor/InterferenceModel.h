#ifndef DEPVEC_INTERFERENCE_MODEL_H
#define DEPVEC_INTERFERENCE_MODEL_H

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

// The interference algorithm of INTERFERENCE.md, Section 5, on an abstract
// program. It uses only the standard library, so it can be tested without
// LLVM. The rules mirror formal/Depvec/Static.lean.

namespace depvec {

// A function as blocks of node IDs. Node 0 is the init node and belongs to no
// block. Every other node belongs to at most one block.
struct InterferenceProgram {
  unsigned numNodes = 1;
  unsigned entry = 0;
  // Node IDs of each block in execution order. Every block is non-empty and
  // its last node is the terminator.
  std::vector<std::vector<unsigned>> code;
  std::vector<std::vector<unsigned>> succ;
  // Nodes that may be listed as interferences.
  std::vector<bool> candidate;
  // Registers each node reads and assigns, indexed by node ID. Missing
  // entries are empty.
  std::vector<std::vector<unsigned>> uses;
  std::vector<std::vector<unsigned>> defs;
};

// A plain edge has `plainSource`. Otherwise the edge is guarded: its source
// is `selection[p]` when the path last entered `selectionBlock` from block p.
struct InterferenceEdge {
  unsigned target = 0;
  std::optional<unsigned> plainSource;
  unsigned selectionBlock = 0;
  std::map<unsigned, unsigned> selection;
};

// Why a program or edge is outside the model. `edge` is the index of the
// offending edge, if the problem is with an edge.
struct InterferenceError {
  std::optional<size_t> edge;
  std::string message;
};

// Returns the first problem with the program or an edge that is not well
// formed (Section 1), or nothing if there is none.
std::optional<InterferenceError>
checkInterferenceEdges(const InterferenceProgram &Program,
                       const std::vector<InterferenceEdge> &Edges);

// Returns Interf(e) for every edge, in order, or the error of
// checkInterferenceEdges.
std::variant<InterferenceError, std::vector<std::set<unsigned>>>
computeInterferences(const InterferenceProgram &Program,
                     const std::vector<InterferenceEdge> &Edges);

} // namespace depvec

#endif
