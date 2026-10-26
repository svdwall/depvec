#ifndef DEPVEC_Z3_SMTLIB_ENGINE_HPP
#define DEPVEC_Z3_SMTLIB_ENGINE_HPP

#include "smtlib_solver.hpp"

#include <string>

namespace depvec {

// Persistent SMT-LIB connection to a Z3 executable started with -in -smt2.
// The executable is resolved through PATH unless an explicit path is given.
class Z3SmtLibEngine final : public SmtLibEngine {
public:
  explicit Z3SmtLibEngine(std::string executable = "z3");
  ~Z3SmtLibEngine() override;

  Z3SmtLibEngine(const Z3SmtLibEngine &) = delete;
  Z3SmtLibEngine &operator=(const Z3SmtLibEngine &) = delete;

  std::string execute(std::string_view commands,
                      std::string_view marker) override;

private:
  int socket_ = -1;
  int processId_ = -1;
  std::string outputBuffer_;
};

} // namespace depvec

#endif
