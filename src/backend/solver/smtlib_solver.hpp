#ifndef DEPVEC_SMTLIB_SOLVER_HPP
#define DEPVEC_SMTLIB_SOLVER_HPP

#include "depvec/solver.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace depvec {

// Transport boundary for a concrete SMT-LIB-speaking solver. Implementations
// own process startup, stdin/stdout, and response framing. execute() submits
// commands followed by an SMT-LIB (echo "marker") command and returns all
// stdout preceding that marker.
class SmtLibEngine {
public:
  virtual ~SmtLibEngine() = default;
  virtual std::string execute(std::string_view commands,
                              std::string_view marker) = 0;
};

// Encodes DepVec formulas using SMT-LIB integer arithmetic and delegates the
// protocol exchange to an engine. The engine must outlive this object.
class SmtLibSolver final : public Solver {
public:
  SmtLibSolver(SolverSymbols symbols, SmtLibEngine &engine);

  SolverResult check(const Formula &formula) override;

  // Used by the AST encoder to resolve symbols from the instance's fixed set.
  std::string symbolForValue(const ValueRef &value) const;
  std::string symbolForPointer(const Pointer &pointer) const;

private:
  std::string encodeFormula(const Formula &formula) const;

  SmtLibEngine &engine_;
  std::unordered_map<std::string, std::string> symbolsByKey_;
  std::uint64_t nextMarker_ = 0;
};

} // namespace depvec

#endif
