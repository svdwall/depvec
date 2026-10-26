#include "depvec/assertion_parser.hpp"
#include "depvec/verification_conditions.hpp"
#include "smtlib_solver.hpp"
#include "z3_smtlib_engine.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("verification conditions record solver results and skipped nodes") {
  depvec::FunctionDFG function;
  function.name = "verification_test";

  depvec::InitNode proved{};
  proved.id = 0;
  proved.precondition = depvec::parseAssertion("true");
  proved.weakest_precondition = depvec::parseAssertion("true");
  proved.weakest_precondition_status = "computed";
  function.nodes.emplace_back(std::move(proved));

  depvec::InitNode counterexample{};
  counterexample.id = 1;
  counterexample.precondition = depvec::parseAssertion("true");
  counterexample.weakest_precondition = depvec::parseAssertion("false");
  counterexample.weakest_precondition_status = "computed";
  function.nodes.emplace_back(std::move(counterexample));

  depvec::ExitNode unavailable{};
  unavailable.id = 2;
  unavailable.precondition = depvec::parseAssertion("true");
  unavailable.weakest_precondition_status = "no weakest precondition";
  function.nodes.emplace_back(std::move(unavailable));

  depvec::Z3SmtLibEngine engine;
  depvec::SmtLibSolver solver(depvec::verificationSymbols(function), engine);
  depvec::checkVerificationConditions(function, solver);

  REQUIRE(std::get<depvec::InitNode>(function.nodes[0]).verification.status ==
          depvec::VerificationStatus::Proved);
  REQUIRE(std::get<depvec::InitNode>(function.nodes[1]).verification.status ==
          depvec::VerificationStatus::Counterexample);
  const auto &skipped = std::get<depvec::ExitNode>(function.nodes[2]);
  REQUIRE(skipped.verification.status == depvec::VerificationStatus::NotChecked);
  REQUIRE(skipped.verification.detail == "no weakest precondition");
}
