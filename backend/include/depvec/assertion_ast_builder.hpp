#ifndef DEPVEC_ASSERTION_AST_BUILDER_HPP
#define DEPVEC_ASSERTION_AST_BUILDER_HPP

#include "depvec/assertion_ast.hpp"

#include "DepVecAssertionBaseVisitor.h"

namespace depvec {

class AssertionAstBuilder : public DepVecAssertionBaseVisitor {
public:
  AssertionAst build(DepVecAssertionParser::AssertionContext *context);

  antlrcpp::Any visitFormula(
      DepVecAssertionParser::FormulaContext *context) override;
  antlrcpp::Any visitDisjunction(
      DepVecAssertionParser::DisjunctionContext *context) override;
  antlrcpp::Any visitConjunction(
      DepVecAssertionParser::ConjunctionContext *context) override;
  antlrcpp::Any visitFormulaUnary(
      DepVecAssertionParser::FormulaUnaryContext *context) override;
  antlrcpp::Any visitFormulaPrimary(
      DepVecAssertionParser::FormulaPrimaryContext *context) override;
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
};

} // namespace depvec

#endif
