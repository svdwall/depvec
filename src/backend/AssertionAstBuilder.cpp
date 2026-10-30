#include "depvec/assertion_ast_builder.hpp"

#include <any>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

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
FormulaPtr makeBooleanVariable(ValueRef value) {
  return std::make_shared<Formula>(Formula{BooleanVariable{std::move(value)}});
}

template <typename T> T as(antlrcpp::Any value) {
  return std::any_cast<T>(std::move(value));
}

// Expression visitors return an ExpressionPtr for integers and a FormulaPtr for
// i1 values and boolean literals.
bool isBoolean(const antlrcpp::Any &value) {
  return value.type() == typeid(FormulaPtr);
}

ExpressionPtr integerOperand(antlrcpp::Any value, const char *use) {
  if (isBoolean(value))
    throw std::runtime_error(std::string("a boolean value cannot be used ") + use);
  return as<ExpressionPtr>(std::move(value));
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
  if (context->localValue()) {
    antlrcpp::Any value = visit(context->localValue());
    if (!isBoolean(value))
      throw std::runtime_error("'%" + context->localValue()->getText() +
                               "' is not an i1 value, so it is not a formula");
    return value;
  }
  return visit(context->formula());
}

antlrcpp::Any AssertionAstBuilder::visitPointsTo(
    DepVecAssertionParser::PointsToContext *context) {
  const bool global = context->GlobalIdentifier() != nullptr;
  const std::string pointerName = global
      ? context->GlobalIdentifier()->getText()
      : context->AllocaIdentifier()->getText();
  ExpressionPtr location = makeDereference(makePointer(pointerName, global));
  ExpressionPtr value =
      integerOperand(visit(context->additive()), "as a memory value");
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
  antlrcpp::Any left = visit(terms[0]);
  antlrcpp::Any right = visit(terms[1]);
  if (isBoolean(left) && isBoolean(right)) {
    if (op != ComparisonOperator::Equal && op != ComparisonOperator::NotEqual)
      throw std::runtime_error("booleans can only be compared with = and !=");
    FormulaPtr equal = makeLogical(LogicalOperator::Iff, as<FormulaPtr>(std::move(left)),
                                   as<FormulaPtr>(std::move(right)));
    if (op == ComparisonOperator::Equal) return equal;
    return makeNegation(std::move(equal));
  }
  if (isBoolean(left) || isBoolean(right))
    throw std::runtime_error("a boolean value cannot be compared with an integer");
  return makeComparison(op, as<ExpressionPtr>(std::move(left)),
                        as<ExpressionPtr>(std::move(right)));
}

antlrcpp::Any AssertionAstBuilder::visitAdditive(
    DepVecAssertionParser::AdditiveContext *context) {
  const auto terms = context->multiplicative();
  if (terms.size() == 1) return visit(terms.front());
  ExpressionPtr result = integerOperand(visit(terms.front()), "in arithmetic");
  for (std::size_t index = 1; index < terms.size(); ++index) {
    ArithmeticBinaryOperator op;
    switch (tokenTypeAt(context->children[2 * index - 1])) {
    case DepVecAssertionParser::PLUS: op = ArithmeticBinaryOperator::Add; break;
    case DepVecAssertionParser::MINUS: op = ArithmeticBinaryOperator::Subtract; break;
    default: throw std::logic_error("unexpected additive operator token");
    }
    result = makeBinary(op, std::move(result),
                        integerOperand(visit(terms[index]), "in arithmetic"));
  }
  return result;
}

antlrcpp::Any AssertionAstBuilder::visitMultiplicative(
    DepVecAssertionParser::MultiplicativeContext *context) {
  const auto terms = context->unary();
  if (terms.size() == 1) return visit(terms.front());
  ExpressionPtr result = integerOperand(visit(terms.front()), "in arithmetic");
  for (std::size_t index = 1; index < terms.size(); ++index) {
    ArithmeticBinaryOperator op;
    switch (tokenTypeAt(context->children[2 * index - 1])) {
    case DepVecAssertionParser::STAR: op = ArithmeticBinaryOperator::Multiply; break;
    case DepVecAssertionParser::SLASH: op = ArithmeticBinaryOperator::Divide; break;
    default: throw std::logic_error("unexpected multiplicative operator token");
    }
    result = makeBinary(op, std::move(result),
                        integerOperand(visit(terms[index]), "in arithmetic"));
  }
  return result;
}

antlrcpp::Any AssertionAstBuilder::visitUnary(
    DepVecAssertionParser::UnaryContext *context) {
  if (context->primary()) return visit(context->primary());
  antlrcpp::Any operand = visit(context->unary());
  if (isBoolean(operand) && context->NOT())
    return makeNegation(as<FormulaPtr>(std::move(operand)));
  return makeUnary(ArithmeticUnaryOperator::Negate,
                   integerOperand(std::move(operand), "in arithmetic"));
}

antlrcpp::Any AssertionAstBuilder::visitPrimary(
    DepVecAssertionParser::PrimaryContext *context) {
  if (context->Integer())
    return makeInteger(std::stoll(context->Integer()->getText()));
  if (context->TRUE()) return makeBoolean(true);
  if (context->FALSE()) return makeBoolean(false);
  if (context->STAR()) {
    if (context->GlobalIdentifier())
      return makeDereference(makePointer(context->GlobalIdentifier()->getText(), true));
    return makeDereference(makePointer(context->LocalIdentifier()->getText(), false));
  }
  if (context->GlobalIdentifier())
    return makeVariable(ValueRef{withoutSigil(context->GlobalIdentifier()->getText())});
  if (context->localValue()) return visit(context->localValue());
  return visit(context->additive());
}

antlrcpp::Any AssertionAstBuilder::visitLocalValue(
    DepVecAssertionParser::LocalValueContext *context) {
  std::string name = withoutSigil(context->LocalIdentifier()->getText());
  const auto aggregate = types_.aggregates.find(name);
  if (!context->Integer()) {
    if (aggregate != types_.aggregates.end())
      throw std::runtime_error("'%" + name + "' is an aggregate; name one of " +
                               "its members, such as %" + name + "[0]");
    if (types_.booleans.count(name))
      return makeBooleanVariable(ValueRef{std::move(name), {}});
    return makeVariable(ValueRef{std::move(name), {}});
  }

  if (aggregate == types_.aggregates.end())
    throw std::runtime_error("'%" + name + "' is not an aggregate, so it has no members");
  const std::string indexText = context->Integer()->getText();
  const std::vector<ScalarType> &members = aggregate->second;
  if (indexText.size() > 9 || std::stoul(indexText) >= members.size())
    throw std::runtime_error("'%" + name + "' has no member " + indexText);
  const auto field = static_cast<std::uint32_t>(std::stoul(indexText));
  ValueRef value{std::move(name), field};
  if (members[field] == ScalarType::Boolean)
    return makeBooleanVariable(std::move(value));
  return makeVariable(std::move(value));
}

} // namespace depvec
