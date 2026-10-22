#ifndef DEPVEC_INSTRUCTION_HPP
#define DEPVEC_INSTRUCTION_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "depvec/ir_reference.hpp"

namespace depvec {

// LLVM integer type. A width of 1 represents i1.
struct IntegerType {
  std::uint32_t bitWidth;
};

// Integer constants are stored as decimal text so the representation is not
// limited to the host machine's integer width. Their type comes from the
// instruction operand type.
struct IntegerConstant {
  std::string value;
};

using Value = std::variant<ValueRef, IntegerConstant>;

struct BasicBlockRef {
  std::string name;
};

enum class IntegerBinaryOperator { Add, Subtract, Multiply };

struct IntegerBinaryInstruction {
  ValueRef result;
  IntegerBinaryOperator op;
  Value left;
  Value right;
  IntegerType type;
  bool noSignedWrap = false;
  bool noUnsignedWrap = false;
};

enum class IntegerPredicate {
  Equal,
  NotEqual,
  Greater,
  GreaterEqual,
  Less,
  LessEqual
};

struct IntegerCompareInstruction {
  ValueRef result; // The result has LLVM type i1.
  IntegerPredicate predicate;
  Value left;
  Value right;
  IntegerType operandType;
};

struct AllocaInstruction {
  AllocaPointer result;
  IntegerType allocatedType;
};

enum class MemoryOrdering {
  // Also represents non-atomic LLVM loads and stores in the current model.
  Relaxed,
  Acquire,
  Release,
  AcquireRelease,
  SequentiallyConsistent
};

struct LoadInstruction {
  ValueRef result;
  Pointer pointer;
  IntegerType type;
  MemoryOrdering ordering;
};

struct StoreInstruction {
  Value value;
  Pointer pointer;
  IntegerType type;
  MemoryOrdering ordering;
};

struct CompareExchangeInstruction {
  ValueRef result; // LLVM's {integer, i1} result aggregate.
  Pointer pointer;
  Value expected;
  Value replacement;
  IntegerType type;
  MemoryOrdering successOrdering;
  MemoryOrdering failureOrdering;
  bool isWeak = false;
};

struct ExtractValueInstruction {
  ValueRef result;
  ValueRef aggregate;
  std::uint32_t index; // 0 extracts the old value; 1 extracts success.
  IntegerType resultType;
};

struct BranchInstruction {
  // With no condition, target is the unconditional destination. With a
  // condition, target is the true destination and falseTarget is required.
  std::optional<Value> condition;
  BasicBlockRef target;
  std::optional<BasicBlockRef> falseTarget;
};

struct PhiIncoming {
  Value value;
  BasicBlockRef predecessor;
};

struct PhiInstruction {
  ValueRef result;
  IntegerType type;
  std::vector<PhiIncoming> incoming;
};

struct Instruction {
  using Node = std::variant<IntegerBinaryInstruction,
                            IntegerCompareInstruction, AllocaInstruction,
                            LoadInstruction, StoreInstruction,
                            CompareExchangeInstruction,
                            ExtractValueInstruction, BranchInstruction,
                            PhiInstruction>;

  Node node;
};

} // namespace depvec

#endif
