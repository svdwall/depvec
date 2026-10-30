#ifndef DEPVEC_WEAKEST_PRECONDITION_HPP
#define DEPVEC_WEAKEST_PRECONDITION_HPP

#include "depvec/assertion_ast.hpp"
#include "depvec/dfg.hpp"

#include <vector>

namespace depvec {

// Compute the weakest precondition for one supported instruction. Throws
// std::invalid_argument when the instruction has no implementation yet.
FormulaPtr weakestPrecondition(const Instruction &instruction,
                               const Formula &postcondition);

// A branch has one postcondition per arm. Without a condition, only the true
// postcondition applies.
FormulaPtr weakestPreconditionOfBranch(const BranchInstruction &branch,
                                       const Formula &truePostcondition,
                                       const Formula &falsePostcondition);

// A phi takes its value from the predecessor the path came from, under that
// path's incoming assertion. A predecessor without one has assertion true.
FormulaPtr weakestPreconditionOfPhi(
    const PhiInstruction &phi, const std::vector<IncomingAssertion> &incoming,
    const Formula &postcondition);

void generateWeakestPreconditions(DFG &dfg);

} // namespace depvec

#endif
