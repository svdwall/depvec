#include "depvec/weakest_precondition.hpp"

#include "depvec/dfg.hpp"

#include <algorithm>
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
      if (sameValue(node.value, variable))
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

// Replaces the i1 value variable by a formula.
FormulaPtr substituteBoolean(const Formula &formula, const ValueRef &variable,
                             const FormulaPtr &replacement) {
  return std::visit([&](const auto &node) -> FormulaPtr {
    using Form = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Form, BooleanVariable>) {
      if (sameValue(node.value, variable))
        return replacement;
      return makeFormula(node);
    } else if constexpr (std::is_same_v<Form, LogicalFormula>) {
      return makeFormula(LogicalFormula{
          node.op, substituteBoolean(*node.left, variable, replacement),
          substituteBoolean(*node.right, variable, replacement)});
    } else if constexpr (std::is_same_v<Form, NegationFormula>) {
      return makeFormula(NegationFormula{
          substituteBoolean(*node.operand, variable, replacement)});
    } else {
      return makeFormula(node);
    }
  }, formula.node);
}

ComparisonOperator comparisonOperator(IntegerPredicate predicate) {
  switch (predicate) {
  case IntegerPredicate::Equal: return ComparisonOperator::Equal;
  case IntegerPredicate::NotEqual: return ComparisonOperator::NotEqual;
  case IntegerPredicate::Greater: return ComparisonOperator::Greater;
  case IntegerPredicate::GreaterEqual: return ComparisonOperator::GreaterEqual;
  case IntegerPredicate::Less: return ComparisonOperator::Less;
  case IntegerPredicate::LessEqual: return ComparisonOperator::LessEqual;
  }
  throw std::logic_error("unknown integer predicate");
}

// An i1 operand as a formula. The extractor prints constants as signed
// decimals, so i1 true is -1.
FormulaPtr booleanValue(const Value &value) {
  if (const auto *ref = std::get_if<ValueRef>(&value))
    return makeFormula(BooleanVariable{*ref});
  const std::string &text = std::get<IntegerConstant>(value).value;
  if (text == "-1" || text == "1") return makeFormula(BooleanLiteral{true});
  if (text == "0") return makeFormula(BooleanLiteral{false});
  throw std::invalid_argument("'" + text + "' is not an i1 constant");
}

FormulaPtr conjunction(FormulaPtr left, FormulaPtr right) {
  return makeFormula(LogicalFormula{LogicalOperator::And, std::move(left),
                                    std::move(right)});
}

FormulaPtr implication(FormulaPtr left, FormulaPtr right) {
  return makeFormula(LogicalFormula{LogicalOperator::Implies, std::move(left),
                                    std::move(right)});
}

// Bit widths are ignored, except that i1 values are booleans. Integer
// operations on i1 have no counterpart in the assertion AST.
void requireInteger(IntegerType type, const char *instruction) {
  if (type.bitWidth == 1)
    throw std::invalid_argument(
        std::string("weakest precondition for ") + instruction +
        " on i1 values is not supported");
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
      requireInteger(operation.type, "arithmetic");
      auto replacement = makeExpression(ArithmeticBinaryExpression{
          arithmeticOperator(operation.op), valueExpression(operation.left),
          valueExpression(operation.right)});
      return substituteFormula(postcondition, operation.result, replacement);
    } else if constexpr (std::is_same_v<Op, IntegerCompareInstruction>) {
      requireInteger(operation.operandType, "comparisons");
      auto replacement = makeFormula(ComparisonFormula{
          comparisonOperator(operation.predicate),
          valueExpression(operation.left), valueExpression(operation.right)});
      return substituteBoolean(postcondition, operation.result, replacement);
    } else if constexpr (std::is_same_v<Op, LoadInstruction>) {
      requireInteger(operation.type, "loads");
      auto replacement = makeExpression(Dereference{operation.pointer});
      return substituteFormula(postcondition, operation.result, replacement);
    } else if constexpr (std::is_same_v<Op, StoreInstruction>) {
      // Stores update a location, so replace matching dereferences only.
      requireInteger(operation.type, "stores");
      return substituteMemory(postcondition, operation.pointer,
                              valueExpression(operation.value));
    } else if constexpr (std::is_same_v<Op, AllocaInstruction>) {
      // An alloca assigns no value that an assertion can name.
      return makeFormula(postcondition.node);
    } else if constexpr (std::is_same_v<Op, ExtractValueInstruction>) {
      // Field 0 is the old value and field 1 the success flag of a cmpxchg.
      const ValueRef member{operation.aggregate.name, operation.index};
      if (operation.index == 1)
        return substituteBoolean(postcondition, operation.result,
                                 makeFormula(BooleanVariable{member}));
      return substituteFormula(postcondition, operation.result,
                               makeExpression(Variable{member}));
    } else if constexpr (std::is_same_v<Op, CompareExchangeInstruction>) {
      requireInteger(operation.type, "cmpxchg");
      const ValueRef old{operation.result.name, 0};
      const ValueRef success{operation.result.name, 1};
      const ExpressionPtr current = makeExpression(Dereference{operation.pointer});
      // The result names the value before the exchange, so substitute it after
      // any store: the dereferences left then refer to the old memory.
      auto outcome = [&](const Formula &formula, bool succeeded) {
        const FormulaPtr named = substituteFormula(formula, old, current);
        return substituteBoolean(*named, success,
                                 makeFormula(BooleanLiteral{succeeded}));
      };
      const FormulaPtr stored = substituteMemory(
          postcondition, operation.pointer, valueExpression(operation.replacement));
      const FormulaPtr matches = makeFormula(ComparisonFormula{
          ComparisonOperator::Equal, current, valueExpression(operation.expected)});
      const FormulaPtr onSuccess = implication(matches, outcome(*stored, true));
      // A weak cmpxchg may fail even when the values match.
      FormulaPtr onFailure = outcome(postcondition, false);
      if (!operation.isWeak)
        onFailure = implication(makeFormula(NegationFormula{matches}),
                                std::move(onFailure));
      return conjunction(onSuccess, std::move(onFailure));
    } else if constexpr (std::is_same_v<Op, PhiInstruction>) {
      throw std::invalid_argument(
          "the weakest precondition of a phi needs its incoming assertions");
    } else {
      throw std::invalid_argument(
          "the weakest precondition of a branch needs both postconditions");
    }
  }, instruction.node);
}

FormulaPtr weakestPreconditionOfBranch(const BranchInstruction &branch,
                                       const Formula &truePostcondition,
                                       const Formula &falsePostcondition) {
  if (!branch.condition)
    return makeFormula(truePostcondition.node);
  // Each arm is guarded by its condition, so a path never continues with the
  // other arm's postcondition.
  const FormulaPtr condition = booleanValue(*branch.condition);
  return makeFormula(LogicalFormula{
      LogicalOperator::Or,
      conjunction(condition, makeFormula(truePostcondition.node)),
      conjunction(makeFormula(NegationFormula{condition}),
                  makeFormula(falsePostcondition.node))});
}

FormulaPtr weakestPreconditionOfPhi(
    const PhiInstruction &phi, const std::vector<IncomingAssertion> &incoming,
    const Formula &postcondition) {
  FormulaPtr result;
  std::vector<std::string> seen;
  for (const PhiIncoming &entry : phi.incoming) {
    const std::string &predecessor = entry.predecessor.name;
    if (std::find(seen.begin(), seen.end(), predecessor) != seen.end())
      continue;
    seen.push_back(predecessor);

    FormulaPtr assertion = makeFormula(BooleanLiteral{true});
    for (const IncomingAssertion &candidate : incoming)
      if (candidate.predecessor == predecessor && candidate.assertion)
        assertion = candidate.assertion;
    const FormulaPtr path = phi.type.bitWidth == 1
        ? substituteBoolean(postcondition, phi.result, booleanValue(entry.value))
        : substituteFormula(postcondition, phi.result, valueExpression(entry.value));
    FormulaPtr guarded = implication(std::move(assertion), path);
    result = result.get() != nullptr
                 ? conjunction(std::move(result), std::move(guarded))
                 : std::move(guarded);
  }
  // FormulaPtr overloads ! to build a negation, so compare with nullptr.
  if (result.get() == nullptr)
    throw std::invalid_argument("phi has no incoming values");
  return result;
}

namespace {

// The incoming assertions of the data hyperedge into a phi node.
std::vector<IncomingAssertion> incomingAssertionsOf(const FunctionDFG &function,
                                                    NodeId phi) {
  for (const DependencyEdge &edge : function.edges)
    if (edge.target == phi && !edge.incomingAssertions.empty())
      return edge.incomingAssertions;
  return {};
}

template <typename Node, typename Compute>
void computeInto(Node &node, Compute compute) {
  try {
    node.weakest_precondition = compute();
    node.weakest_precondition_status = "computed";
  } catch (const std::exception &error) {
    node.weakest_precondition_status =
        std::string("not computed: ") + error.what();
  }
}

} // namespace

void generateWeakestPreconditions(DFG &dfg) {
  for (FunctionDFG &function : dfg.functions) {
    for (GraphNode &graphNode : function.nodes) {
      std::visit([](auto &node) {
        node.weakest_precondition.reset();
        node.weakest_precondition_status.clear();
      }, graphNode);

      if (auto *branchNode = std::get_if<BranchNode>(&graphNode)) {
        if (branchNode->true_postcondition.get() == nullptr ||
            branchNode->false_postcondition.get() == nullptr) {
          branchNode->weakest_precondition_status = "postcondition is missing";
          continue;
        }
        computeInto(*branchNode, [&] {
          return weakestPreconditionOfBranch(branchNode->instruction,
                                             *branchNode->true_postcondition,
                                             *branchNode->false_postcondition);
        });
        continue;
      }

      auto *instructionNode = std::get_if<InstructionNode>(&graphNode);
      if (!instructionNode) {
        std::visit([](auto &node) {
          node.weakest_precondition_status = "not an instruction node";
        }, graphNode);
      } else if (instructionNode->postcondition.get() == nullptr) {
        instructionNode->weakest_precondition_status =
            "postcondition is missing";
      } else if (const auto *phi = std::get_if<PhiInstruction>(
                     &instructionNode->instruction.node)) {
        computeInto(*instructionNode, [&] {
          return weakestPreconditionOfPhi(
              *phi, incomingAssertionsOf(function, instructionNode->id),
              *instructionNode->postcondition);
        });
      } else {
        computeInto(*instructionNode, [&] {
          return weakestPrecondition(instructionNode->instruction,
                                     *instructionNode->postcondition);
        });
      }
    }
  }
}

} // namespace depvec
