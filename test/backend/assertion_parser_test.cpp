#include "depvec/assertion_parser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>

TEST_CASE("assertion parser builds a comparison formula") {
  const auto formula = depvec::parseAssertion("%x > 0");

  REQUIRE(formula != nullptr);
  REQUIRE(std::holds_alternative<depvec::ComparisonFormula>(formula->node));
}
