#ifndef DEPVEC_ASSERTION_AST_HPP
#define DEPVEC_ASSERTION_AST_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace depvec {

struct Expression;
struct Formula;
using ExpressionPtr = std::shared_ptr<Expression>;
using FormulaPtr = std::shared_ptr<Formula>;

struct IntegerLiteral { std::int64_t value; };
struct Variable { std::string name; };
struct Dereference { std::string name; };

enum class ArithmeticUnaryOperator { Negate };
enum class ArithmeticBinaryOperator { Add, Subtract, Multiply, Divide };

struct ArithmeticUnaryExpression {
  ArithmeticUnaryOperator op;
  ExpressionPtr operand;
};
struct ArithmeticBinaryExpression {
  ArithmeticBinaryOperator op;
  ExpressionPtr left;
  ExpressionPtr right;
};

struct Expression {
  using Node = std::variant<IntegerLiteral, Variable, Dereference,
                            ArithmeticUnaryExpression,
                            ArithmeticBinaryExpression>;
  Node node;
};

struct BooleanLiteral { bool value; };
enum class ComparisonOperator { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
enum class LogicalOperator { And, Or };

struct ComparisonFormula {
  ComparisonOperator op;
  ExpressionPtr left;
  ExpressionPtr right;
};
struct LogicalFormula {
  LogicalOperator op;
  FormulaPtr left;
  FormulaPtr right;
};
struct NegationFormula { FormulaPtr operand; };

struct Formula {
  using Node = std::variant<BooleanLiteral, ComparisonFormula, LogicalFormula,
                            NegationFormula>;
  Node node;
};

constexpr std::string_view operatorText(ArithmeticUnaryOperator op) {
  switch (op) { case ArithmeticUnaryOperator::Negate: return "-"; }
  return "?";
}
constexpr std::string_view operatorText(ArithmeticBinaryOperator op) {
  switch (op) {
  case ArithmeticBinaryOperator::Add: return "+";
  case ArithmeticBinaryOperator::Subtract: return "-";
  case ArithmeticBinaryOperator::Multiply: return "*";
  case ArithmeticBinaryOperator::Divide: return "/";
  }
  return "?";
}
constexpr std::string_view operatorText(ComparisonOperator op) {
  switch (op) {
  case ComparisonOperator::Equal: return "==";
  case ComparisonOperator::NotEqual: return "!=";
  case ComparisonOperator::Less: return "<";
  case ComparisonOperator::LessEqual: return "<=";
  case ComparisonOperator::Greater: return ">";
  case ComparisonOperator::GreaterEqual: return ">=";
  }
  return "?";
}
constexpr std::string_view operatorText(LogicalOperator op) {
  switch (op) {
  case LogicalOperator::And: return "&&";
  case LogicalOperator::Or: return "||";
  }
  return "?";
}

struct AssertionAst { FormulaPtr formula; };

// These operators build formula nodes. Like all overloaded logical operators,
// && and || evaluate both C++ operands; they do not short-circuit.
inline Formula operator&&(Formula left, Formula right) {
  return Formula{LogicalFormula{
      LogicalOperator::And,
      std::make_shared<Formula>(std::move(left)),
      std::make_shared<Formula>(std::move(right))}};
}

inline Formula operator||(Formula left, Formula right) {
  return Formula{LogicalFormula{
      LogicalOperator::Or,
      std::make_shared<Formula>(std::move(left)),
      std::make_shared<Formula>(std::move(right))}};
}

inline Formula operator!(Formula operand) {
  return Formula{NegationFormula{
      std::make_shared<Formula>(std::move(operand))}};
}

inline FormulaPtr operator&&(FormulaPtr left, FormulaPtr right) {
  return std::make_shared<Formula>(
      *std::move(left) && *std::move(right));
}

inline FormulaPtr operator||(FormulaPtr left, FormulaPtr right) {
  return std::make_shared<Formula>(
      *std::move(left) || *std::move(right));
}

inline FormulaPtr operator!(FormulaPtr operand) {
  return std::make_shared<Formula>(!*std::move(operand));
}

} // namespace depvec

#endif
