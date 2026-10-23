#include "depvec/weakest_precondition.hpp"

#include "depvec/dfg.hpp"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace depvec {
namespace {

ExpressionPtr makeExpression(Expression::Node node) {
  return std::make_shared<Expression>(Expression{std::move(node)});
}

FormulaPtr makeFormula(Formula::Node node) {
  return std::make_shared<Formula>(Formula{std::move(node)});
}

ExpressionPtr valueExpression(const Value &value) {
  return std::visit([](const auto &operand) -> ExpressionPtr {
    using Operand = std::decay_t<decltype(operand)>;
    if constexpr (std::is_same_v<Operand, ValueRef>) {
      return makeExpression(Variable{operand});
    } else {
      try {
        std::size_t parsed = 0;
        const auto integer = std::stoll(operand.value, &parsed, 10);
        if (parsed != operand.value.size())
          throw std::invalid_argument("not a decimal integer");
        return makeExpression(IntegerLiteral{integer});
      } catch (const std::exception &) {
        throw std::invalid_argument(
            "weakest precondition cannot represent integer constant '" +
            operand.value + "' in the assertion AST");
      }
    }
  }, value);
}

bool samePointer(const Pointer &left, const Pointer &right) {
  if (left.index() != right.index())
    return false;
  return std::visit([&](const auto &pointer) {
    using Ref = std::decay_t<decltype(pointer)>;
    const auto *other = std::get_if<Ref>(&right);
    if (!other)
      return false;
    if constexpr (std::is_same_v<Ref, AllocaPointer>)
      return pointer.allocaSite == other->allocaSite;
    else
      return pointer.globalName == other->globalName;
  }, left);
}

ExpressionPtr substituteExpression(
    const Expression &expression, const ValueRef &variable,
    const ExpressionPtr &replacement, const Pointer *storedPointer = nullptr,
    const ExpressionPtr &storedValue = {}) {
  return std::visit([&](const auto &node) -> ExpressionPtr {
    using Expr = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Expr, Variable>) {
      if (node.value.name == variable.name)
        return replacement;
      return makeExpression(node);
    } else if constexpr (std::is_same_v<Expr, Dereference>) {
      if (storedPointer && samePointer(node.pointer, *storedPointer))
        return storedValue;
      return makeExpression(node);
    } else if constexpr (std::is_same_v<Expr, ArithmeticUnaryExpression>) {
      return makeExpression(ArithmeticUnaryExpression{
          node.op, substituteExpression(*node.operand, variable, replacement,
                                        storedPointer, storedValue)});
    } else if constexpr (std::is_same_v<Expr, ArithmeticBinaryExpression>) {
      return makeExpression(ArithmeticBinaryExpression{
          node.op,
          substituteExpression(*node.left, variable, replacement,
                               storedPointer, storedValue),
          substituteExpression(*node.right, variable, replacement,
                               storedPointer, storedValue)});
    } else {
      return makeExpression(node);
    }
  }, expression.node);
}

ExpressionPtr substituteMemory(Expression const &expression,
                               const Pointer &pointer,
                               const ExpressionPtr &value) {
  return std::visit([&](const auto &node) -> ExpressionPtr {
    using Expr = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Expr, Dereference>) {
      if (samePointer(node.pointer, pointer))
        return value;
      return makeExpression(node);
    } else if constexpr (std::is_same_v<Expr, ArithmeticUnaryExpression>) {
      return makeExpression(ArithmeticUnaryExpression{
          node.op, substituteMemory(*node.operand, pointer, value)});
    } else if constexpr (std::is_same_v<Expr, ArithmeticBinaryExpression>) {
      return makeExpression(ArithmeticBinaryExpression{
          node.op, substituteMemory(*node.left, pointer, value),
          substituteMemory(*node.right, pointer, value)});
    } else {
      return makeExpression(node);
    }
  }, expression.node);
}

FormulaPtr substituteMemory(const Formula &formula, const Pointer &pointer,
                            const ExpressionPtr &value) {
  return std::visit([&](const auto &node) -> FormulaPtr {
    using Form = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Form, ComparisonFormula>) {
      return makeFormula(ComparisonFormula{
          node.op, substituteMemory(*node.left, pointer, value),
          substituteMemory(*node.right, pointer, value)});
    } else if constexpr (std::is_same_v<Form, LogicalFormula>) {
      return makeFormula(LogicalFormula{
          node.op, substituteMemory(*node.left, pointer, value),
          substituteMemory(*node.right, pointer, value)});
    } else if constexpr (std::is_same_v<Form, NegationFormula>) {
      return makeFormula(NegationFormula{
          substituteMemory(*node.operand, pointer, value)});
    } else {
      return makeFormula(node);
    }
  }, formula.node);
}

FormulaPtr substituteFormula(
    const Formula &formula, const ValueRef &variable,
    const ExpressionPtr &replacement, const Pointer *storedPointer = nullptr,
    const ExpressionPtr &storedValue = {}) {
  return std::visit([&](const auto &node) -> FormulaPtr {
    using Form = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Form, ComparisonFormula>) {
      return makeFormula(ComparisonFormula{
          node.op,
          substituteExpression(*node.left, variable, replacement,
                               storedPointer, storedValue),
          substituteExpression(*node.right, variable, replacement,
                               storedPointer, storedValue)});
    } else if constexpr (std::is_same_v<Form, LogicalFormula>) {
      return makeFormula(LogicalFormula{
          node.op,
          substituteFormula(*node.left, variable, replacement, storedPointer,
                            storedValue),
          substituteFormula(*node.right, variable, replacement, storedPointer,
                            storedValue)});
    } else if constexpr (std::is_same_v<Form, NegationFormula>) {
      return makeFormula(NegationFormula{substituteFormula(
          *node.operand, variable, replacement, storedPointer, storedValue)});
    } else {
      return makeFormula(node);
    }
  }, formula.node);
}

ArithmeticBinaryOperator arithmeticOperator(IntegerBinaryOperator op) {
  switch (op) {
  case IntegerBinaryOperator::Add: return ArithmeticBinaryOperator::Add;
  case IntegerBinaryOperator::Subtract: return ArithmeticBinaryOperator::Subtract;
  case IntegerBinaryOperator::Multiply: return ArithmeticBinaryOperator::Multiply;
  }
  throw std::logic_error("unknown integer binary operator");
}

} // namespace

FormulaPtr weakestPrecondition(const Instruction &instruction,
                               const Formula &postcondition) {
  return std::visit([&](const auto &operation) -> FormulaPtr {
    using Op = std::decay_t<decltype(operation)>;
    if constexpr (std::is_same_v<Op, IntegerBinaryInstruction>) {
      auto replacement = makeExpression(ArithmeticBinaryExpression{
          arithmeticOperator(operation.op), valueExpression(operation.left),
          valueExpression(operation.right)});
      return substituteFormula(postcondition, operation.result, replacement);
    } else if constexpr (std::is_same_v<Op, IntegerCompareInstruction>) {
      throw std::invalid_argument(
          "weakest precondition for integer comparisons is not supported by "
          "the current assertion AST");
    } else if constexpr (std::is_same_v<Op, LoadInstruction>) {
      auto replacement = makeExpression(Dereference{operation.pointer});
      return substituteFormula(postcondition, operation.result, replacement);
    } else if constexpr (std::is_same_v<Op, StoreInstruction>) {
      // Stores update a location, so replace matching dereferences only.
      return substituteMemory(postcondition, operation.pointer,
                              valueExpression(operation.value));
    } else {
      throw std::invalid_argument(
          "weakest precondition is not implemented for this instruction "
          "kind");
    }
  }, instruction.node);
}

void generateWeakestPreconditions(DFG &dfg) {
  for (FunctionDFG &function : dfg.functions) {
    for (GraphNode &graphNode : function.nodes) {
      std::visit([](auto &node) {
        node.weakest_precondition.reset();
        node.weakest_precondition_status.clear();
      }, graphNode);

      auto *instructionNode = std::get_if<InstructionNode>(&graphNode);
      auto *branchNode = std::get_if<BranchNode>(&graphNode);
      if (branchNode) {
        branchNode->weakest_precondition_status = "branch instruction skipped";
      } else if (!instructionNode) {
        std::visit([](auto &node) {
          node.weakest_precondition_status = "not an instruction node";
        }, graphNode);
      } else if (std::holds_alternative<BranchInstruction>(
                     instructionNode->instruction.node)) {
        instructionNode->weakest_precondition_status =
            "branch instruction skipped";
      } else if (instructionNode->postcondition.get() == nullptr) {
        instructionNode->weakest_precondition_status =
            "postcondition is missing";
      } else {
        try {
          instructionNode->weakest_precondition = weakestPrecondition(
              instructionNode->instruction,
              *instructionNode->postcondition);
          instructionNode->weakest_precondition_status = "computed";
        } catch (const std::exception &error) {
          instructionNode->weakest_precondition_status =
              std::string("not computed: ") + error.what();
        }
      }
    }
  }
}

} // namespace depvec
