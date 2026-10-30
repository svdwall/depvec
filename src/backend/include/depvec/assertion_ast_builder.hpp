#ifndef DEPVEC_ASSERTION_AST_BUILDER_HPP
#define DEPVEC_ASSERTION_AST_BUILDER_HPP

#include "depvec/assertion_ast.hpp"
#include "depvec/value_types.hpp"

#include "DepVecAssertionBaseVisitor.h"

namespace depvec {

// Builds the AST for one assertion. Boolean local values (LLVM type i1)
// become formulas; integer values become expressions.
class AssertionAstBuilder : public DepVecAssertionBaseVisitor {
public:
  explicit AssertionAstBuilder(const ValueTypes &types) : types_(types) {}

  AssertionAst build(DepVecAssertionParser::AssertionContext *context);

  antlrcpp::Any visitFormula(
      DepVecAssertionParser::FormulaContext *context) override;
  antlrcpp::Any visitImplication(
      DepVecAssertionParser::ImplicationContext *context) override;
  antlrcpp::Any visitDisjunction(
      DepVecAssertionParser::DisjunctionContext *context) override;
  antlrcpp::Any visitConjunction(
      DepVecAssertionParser::ConjunctionContext *context) override;
  antlrcpp::Any visitFormulaUnary(
      DepVecAssertionParser::FormulaUnaryContext *context) override;
  antlrcpp::Any visitFormulaPrimary(
      DepVecAssertionParser::FormulaPrimaryContext *context) override;
  antlrcpp::Any visitPointsTo(
      DepVecAssertionParser::PointsToContext *context) override;
  antlrcpp::Any visitComparison(
      DepVecAssertionParser::ComparisonContext *context) override;
  antlrcpp::Any visitAdditive(
      DepVecAssertionParser::AdditiveContext *context) override;
  antlrcpp::Any visitMultiplicative(
      DepVecAssertionParser::MultiplicativeContext *context) override;
  antlrcpp::Any visitUnary(
      DepVecAssertionParser::UnaryContext *context) override;
  antlrcpp::Any visitPrimary(
      DepVecAssertionParser::PrimaryContext *context) override;
  antlrcpp::Any visitLocalValue(
      DepVecAssertionParser::LocalValueContext *context) override;

private:
  const ValueTypes &types_;
};

} // namespace depvec

#endif
