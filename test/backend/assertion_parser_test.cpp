#include "depvec/assertion_parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <variant>

TEST_CASE("assertion parser builds a comparison formula") {
  const auto formula = depvec::parseAssertion("%x > 0");

  REQUIRE(formula != nullptr);
  REQUIRE(std::holds_alternative<depvec::ComparisonFormula>(formula->node));
}

TEST_CASE("assertion parser treats i1 values as booleans") {
  const depvec::ValueTypes booleans{{"cmp"}, {}};

  const auto equal = depvec::parseAssertion("%cmp = false", booleans);
  const auto *iff = std::get_if<depvec::LogicalFormula>(&equal->node);
  REQUIRE(iff != nullptr);
  REQUIRE(iff->op == depvec::LogicalOperator::Iff);
  REQUIRE(std::holds_alternative<depvec::BooleanVariable>(iff->left->node));

  const auto bare = depvec::parseAssertion("!%cmp", booleans);
  const auto *negation = std::get_if<depvec::NegationFormula>(&bare->node);
  REQUIRE(negation != nullptr);
  REQUIRE(std::holds_alternative<depvec::BooleanVariable>(negation->operand->node));
}

TEST_CASE("assertion parser rejects mixing booleans and integers") {
  const depvec::ValueTypes booleans{{"cmp"}, {}};
  REQUIRE_THROWS_AS(depvec::parseAssertion("%cmp = 1", booleans), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%cmp + 1 = 2", booleans), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%cmp < true", booleans), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%cmp = false"), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%x"), std::runtime_error);
}

TEST_CASE("assertion parser names the members of a cmpxchg result") {
  depvec::ValueTypes types;
  types.aggregates["r"] = {depvec::ScalarType::Integer, depvec::ScalarType::Boolean};

  const auto formula = depvec::parseAssertion("%r[0] = 1 && %r[1]", types);
  const auto *conjunction = std::get_if<depvec::LogicalFormula>(&formula->node);
  REQUIRE(conjunction != nullptr);
  const auto *success = std::get_if<depvec::BooleanVariable>(&conjunction->right->node);
  REQUIRE(success != nullptr);
  REQUIRE(success->value.name == "r");
  REQUIRE(success->value.field == 1U);

  REQUIRE_THROWS_AS(depvec::parseAssertion("%r = 1", types), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%r[2] = 1", types), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%x[0] = 1", types), std::runtime_error);
  REQUIRE_THROWS_AS(depvec::parseAssertion("%r[0]", types), std::runtime_error);
}
