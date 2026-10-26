#include "depvec/assertion_parser.hpp"
#include "smtlib_solver.hpp"
#include "z3_smtlib_engine.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Z3 basic functionality") {
  depvec::Z3SmtLibEngine engine;

  depvec::SolverSymbols symbols;
  symbols.variables.push_back({"x"});
  depvec::SmtLibSolver solver(std::move(symbols), engine);

  const auto satisfiable =
      depvec::parseAssertion("%x > 0 && %x < 3");
  const auto unsatisfiable =
      depvec::parseAssertion("%x > 0 && %x <= 0");

  REQUIRE(solver.check(*satisfiable).status == depvec::SolverStatus::Sat);
  REQUIRE(solver.check(*unsatisfiable).status == depvec::SolverStatus::Unsat);
}
