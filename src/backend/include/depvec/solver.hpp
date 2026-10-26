#ifndef DEPVEC_SOLVER_HPP
#define DEPVEC_SOLVER_HPP

#include "depvec/assertion_ast.hpp"

#include <string>
#include <utility>
#include <vector>

namespace depvec {

// The logical result of a solver query. Solver failures (for example, a
// process exiting or returning malformed output) are reported as exceptions,
// not as Unknown.
enum class SolverStatus { Sat, Unsat, Unknown };

struct SolverResult {
  SolverStatus status;

  // Optional solver-provided explanation, primarily useful when status is
  // Unknown. Empty when the solver has no additional detail.
  std::string reason;
};

// The complete symbol set available to one solver instance. These declarations
// are fixed for that instance's lifetime.
struct SolverSymbols {
  std::vector<ValueRef> variables;
  std::vector<Pointer> memoryLocations;
};

// Backend-independent interface for checking formulas. A concrete
// implementation owns context management and translation to its solver's
// input language.
class Solver {
public:
  explicit Solver(SolverSymbols symbols) : symbols_(std::move(symbols)) {}
  virtual ~Solver() = default;

  // Check whether formula is satisfiable.
  virtual SolverResult check(const Formula &formula) = 0;

protected:
  const SolverSymbols symbols_;
};

} // namespace depvec

#endif
