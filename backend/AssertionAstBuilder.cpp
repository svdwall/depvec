#include "depvec/assertion_ast_builder.hpp"

#include <any>
#include <stdexcept>
#include <string>
#include <utility>

namespace depvec {
namespace {

ExpressionPtr makeInteger(std::int64_t value) {
  return std::make_shared<Expression>(Expression{IntegerLiteral{value}});
}
ExpressionPtr makeVariable(ValueRef value) {
  return std::make_shared<Expression>(Expression{Variable{std::move(value)}});
}
ExpressionPtr makeDereference(Pointer pointer) {
  return std::make_shared<Expression>(Expression{Dereference{std::move(pointer)}});
}
std::string withoutSigil(std::string name) {
  if (!name.empty() && (name.front() == '%' || name.front() == '@'))
    name.erase(name.begin());
  return name;
}
Pointer makePointer(std::string name, bool global) {
  name = withoutSigil(std::move(name));
  if (global) return GlobalPointer{std::move(name)};
  return AllocaPointer{std::move(name)};
}
ExpressionPtr makeUnary(ArithmeticUnaryOperator op, ExpressionPtr operand) {
  return std::make_shared<Expression>(Expression{
      ArithmeticUnaryExpression{op, std::move(operand)}});
}
ExpressionPtr makeBinary(ArithmeticBinaryOperator op, ExpressionPtr left,
                          ExpressionPtr right) {
  return std::make_shared<Expression>(Expression{
      ArithmeticBinaryExpression{op, std::move(left), std::move(right)}});
}
FormulaPtr makeBoolean(bool value) {
  return std::make_shared<Formula>(Formula{BooleanLiteral{value}});
}
FormulaPtr makeComparison(ComparisonOperator op, ExpressionPtr left,
                          ExpressionPtr right) {
  return std::make_shared<Formula>(Formula{
      ComparisonFormula{op, std::move(left), std::move(right)}});
}
FormulaPtr makeLogical(LogicalOperator op, FormulaPtr left, FormulaPtr right) {
  return std::make_shared<Formula>(Formula{
      LogicalFormula{op, std::move(left), std::move(right)}});
}
FormulaPtr makeNegation(FormulaPtr operand) {
  return std::make_shared<Formula>(Formula{NegationFormula{std::move(operand)}});
}

template <typename T> T as(antlrcpp::Any value) {
  return std::any_cast<T>(std::move(value));
}

std::size_t tokenTypeAt(antlr4::tree::ParseTree *tree) {
  auto *terminal = dynamic_cast<antlr4::tree::TerminalNode *>(tree);
  if (!terminal)
    throw std::logic_error("expected an operator token in parse tree");
  return terminal->getSymbol()->getType();
}

} // namespace

AssertionAst AssertionAstBuilder::build(
    DepVecAssertionParser::AssertionContext *context) {
  return AssertionAst{as<FormulaPtr>(visit(context->formula()))};
}

antlrcpp::Any AssertionAstBuilder::visitFormula(
    DepVecAssertionParser::FormulaContext *context) {
  return visit(context->implication());
}

antlrcpp::Any AssertionAstBuilder::visitImplication(
    DepVecAssertionParser::ImplicationContext *context) {
  FormulaPtr left = as<FormulaPtr>(visit(context->disjunction()));
  if (!context->implication())
    return left;
  return makeLogical(LogicalOperator::Implies, std::move(left),
                     as<FormulaPtr>(visit(context->implication())));
}

antlrcpp::Any AssertionAstBuilder::visitDisjunction(
    DepVecAssertionParser::DisjunctionContext *context) {
  const auto terms = context->conjunction();
  FormulaPtr result = as<FormulaPtr>(visit(terms.front()));
  for (std::size_t index = 1; index < terms.size(); ++index)
    result = makeLogical(LogicalOperator::Or, std::move(result),
                         as<FormulaPtr>(visit(terms[index])));
  return result;
}

antlrcpp::Any AssertionAstBuilder::visitConjunction(
    DepVecAssertionParser::ConjunctionContext *context) {
  const auto terms = context->formulaUnary();
  FormulaPtr result = as<FormulaPtr>(visit(terms.front()));
  for (std::size_t index = 1; index < terms.size(); ++index)
    result = makeLogical(LogicalOperator::And, std::move(result),
                         as<FormulaPtr>(visit(terms[index])));
  return result;
}

antlrcpp::Any AssertionAstBuilder::visitFormulaUnary(
    DepVecAssertionParser::FormulaUnaryContext *context) {
  if (context->NOT())
    return makeNegation(as<FormulaPtr>(visit(context->formulaUnary())));
  return visit(context->formulaPrimary());
}

antlrcpp::Any AssertionAstBuilder::visitFormulaPrimary(
    DepVecAssertionParser::FormulaPrimaryContext *context) {
  if (context->TRUE()) return makeBoolean(true);
  if (context->FALSE()) return makeBoolean(false);
  if (context->pointsTo()) return visit(context->pointsTo());
  if (context->comparison()) return visit(context->comparison());
  return visit(context->formula());
}

antlrcpp::Any AssertionAstBuilder::visitPointsTo(
    DepVecAssertionParser::PointsToContext *context) {
  const bool global = context->GlobalIdentifier() != nullptr;
  const std::string pointerName = global
      ? context->GlobalIdentifier()->getText()
      : context->AllocaIdentifier()->getText();
  ExpressionPtr location = makeDereference(makePointer(pointerName, global));
  ExpressionPtr value = as<ExpressionPtr>(visit(context->additive()));
  return makeComparison(ComparisonOperator::Equal, std::move(location),
                        std::move(value));
}

antlrcpp::Any AssertionAstBuilder::visitComparison(
    DepVecAssertionParser::ComparisonContext *context) {
  ComparisonOperator op;
  switch (tokenTypeAt(context->children[1])) {
  case DepVecAssertionParser::EQ: op = ComparisonOperator::Equal; break;
  case DepVecAssertionParser::NE: op = ComparisonOperator::NotEqual; break;
  case DepVecAssertionParser::LT: op = ComparisonOperator::Less; break;
  case DepVecAssertionParser::LE: op = ComparisonOperator::LessEqual; break;
  case DepVecAssertionParser::GT: op = ComparisonOperator::Greater; break;
  case DepVecAssertionParser::GE: op = ComparisonOperator::GreaterEqual; break;
  default: throw std::logic_error("unexpected comparison operator token");
  }
  const auto terms = context->additive();
  return makeComparison(op, as<ExpressionPtr>(visit(terms[0])),
                        as<ExpressionPtr>(visit(terms[1])));
}

antlrcpp::Any AssertionAstBuilder::visitAdditive(
    DepVecAssertionParser::AdditiveContext *context) {
  const auto terms = context->multiplicative();
  ExpressionPtr result = as<ExpressionPtr>(visit(terms.front()));
  for (std::size_t index = 1; index < terms.size(); ++index) {
    ArithmeticBinaryOperator op;
    switch (tokenTypeAt(context->children[2 * index - 1])) {
    case DepVecAssertionParser::PLUS: op = ArithmeticBinaryOperator::Add; break;
    case DepVecAssertionParser::MINUS: op = ArithmeticBinaryOperator::Subtract; break;
    default: throw std::logic_error("unexpected additive operator token");
    }
    result = makeBinary(op, std::move(result),
                        as<ExpressionPtr>(visit(terms[index])));
  }
  return result;
}

antlrcpp::Any AssertionAstBuilder::visitMultiplicative(
    DepVecAssertionParser::MultiplicativeContext *context) {
  const auto terms = context->unary();
  ExpressionPtr result = as<ExpressionPtr>(visit(terms.front()));
  for (std::size_t index = 1; index < terms.size(); ++index) {
    ArithmeticBinaryOperator op;
    switch (tokenTypeAt(context->children[2 * index - 1])) {
    case DepVecAssertionParser::STAR: op = ArithmeticBinaryOperator::Multiply; break;
    case DepVecAssertionParser::SLASH: op = ArithmeticBinaryOperator::Divide; break;
    default: throw std::logic_error("unexpected multiplicative operator token");
    }
    result = makeBinary(op, std::move(result),
                        as<ExpressionPtr>(visit(terms[index])));
  }
  return result;
}

antlrcpp::Any AssertionAstBuilder::visitUnary(
    DepVecAssertionParser::UnaryContext *context) {
  if (context->primary()) return visit(context->primary());
  return makeUnary(ArithmeticUnaryOperator::Negate,
                   as<ExpressionPtr>(visit(context->unary())));
}

antlrcpp::Any AssertionAstBuilder::visitPrimary(
    DepVecAssertionParser::PrimaryContext *context) {
  if (context->Integer())
    return makeInteger(std::stoll(context->Integer()->getText()));
  if (context->STAR()) {
    if (context->GlobalIdentifier())
      return makeDereference(makePointer(context->GlobalIdentifier()->getText(), true));
    return makeDereference(makePointer(context->LocalIdentifier()->getText(), false));
  }
  if (context->GlobalIdentifier())
    return makeVariable(ValueRef{withoutSigil(context->GlobalIdentifier()->getText())});
  if (context->LocalIdentifier())
    return makeVariable(ValueRef{withoutSigil(context->LocalIdentifier()->getText())});
  return visit(context->additive());
}

} // namespace depvec
