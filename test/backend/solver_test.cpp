#include "depvec/assertion_parser.hpp"
#include "smtlib_solver.hpp"
#include "z3_smtlib_engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

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

TEST_CASE("Z3 checks i1 values as booleans") {
  depvec::Z3SmtLibEngine engine;

  depvec::SolverSymbols symbols;
  symbols.variables.push_back({"x"});
  symbols.booleanVariables.push_back({"b"});
  depvec::SmtLibSolver solver(std::move(symbols), engine);
  const depvec::ValueTypes booleans{{"b"}, {}};

  REQUIRE(solver.check(*depvec::parseAssertion("%b && %x = 1", booleans)).status ==
          depvec::SolverStatus::Sat);
  REQUIRE(solver.check(*depvec::parseAssertion("%b = false && %b", booleans)).status ==
          depvec::SolverStatus::Unsat);
  REQUIRE(solver.check(*depvec::parseAssertion("%b != true && %b = false", booleans)).status ==
          depvec::SolverStatus::Sat);
}

TEST_CASE("Z3 gives each cmpxchg member its own symbol") {
  depvec::Z3SmtLibEngine engine;

  depvec::SolverSymbols symbols;
  symbols.variables.push_back({"r", 0});
  symbols.booleanVariables.push_back({"r", 1});
  depvec::SmtLibSolver solver(std::move(symbols), engine);
  depvec::ValueTypes types;
  types.aggregates["r"] = {depvec::ScalarType::Integer, depvec::ScalarType::Boolean};

  REQUIRE(solver.check(*depvec::parseAssertion("%r[0] = 3 && %r[1]", types)).status ==
          depvec::SolverStatus::Sat);
  REQUIRE(solver.check(*depvec::parseAssertion("%r[1] && !%r[1]", types)).status ==
          depvec::SolverStatus::Unsat);
}
