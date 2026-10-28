#include "depvec/graph_json_reader.hpp"

#include "depvec/assertion_parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace depvec {
namespace {

using Json = nlohmann::json;

struct InstructionReferences {
  std::unordered_set<std::string> values;
  std::unordered_set<std::string> allocas;
  std::unordered_set<std::string> globals;
};

void collectValue(const Value &value, InstructionReferences &references) {
  if (const auto *ref = std::get_if<ValueRef>(&value))
    references.values.insert(ref->name);
}

void collectPointer(const Pointer &pointer, InstructionReferences &references) {
  std::visit([&](const auto &ref) {
    using Ref = std::decay_t<decltype(ref)>;
    if constexpr (std::is_same_v<Ref, AllocaPointer>)
      references.allocas.insert(ref.allocaSite);
    else
      references.globals.insert(ref.globalName);
  }, pointer);
}

InstructionReferences collectInstructionReferences(const FunctionDFG &function) {
  InstructionReferences references;
  for (const GraphNode &graphNode : function.nodes) {
    const Instruction *instruction = nullptr;
    if (const auto *node = std::get_if<InstructionNode>(&graphNode))
      instruction = &node->instruction;
    else if (const auto *branch = std::get_if<BranchNode>(&graphNode)) {
      if (branch->instruction.condition)
        collectValue(*branch->instruction.condition, references);
      continue;
    }
    if (!instruction) continue;
    std::visit([&](const auto &instruction) {
      using Op = std::decay_t<decltype(instruction)>;
      if constexpr (std::is_same_v<Op, IntegerBinaryInstruction> ||
                    std::is_same_v<Op, IntegerCompareInstruction>) {
        references.values.insert(instruction.result.name);
        collectValue(instruction.left, references);
        collectValue(instruction.right, references);
      } else if constexpr (std::is_same_v<Op, AllocaInstruction>) {
        references.values.insert(instruction.result.allocaSite);
        references.allocas.insert(instruction.result.allocaSite);
      } else if constexpr (std::is_same_v<Op, LoadInstruction>) {
        references.values.insert(instruction.result.name);
        collectPointer(instruction.pointer, references);
      } else if constexpr (std::is_same_v<Op, StoreInstruction>) {
        collectValue(instruction.value, references);
        collectPointer(instruction.pointer, references);
      } else if constexpr (std::is_same_v<Op, CompareExchangeInstruction>) {
        references.values.insert(instruction.result.name);
        collectPointer(instruction.pointer, references);
        collectValue(instruction.expected, references);
        collectValue(instruction.replacement, references);
      } else if constexpr (std::is_same_v<Op, ExtractValueInstruction>) {
        references.values.insert(instruction.result.name);
        references.values.insert(instruction.aggregate.name);
      } else if constexpr (std::is_same_v<Op, BranchInstruction>) {
        if (instruction.condition)
          collectValue(*instruction.condition, references);
      } else if constexpr (std::is_same_v<Op, PhiInstruction>) {
        references.values.insert(instruction.result.name);
        for (const PhiIncoming &incoming : instruction.incoming)
          collectValue(incoming.value, references);
      }
    }, instruction->node);
  }
  return references;
}

void validateExpressionReferences(const Expression &expression,
                                  const InstructionReferences &references,
                                  const std::string &context) {
  std::visit([&](const auto &node) {
    using Expr = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Expr, Variable>) {
      if (!references.values.count(node.value.name))
        throw std::runtime_error(context + ": assertion references unknown LLVM value '%" +
                                 node.value.name + "'");
    } else if constexpr (std::is_same_v<Expr, Dereference>) {
      std::visit([&](const auto &pointer) {
        using Ref = std::decay_t<decltype(pointer)>;
        if constexpr (std::is_same_v<Ref, AllocaPointer>) {
          if (!references.allocas.count(pointer.allocaSite))
            throw std::runtime_error(context + ": assertion dereferences unknown alloca '%" +
                                     pointer.allocaSite + "'");
        } else if (!references.globals.count(pointer.globalName)) {
          throw std::runtime_error(context + ": assertion dereferences unknown global '@" +
                                   pointer.globalName + "'");
        }
      }, node.pointer);
    } else if constexpr (std::is_same_v<Expr, ArithmeticUnaryExpression>) {
      validateExpressionReferences(*node.operand, references, context);
    } else if constexpr (std::is_same_v<Expr, ArithmeticBinaryExpression>) {
      validateExpressionReferences(*node.left, references, context);
      validateExpressionReferences(*node.right, references, context);
    }
  }, expression.node);
}

void validateFormulaReferences(const Formula &formula,
                              const InstructionReferences &references,
                              const std::string &context) {
  std::visit([&](const auto &node) {
    using Expr = std::decay_t<decltype(node)>;
    if constexpr (std::is_same_v<Expr, ComparisonFormula>) {
      validateExpressionReferences(*node.left, references, context);
      validateExpressionReferences(*node.right, references, context);
    } else if constexpr (std::is_same_v<Expr, LogicalFormula>) {
      validateFormulaReferences(*node.left, references, context);
      validateFormulaReferences(*node.right, references, context);
    } else if constexpr (std::is_same_v<Expr, NegationFormula>) {
      validateFormulaReferences(*node.operand, references, context);
    }
  }, formula.node);
}

const Json &field(const Json &object, const char *key,
                  const std::string &context) {
  if (!object.is_object())
    throw std::runtime_error(context + ": expected an object");
  const auto it = object.find(key);
  if (it == object.end())
    throw std::runtime_error(context + ": missing field '" + key + "'");
  return *it;
}

std::string stringField(const Json &object, const char *key,
                        const std::string &context) {
  const Json &value = field(object, key, context);
  if (!value.is_string())
    throw std::runtime_error(context + ": field '" + key + "' must be a string");
  return value.get<std::string>();
}

std::uint64_t unsignedField(const Json &object, const char *key,
                            const std::string &context) {
  const Json &value = field(object, key, context);
  if (!value.is_number_unsigned() && !value.is_number_integer())
    throw std::runtime_error(context + ": field '" + key + "' must be an integer");
  if (value.is_number_unsigned())
    return value.get<std::uint64_t>();
  const auto parsed = value.get<std::int64_t>();
  if (parsed < 0)
    throw std::runtime_error(context + ": field '" + key + "' cannot be negative");
  return static_cast<std::uint64_t>(parsed);
}

std::int64_t signedField(const Json &object, const char *key,
                         const std::string &context) {
  const Json &value = field(object, key, context);
  if (!value.is_number_integer())
    throw std::runtime_error(context + ": field '" + key + "' must be an integer");
  return value.get<std::int64_t>();
}

IntegerType integerType(const Json &object, const char *key,
                        const std::string &context) {
  const auto width = unsignedField(object, key, context);
  if (width == 0 || width > std::numeric_limits<std::uint32_t>::max())
    throw std::runtime_error(context + ": invalid integer width");
  return IntegerType{static_cast<std::uint32_t>(width)};
}

ValueRef valueRef(const Json &value, const std::string &context) {
  if (!value.is_object() || stringField(value, "kind", context) != "ref")
    throw std::runtime_error(context + ": expected a local value reference");
  return ValueRef{stringField(value, "name", context)};
}

ValueRef localField(const Json &object, const char *key,
                    const std::string &context) {
  return ValueRef{stringField(object, key, context)};
}

Value parseValue(const Json &value, const std::string &context) {
  const std::string kind = stringField(value, "kind", context);
  if (kind == "ref")
    return valueRef(value, context);
  if (kind == "integer")
    return IntegerConstant{stringField(value, "value", context)};
  throw std::runtime_error(context + ": unsupported value kind '" + kind + "'");
}

Pointer pointerField(const Json &object, const char *key,
                     const std::string &context) {
  const Json &value = field(object, key, context);
  if (!value.is_object())
    throw std::runtime_error(context + ": field '" + key + "' must identify a pointer");
  const bool hasAlloca = value.contains("alloca");
  const bool hasGlobal = value.contains("global");
  if (hasAlloca == hasGlobal || value.size() != 1)
    throw std::runtime_error(context + ": field '" + key +
                             "' must contain exactly one of 'alloca' or 'global'");
  if (hasAlloca)
    return AllocaPointer{stringField(value, "alloca", context + "." + key)};
  return GlobalPointer{stringField(value, "global", context + "." + key)};
}

MemoryOrdering ordering(const Json &object, const char *key,
                        const std::string &context) {
  const std::string text = stringField(object, key, context);
  if (text == "relaxed" || text == "monotonic")
    return MemoryOrdering::Relaxed;
  if (text == "acquire") return MemoryOrdering::Acquire;
  if (text == "release") return MemoryOrdering::Release;
  if (text == "acq_rel") return MemoryOrdering::AcquireRelease;
  if (text == "seq_cst") return MemoryOrdering::SequentiallyConsistent;
  throw std::runtime_error(context + ": unsupported memory ordering '" + text + "'");
}

MemoryOrdering loadOrdering(const Json &object, const std::string &context) {
  const auto value = ordering(object, "ordering", context);
  if (value == MemoryOrdering::Release ||
      value == MemoryOrdering::AcquireRelease)
    throw std::runtime_error(context + ": invalid ordering for a load");
  return value;
}

MemoryOrdering storeOrdering(const Json &object, const std::string &context) {
  const auto value = ordering(object, "ordering", context);
  if (value == MemoryOrdering::Acquire ||
      value == MemoryOrdering::AcquireRelease)
    throw std::runtime_error(context + ": invalid ordering for a store");
  return value;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): named after LLVM's operands.
bool validCmpXchgOrderings(MemoryOrdering success, MemoryOrdering failure) {
  if (failure == MemoryOrdering::Relaxed)
    return true;
  if (failure == MemoryOrdering::Acquire)
    return success == MemoryOrdering::Acquire ||
           success == MemoryOrdering::AcquireRelease ||
           success == MemoryOrdering::SequentiallyConsistent;
  if (failure == MemoryOrdering::SequentiallyConsistent)
    return success == MemoryOrdering::SequentiallyConsistent;
  return false;
}

IntegerBinaryOperator binaryOperator(const std::string &opcode,
                                     const std::string &context) {
  if (opcode == "add") return IntegerBinaryOperator::Add;
  if (opcode == "sub") return IntegerBinaryOperator::Subtract;
  if (opcode == "mul") return IntegerBinaryOperator::Multiply;
  throw std::runtime_error(context + ": unsupported integer opcode '" + opcode + "'");
}

IntegerPredicate predicate(const std::string &text,
                           const std::string &context) {
  if (text == "eq") return IntegerPredicate::Equal;
  if (text == "ne") return IntegerPredicate::NotEqual;
  if (text == "ugt" || text == "sgt") return IntegerPredicate::Greater;
  if (text == "uge" || text == "sge") return IntegerPredicate::GreaterEqual;
  if (text == "ult" || text == "slt") return IntegerPredicate::Less;
  if (text == "ule" || text == "sle") return IntegerPredicate::LessEqual;
  throw std::runtime_error(context + ": unsupported integer predicate '" + text + "'");
}

Instruction parseInstruction(const Json &operation,
                             const std::string &context) {
  const std::string opcode = stringField(operation, "opcode", context);
  if (opcode == "unsupported") {
    const std::string reason = stringField(operation, "reason", context);
    throw std::runtime_error(context + ": unsupported instruction: " + reason);
  }

  if (opcode == "add" || opcode == "sub" || opcode == "mul") {
    return Instruction{IntegerBinaryInstruction{
        localField(operation, "result", context),
        binaryOperator(opcode, context),
        parseValue(field(operation, "left", context), context + ".left"),
        parseValue(field(operation, "right", context), context + ".right"),
        integerType(operation, "type", context),
        field(operation, "nsw", context).get<bool>(),
        field(operation, "nuw", context).get<bool>()}};
  }
  if (opcode == "icmp") {
    return Instruction{IntegerCompareInstruction{
        localField(operation, "result", context),
        predicate(stringField(operation, "predicate", context), context),
        parseValue(field(operation, "left", context), context + ".left"),
        parseValue(field(operation, "right", context), context + ".right"),
        integerType(operation, "operandType", context)}};
  }
  if (opcode == "alloca") {
    return Instruction{AllocaInstruction{
        AllocaPointer{stringField(operation, "result", context)},
        integerType(operation, "allocatedType", context)}};
  }
  if (opcode == "load") {
    const auto memoryOrder = loadOrdering(operation, context);
    return Instruction{LoadInstruction{
        localField(operation, "result", context),
        pointerField(operation, "pointer", context),
        integerType(operation, "type", context),
        memoryOrder}};
  }
  if (opcode == "store") {
    const auto memoryOrder = storeOrdering(operation, context);
    return Instruction{StoreInstruction{
        parseValue(field(operation, "value", context), context + ".value"),
        pointerField(operation, "pointer", context),
        integerType(operation, "type", context),
        memoryOrder}};
  }
  if (opcode == "cmpxchg") {
    const auto successOrder = ordering(operation, "successOrdering", context);
    const auto failureOrder = ordering(operation, "failureOrdering", context);
    if (!validCmpXchgOrderings(successOrder, failureOrder))
      throw std::runtime_error(context + ": invalid cmpxchg success/failure ordering pair");
    return Instruction{CompareExchangeInstruction{
        localField(operation, "result", context),
        pointerField(operation, "pointer", context),
        parseValue(field(operation, "expected", context), context + ".expected"),
        parseValue(field(operation, "replacement", context), context + ".replacement"),
        integerType(operation, "type", context),
        successOrder,
        failureOrder,
        field(operation, "weak", context).get<bool>()}};
  }
  if (opcode == "extractvalue") {
    const auto index = unsignedField(operation, "index", context);
    if (index > 1)
      throw std::runtime_error(context + ": only cmpxchg result indices 0 and 1 are supported");
    return Instruction{ExtractValueInstruction{
        localField(operation, "result", context),
        localField(operation, "aggregate", context),
        static_cast<std::uint32_t>(index),
        integerType(operation, "resultType", context)}};
  }
  if (opcode == "br") {
    const Json &condition = field(operation, "condition", context);
    std::optional<Value> parsedCondition;
    if (!condition.is_null())
      parsedCondition = parseValue(condition, context + ".condition");
    std::optional<BasicBlockRef> falseTarget;
    const Json &falseTargetJson = field(operation, "falseTarget", context);
    if (!falseTargetJson.is_null()) {
      if (!falseTargetJson.is_string())
        throw std::runtime_error(context + ": falseTarget must be a string or null");
      falseTarget = BasicBlockRef{falseTargetJson.get<std::string>()};
    }
    if (parsedCondition.has_value() != falseTarget.has_value())
      throw std::runtime_error(context +
                               ": conditional branches need a condition and falseTarget");
    return Instruction{BranchInstruction{
        std::move(parsedCondition),
        BasicBlockRef{stringField(operation, "target", context)},
        std::move(falseTarget)}};
  }
  if (opcode == "phi") {
    const Json &incomingJson = field(operation, "incoming", context);
    if (!incomingJson.is_array())
      throw std::runtime_error(context + ": phi incoming must be an array");
    std::vector<PhiIncoming> incoming;
    incoming.reserve(incomingJson.size());
    for (std::size_t index = 0; index < incomingJson.size(); ++index) {
      const Json &entry = incomingJson[index];
      const std::string entryContext = context + ".incoming[" +
                                       std::to_string(index) + "]";
      incoming.push_back(PhiIncoming{
          parseValue(field(entry, "value", entryContext), entryContext + ".value"),
          BasicBlockRef{stringField(entry, "predecessor", entryContext)}});
    }
    return Instruction{PhiInstruction{
        localField(operation, "result", context),
        integerType(operation, "type", context), std::move(incoming)}};
  }
  throw std::runtime_error(context + ": unknown opcode '" + opcode + "'");
}

GraphNode parseNode(const Json &value, const std::string &functionName) {
  const std::string context = "function '" + functionName + "' node";
  const NodeId id = unsignedField(value, "id", context);
  const std::string kind = stringField(value, "kind", context);
  const auto line = signedField(value, "line", context);
  const auto instructionText = stringField(value, "instruction", context);
  if (kind == "instruction") {
    const std::string instructionContext = context + " " + std::to_string(id);
    Instruction parsed = parseInstruction(
        field(value, "operation", instructionContext), instructionContext);
    if (const auto *branch = std::get_if<BranchInstruction>(&parsed.node))
      return BranchNode{id, line, instructionText, *branch};
    return InstructionNode{id, line, instructionText, std::move(parsed)};
  }
  if (value.contains("operation"))
    throw std::runtime_error(context + ": init and exit nodes cannot have an operation");
  if (kind == "init") return InitNode{id, line, instructionText};
  if (kind == "exit") return ExitNode{id, line, instructionText};
  throw std::runtime_error(context + ": unknown node kind '" + kind + "'");
}

std::vector<NodeId> nodeIdArrayField(const Json &value, const char *name,
                                     const std::string &context) {
  const Json &array = field(value, name, context);
  if (!array.is_array())
    throw std::runtime_error(context + ": " + name + " must be an array");
  std::vector<NodeId> ids;
  ids.reserve(array.size());
  for (const Json &entry : array) {
    if (!entry.is_number_integer() && !entry.is_number_unsigned())
      throw std::runtime_error(context + ": " + name + " entries must be integer node IDs");
    if (entry.is_number_unsigned()) {
      ids.push_back(entry.get<NodeId>());
    } else {
      const auto id = entry.get<std::int64_t>();
      if (id < 0)
        throw std::runtime_error(context + ": " + name + " entries cannot be negative");
      ids.push_back(static_cast<NodeId>(id));
    }
  }
  return ids;
}

DependencyEdge parseEdge(const Json &value, const std::string &functionName) {
  const std::string context = "function '" + functionName + "' edge";
  DependencyEdge edge;
  edge.id = stringField(value, "id", context);
  edge.sources = nodeIdArrayField(value, "sources", context + " '" + edge.id + "'");
  edge.target = unsignedField(value, "target", context + " '" + edge.id + "'");
  edge.kind = stringField(value, "kind", context + " '" + edge.id + "'");
  const Json &values = field(value, "values", context + " '" + edge.id + "'");
  if (!values.is_array())
    throw std::runtime_error(context + " '" + edge.id + "': values must be an array");
  for (const Json &entry : values) {
    if (!entry.is_string())
      throw std::runtime_error(context + " '" + edge.id + "': values entries must be strings");
    edge.values.push_back(entry.get<std::string>());
  }
  const std::string assertion =
      stringField(value, "assertion", context + " '" + edge.id + "'");
  if (assertion.empty())
    edge.assertion = std::make_shared<Formula>(Formula{BooleanLiteral{true}});
  else {
    try {
      edge.assertion = parseAssertion(assertion);
    } catch (const std::exception &error) {
      throw std::runtime_error(context + " '" + edge.id + "' assertion: " +
                               error.what());
    }
  }
  edge.interferences =
      nodeIdArrayField(value, "interferences", context + " '" + edge.id + "'");
  return edge;
}

FormulaPtr conjunctionOfAssertions(const FunctionDFG &function,
                                  NodeId nodeId, bool incoming) {
  FormulaPtr result =
      std::make_shared<Formula>(Formula{BooleanLiteral{true}});
  for (const DependencyEdge &edge : function.edges) {
    const bool connected = incoming
        ? edge.target == nodeId
        : std::find(edge.sources.begin(), edge.sources.end(), nodeId) !=
              edge.sources.end();
    if (!connected)
      continue;
    const FormulaPtr assertion = edge.assertion
        ? edge.assertion
        : std::make_shared<Formula>(Formula{BooleanLiteral{true}});
    result = std::make_shared<Formula>(Formula{LogicalFormula{
        LogicalOperator::And, std::move(result), assertion}});
  }
  return result;
}

FormulaPtr conjunctionOfBranchAssertions(const FunctionDFG &function,
                                         NodeId nodeId,
                                         const std::string &branchValue) {
  FormulaPtr result =
      std::make_shared<Formula>(Formula{BooleanLiteral{true}});
  for (const DependencyEdge &edge : function.edges) {
    if (std::find(edge.sources.begin(), edge.sources.end(), nodeId) ==
            edge.sources.end() ||
        std::find(edge.values.begin(), edge.values.end(), branchValue) ==
            edge.values.end())
      continue;
    const FormulaPtr assertion = edge.assertion
        ? edge.assertion
        : std::make_shared<Formula>(Formula{BooleanLiteral{true}});
    result = std::make_shared<Formula>(Formula{LogicalFormula{
        LogicalOperator::And, std::move(result), assertion}});
  }
  return result;
}

FunctionDFG parseFunction(const Json &value) {
  FunctionDFG function;
  function.name = stringField(value, "name", "function");
  function.ir = stringField(value, "ir", "function '" + function.name + "'");
  const Json &nodes = field(value, "nodes", "function '" + function.name + "'");
  const Json &edges = field(value, "edges", "function '" + function.name + "'");
  if (!nodes.is_array() || !edges.is_array())
    throw std::runtime_error("function '" + function.name +
                             "': nodes and edges must be arrays");

  std::unordered_set<NodeId> nodeIds;
  for (const Json &nodeValue : nodes) {
    GraphNode node = parseNode(nodeValue, function.name);
    const NodeId id = std::visit([](const auto &nodeData) { return nodeData.id; }, node);
    if (!nodeIds.insert(id).second)
      throw std::runtime_error("function '" + function.name + "': duplicate node ID " +
                               std::to_string(id));
    function.nodes.push_back(std::move(node));
  }

  std::unordered_set<std::string> allocaSites;
  for (const GraphNode &node : function.nodes) {
    const auto *instructionNode = std::get_if<InstructionNode>(&node);
    if (!instructionNode)
      continue;
    if (const auto *alloca = std::get_if<AllocaInstruction>(
            &instructionNode->instruction.node))
      allocaSites.insert(alloca->result.allocaSite);
  }
  for (const GraphNode &node : function.nodes) {
    const auto *instructionNode = std::get_if<InstructionNode>(&node);
    if (!instructionNode)
      continue;
    const std::string context = "function '" + function.name + "' node " +
                                std::to_string(instructionNode->id);
    auto requireKnownAlloca = [&](const Pointer &pointer) {
      const auto *alloca = std::get_if<AllocaPointer>(&pointer);
      if (alloca && !allocaSites.count(alloca->allocaSite))
        throw std::runtime_error(context + ": pointer refers to unknown alloca '%" +
                                 alloca->allocaSite + "'");
    };
    if (const auto *load =
            std::get_if<LoadInstruction>(&instructionNode->instruction.node))
      requireKnownAlloca(load->pointer);
    else if (const auto *store =
                 std::get_if<StoreInstruction>(&instructionNode->instruction.node))
      requireKnownAlloca(store->pointer);
    else if (const auto *cmpxchg =
                 std::get_if<CompareExchangeInstruction>(
                     &instructionNode->instruction.node))
      requireKnownAlloca(cmpxchg->pointer);
  }

  const InstructionReferences instructionReferences =
      collectInstructionReferences(function);

  std::unordered_set<std::string> edgeIds;
  for (const Json &edgeValue : edges) {
    DependencyEdge edge = parseEdge(edgeValue, function.name);
    validateFormulaReferences(
        *edge.assertion, instructionReferences,
        "function '" + function.name + "' edge '" + edge.id + "' assertion");
    if (!edgeIds.insert(edge.id).second)
      throw std::runtime_error("function '" + function.name + "': duplicate edge ID '" +
                               edge.id + "'");
    if (!nodeIds.count(edge.target))
      throw std::runtime_error("function '" + function.name + "' edge '" + edge.id +
                               "': target node " + std::to_string(edge.target) +
                               " does not exist");
    for (NodeId source : edge.sources) {
      if (!nodeIds.count(source))
        throw std::runtime_error("function '" + function.name + "' edge '" + edge.id +
                                 "': source node " + std::to_string(source) +
                                 " does not exist");
    }
    for (NodeId interference : edge.interferences) {
      if (!nodeIds.count(interference))
        throw std::runtime_error("function '" + function.name + "' edge '" + edge.id +
                                 "': interference node " +
                                 std::to_string(interference) + " does not exist");
    }
    function.edges.push_back(std::move(edge));
  }

  for (GraphNode &node : function.nodes) {
    std::visit([&](auto &nodeData) {
      nodeData.precondition = conjunctionOfAssertions(function, nodeData.id, true);
      if constexpr (std::is_same_v<std::decay_t<decltype(nodeData)>, BranchNode>) {
        for (const DependencyEdge &edge : function.edges) {
          if (std::find(edge.sources.begin(), edge.sources.end(), nodeData.id) ==
              edge.sources.end())
            continue;
          if (edge.kind != "control")
            throw std::runtime_error("function '" + function.name +
                "' branch node " + std::to_string(nodeData.id) +
                ": outgoing edges must be control edges");
          const bool isTrueBranch = std::find(
              edge.values.begin(), edge.values.end(), TrueBranchValue) !=
              edge.values.end();
          const bool isFalseBranch = std::find(
              edge.values.begin(), edge.values.end(), FalseBranchValue) !=
              edge.values.end();
          const bool isUnconditionalBranch = std::find(
              edge.values.begin(), edge.values.end(),
              UnconditionalBranchValue) != edge.values.end();
          const bool hasValidBranchValue = nodeData.instruction.condition
              ? (isTrueBranch != isFalseBranch && !isUnconditionalBranch)
              : (isUnconditionalBranch && !isTrueBranch && !isFalseBranch);
          if (!hasValidBranchValue)
            throw std::runtime_error("function '" + function.name +
                "' branch node " + std::to_string(nodeData.id) +
                ": outgoing control edge must identify exactly one branch arm");
        }
        if (nodeData.instruction.condition) {
          nodeData.true_postcondition = conjunctionOfBranchAssertions(
              function, nodeData.id, TrueBranchValue);
          nodeData.false_postcondition = conjunctionOfBranchAssertions(
              function, nodeData.id, FalseBranchValue);
        } else {
          nodeData.true_postcondition = conjunctionOfBranchAssertions(
              function, nodeData.id, UnconditionalBranchValue);
          nodeData.false_postcondition =
              std::make_shared<Formula>(Formula{BooleanLiteral{true}});
        }
      } else {
        nodeData.postcondition = conjunctionOfAssertions(function, nodeData.id, false);
      }
    }, node);
  }
  return function;
}

} // namespace

DFG readGraphJson(std::string_view jsonText) {
  Json document;
  try {
    document = Json::parse(jsonText.begin(), jsonText.end());
  } catch (const Json::exception &error) {
    throw std::runtime_error(std::string("invalid graph JSON: ") + error.what());
  }
  if (stringField(document, "format", "graph") != "depvec-graph-v2")
    throw std::runtime_error("graph format must be 'depvec-graph-v2'");

  DFG dfg;
  dfg.module = stringField(document, "module", "graph");
  const Json &functions = field(document, "functions", "graph");
  if (!functions.is_array())
    throw std::runtime_error("graph: functions must be an array");
  for (const Json &functionValue : functions)
    dfg.functions.push_back(parseFunction(functionValue));
  return dfg;
}

DFG readGraphFile(const std::string &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("could not open graph file '" + path + "'");
  const std::string text((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
  return readGraphJson(text);
}

} // namespace depvec
