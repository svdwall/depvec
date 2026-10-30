#ifndef DEPVEC_VERIFICATION_CONDITIONS_HPP
#define DEPVEC_VERIFICATION_CONDITIONS_HPP

#include "depvec/dfg.hpp"
#include "depvec/solver.hpp"

namespace depvec {

// Gather the symbols referenced by a function's preconditions, computed
// weakest preconditions, and interference checks. The resulting set is fixed
// for one solver instance.
SolverSymbols verificationSymbols(const FunctionDFG &function);

// Check each node's precondition && !weakest_precondition. Nodes missing either
// formula remain NotChecked and record why no query could be issued. Once every
// instruction and branch node is proved, also check each interference:
// assertion && node precondition && !weakest_precondition.
void checkVerificationConditions(FunctionDFG &function, Solver &solver);

// Whether every instruction and branch node is proved. Init and exit nodes
// have no verification condition.
bool allNodesProved(const FunctionDFG &function);

const char *verificationStatusText(VerificationStatus status);

} // namespace depvec

#endif
