#ifndef DEPVEC_VERIFICATION_CONDITIONS_HPP
#define DEPVEC_VERIFICATION_CONDITIONS_HPP

#include "depvec/dfg.hpp"
#include "depvec/solver.hpp"

namespace depvec {

// Gather the symbols referenced by a function's preconditions and computed
// weakest preconditions. The resulting set is fixed for one solver instance.
SolverSymbols verificationSymbols(const FunctionDFG &function);

// Check each node's precondition && !weakest_precondition. Nodes missing either
// formula remain NotChecked and record why no query could be issued.
void checkVerificationConditions(FunctionDFG &function, Solver &solver);

const char *verificationStatusText(VerificationStatus status);

} // namespace depvec

#endif
