#ifndef DEPVEC_WEAKEST_PRECONDITION_HPP
#define DEPVEC_WEAKEST_PRECONDITION_HPP

#include "depvec/assertion_ast.hpp"
#include "depvec/dfg.hpp"

namespace depvec {

// Compute the weakest precondition for one supported instruction. Throws
// std::invalid_argument when the instruction has no implementation yet.
FormulaPtr weakestPrecondition(const Instruction &instruction,
                               const Formula &postcondition);

void generateWeakestPreconditions(DFG &dfg);

} // namespace depvec

#endif
