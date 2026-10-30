#include "depvec/assertion_parser.hpp"
#include "depvec/verification_conditions.hpp"
#include "depvec/weakest_precondition.hpp"
#include "smtlib_solver.hpp"
#include "z3_smtlib_engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// i1 values and cmpxchg results the formulas below use.
depvec::ValueTypes testTypes() {
  depvec::ValueTypes types;
  types.booleans = {"c", "cmp", "ok", "x1"};
  types.aggregates["r"] = {depvec::ScalarType::Integer, depvec::ScalarType::Boolean};
  return types;
}

depvec::FormulaPtr parse(const std::string &text) {
  return depvec::parseAssertion(text, testTypes());
}

// Whether formula holds in every state, checked with Z3 over its symbols.
bool valid(const depvec::FormulaPtr &formula) {
  depvec::FunctionDFG function;
  depvec::InitNode node{};
  node.precondition = parse("true");
  node.weakest_precondition = formula;
  function.nodes.emplace_back(std::move(node));

  depvec::Z3SmtLibEngine engine;
  depvec::SmtLibSolver solver(depvec::verificationSymbols(function), engine);
  const depvec::Formula negated{depvec::NegationFormula{formula}};
  return solver.check(negated).status == depvec::SolverStatus::Unsat;
}

bool equivalent(const depvec::FormulaPtr &left, const depvec::FormulaPtr &right) {
  return valid(std::make_shared<depvec::Formula>(depvec::Formula{
      depvec::LogicalFormula{depvec::LogicalOperator::Iff, left, right}}));
}

depvec::Instruction compare(std::uint32_t operandWidth) {
  return {depvec::IntegerCompareInstruction{
      {"cmp", {}}, depvec::IntegerPredicate::Equal, depvec::ValueRef{"0", {}},
      depvec::IntegerConstant{"1"}, {operandWidth}}};
}

depvec::Instruction compareExchange(bool weak) {
  return {depvec::CompareExchangeInstruction{
      {"r", {}}, depvec::GlobalPointer{"g"}, depvec::IntegerConstant{"0"},
      depvec::IntegerConstant{"1"}, {32},
      depvec::MemoryOrdering::SequentiallyConsistent,
      depvec::MemoryOrdering::SequentiallyConsistent, weak}};
}

depvec::PhiInstruction phiOf(
    const std::string &result, std::uint32_t width,
    const std::vector<std::pair<depvec::Value, std::string>> &incoming) {
  depvec::PhiInstruction phi;
  phi.result = {result, {}};
  phi.type = {width};
  for (const auto &[value, predecessor] : incoming)
    phi.incoming.push_back({value, {predecessor}});
  return phi;
}

depvec::BranchInstruction branch(std::optional<depvec::Value> condition) {
  return {std::move(condition), {"then"}, depvec::BasicBlockRef{"else"}};
}

} // namespace

TEST_CASE("weakest precondition replaces an icmp result by its comparison") {
  const auto wp = depvec::weakestPrecondition(compare(32), *parse("%cmp = false -> %x = 1"));
  REQUIRE(equivalent(wp, parse("%0 != 1 -> %x = 1")));
}

TEST_CASE("weakest precondition rejects comparisons of i1 values") {
  REQUIRE_THROWS_AS(depvec::weakestPrecondition(compare(1), *parse("true")),
                    std::invalid_argument);
}

TEST_CASE("weakest precondition of alloca is its postcondition") {
  const depvec::Instruction alloca{depvec::AllocaInstruction{{"p"}, {32}}};
  const auto post = parse("%x = 1");
  REQUIRE(equivalent(depvec::weakestPrecondition(alloca, *post), post));
}

TEST_CASE("weakest precondition of a branch guards each arm") {
  const auto onTrue = parse("%x = 1");
  const auto onFalse = parse("%x = 2");

  const auto conditional = depvec::weakestPreconditionOfBranch(
      branch(depvec::ValueRef{"c", {}}), *onTrue, *onFalse);
  REQUIRE(equivalent(conditional, parse("(%c -> %x = 1) && (!%c -> %x = 2)")));

  // The extractor prints i1 true as -1.
  const auto constant = depvec::weakestPreconditionOfBranch(
      branch(depvec::IntegerConstant{"-1"}), *onTrue, *onFalse);
  REQUIRE(equivalent(constant, onTrue));

  const depvec::BranchInstruction unconditional{std::nullopt, {"next"}, std::nullopt};
  REQUIRE(equivalent(
      depvec::weakestPreconditionOfBranch(unconditional, *onTrue, *parse("true")),
      onTrue));
}

TEST_CASE("weakest precondition of extractvalue names a cmpxchg member") {
  const depvec::Instruction old{depvec::ExtractValueInstruction{
      {"old", {}}, {"r", {}}, 0, {32}}};
  REQUIRE(equivalent(depvec::weakestPrecondition(old, *parse("%old = 5")),
                     parse("%r[0] = 5")));

  const depvec::Instruction ok{depvec::ExtractValueInstruction{
      {"ok", {}}, {"r", {}}, 1, {1}}};
  REQUIRE(equivalent(depvec::weakestPrecondition(ok, *parse("!%ok")),
                     parse("!%r[1]")));
}

TEST_CASE("weakest precondition of a strong cmpxchg splits on success") {
  // On success @g holds 1 and the old value was 0; on failure it was not 0.
  const auto post = parse(
      "(%r[1] -> @g |-> 1 && %r[0] = 0) && (!%r[1] -> %r[0] != 0)");
  REQUIRE(valid(depvec::weakestPrecondition(compareExchange(false), *post)));
}

TEST_CASE("weakest precondition of a weak cmpxchg allows spurious failure") {
  const auto post = parse(
      "(%r[1] -> @g |-> 1 && %r[0] = 0) && (!%r[1] -> %r[0] != 0)");
  // A weak cmpxchg can fail with @g = 0, so the postcondition needs @g != 0.
  REQUIRE(equivalent(depvec::weakestPrecondition(compareExchange(true), *post),
                     parse("!(@g |-> 0)")));
}

TEST_CASE("weakest precondition of a phi checks each incoming path") {
  const depvec::PhiInstruction phi = phiOf(
      "x", 32, {{depvec::ValueRef{"a", {}}, "then"}, {depvec::ValueRef{"b", {}}, "else"}});
  const std::vector<depvec::IncomingAssertion> incoming{
      {"then", 1, parse("%a > 0")}, {"else", 2, parse("%b > 5")}};

  REQUIRE(valid(depvec::weakestPreconditionOfPhi(phi, incoming, *parse("%x > 0"))));
  REQUIRE(equivalent(depvec::weakestPreconditionOfPhi(phi, incoming, *parse("%x > 3")),
                     parse("%a > 0 -> %a > 3")));
  REQUIRE_THROWS_AS(depvec::weakestPrecondition({phi}, *parse("true")),
                    std::invalid_argument);
}

TEST_CASE("weakest precondition of an i1 phi substitutes formulas") {
  const depvec::PhiInstruction phi = phiOf(
      "x1", 1, {{depvec::ValueRef{"c", {}}, "then"}, {depvec::IntegerConstant{"-1"}, "else"}});
  REQUIRE(equivalent(depvec::weakestPreconditionOfPhi(phi, {}, *parse("%x1")),
                     parse("%c")));
}
