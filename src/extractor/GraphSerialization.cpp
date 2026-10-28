#include "GraphSerialization.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <system_error>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace depvec {

namespace {

std::string getInstructionText(const Instruction &I) {
  std::string Text;
  raw_string_ostream OS(Text);
  I.print(OS);
  OS.flush();
  return Text;
}

std::vector<std::string> getFunctionIRLines(const Function &F) {
  std::string IR;
  raw_string_ostream OS(IR);
  F.print(OS);
  OS.flush();

  std::vector<std::string> Lines;
  size_t Start = 0;
  while (Start <= IR.size()) {
    const size_t End = IR.find('\n', Start);
    if (End == std::string::npos) {
      Lines.push_back(IR.substr(Start));
      break;
    }
    Lines.push_back(IR.substr(Start, End - Start));
    Start = End + 1;
  }
  return Lines;
}

int findInstructionLine(const Instruction &I,
                        const std::vector<std::string> &Lines,
                        unsigned StartLine) {
  const std::string Text = getInstructionText(I);
  for (unsigned Line = StartLine; Line < Lines.size(); ++Line) {
    if (StringRef(Lines[Line]).trim() == StringRef(Text).trim())
      return static_cast<int>(Line);
  }
  return -1;
}

std::string getLocalName(const Value &V) {
  std::string Name;
  raw_string_ostream OS(Name);
  V.printAsOperand(OS, false);
  OS.flush();
  if (!Name.empty() && Name.front() == '%')
    Name.erase(Name.begin());
  return Name;
}

json::Value serializeValue(const Value &V) {
  if (const auto *Constant = dyn_cast<ConstantInt>(&V)) {
    SmallString<40> Decimal;
    Constant->getValue().toString(Decimal, 10, true);
    json::Object Result;
    Result["kind"] = "integer";
    Result["value"] = Decimal.str().str();
    return Result;
  }
  if (isa<Argument>(V) || isa<Instruction>(V)) {
    json::Object Result;
    Result["kind"] = "ref";
    Result["name"] = getLocalName(V);
    return Result;
  }
  json::Object Unsupported;
  Unsupported["kind"] = "unsupported";
  std::string Text;
  raw_string_ostream OS(Text);
  V.printAsOperand(OS, false);
  OS.flush();
  Unsupported["text"] = std::move(Text);
  return Unsupported;
}

json::Value serializePointer(const Value &V) {
  json::Object Result;
  if (const auto *Alloca = dyn_cast<AllocaInst>(&V)) {
    Result["alloca"] = getLocalName(*Alloca);
    return Result;
  }
  if (const auto *Global = dyn_cast<GlobalVariable>(&V)) {
    Result["global"] = Global->getName();
    return Result;
  }
  Result["unsupported"] = true;
  return Result;
}

bool hasSupportedPointerOrigin(const Value &V) {
  return isa<AllocaInst>(V) || isa<GlobalVariable>(V);
}

json::Value serializeIntegerType(Type *Ty) {
  if (!Ty->isIntegerTy())
    return nullptr;
  return Ty->getIntegerBitWidth();
}

StringRef orderingName(AtomicOrdering Ordering) {
  switch (Ordering) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
  case AtomicOrdering::Monotonic:
    return "relaxed";
  case AtomicOrdering::Acquire:
    return "acquire";
  case AtomicOrdering::Release:
    return "release";
  case AtomicOrdering::AcquireRelease:
    return "acq_rel";
  case AtomicOrdering::SequentiallyConsistent:
    return "seq_cst";
  }
  llvm_unreachable("unknown atomic ordering");
}

json::Object unsupportedOperation(const Instruction &I, StringRef Reason) {
  json::Object Result;
  Result["opcode"] = "unsupported";
  Result["reason"] = (Twine(Reason) + ": " + I.getOpcodeName()).str();
  return Result;
}

json::Object serializeInstruction(const Instruction &I) {
  json::Object Result;
  Result["opcode"] = I.getOpcodeName();

  if (const auto *Binary = dyn_cast<BinaryOperator>(&I)) {
    if (!Binary->getType()->isIntegerTy() ||
        (I.getOpcode() != Instruction::Add &&
         I.getOpcode() != Instruction::Sub &&
         I.getOpcode() != Instruction::Mul))
      return unsupportedOperation(I, "unsupported binary operation or type");
    Result["result"] = getLocalName(I);
    Result["type"] = serializeIntegerType(Binary->getType());
    Result["left"] = serializeValue(*Binary->getOperand(0));
    Result["right"] = serializeValue(*Binary->getOperand(1));
    Result["nsw"] = Binary->hasNoSignedWrap();
    Result["nuw"] = Binary->hasNoUnsignedWrap();
    return Result;
  }
  if (const auto *Compare = dyn_cast<ICmpInst>(&I)) {
    if (!Compare->getOperand(0)->getType()->isIntegerTy())
      return unsupportedOperation(I, "icmp requires scalar integer operands");
    Result["result"] = getLocalName(I);
    Result["predicate"] = ICmpInst::getPredicateName(Compare->getPredicate());
    Result["operandType"] = serializeIntegerType(Compare->getOperand(0)->getType());
    Result["left"] = serializeValue(*Compare->getOperand(0));
    Result["right"] = serializeValue(*Compare->getOperand(1));
    return Result;
  }
  if (const auto *Alloca = dyn_cast<AllocaInst>(&I)) {
    const auto *Count = dyn_cast<ConstantInt>(Alloca->getArraySize());
    if (!Count || !Count->isOne() || !Alloca->getAllocatedType()->isIntegerTy())
      return unsupportedOperation(I, "only scalar integer allocas are supported");
    Result["result"] = getLocalName(I);
    Result["allocatedType"] = serializeIntegerType(Alloca->getAllocatedType());
    return Result;
  }
  if (const auto *Load = dyn_cast<LoadInst>(&I)) {
    if (!Load->getType()->isIntegerTy())
      return unsupportedOperation(I, "only scalar integer loads are supported");
    if (Load->isVolatile())
      return unsupportedOperation(I, "volatile loads are not represented");
    if (!hasSupportedPointerOrigin(*Load->getPointerOperand()))
      return unsupportedOperation(I, "only direct alloca or global pointers are supported");
    Result["result"] = getLocalName(I);
    Result["pointer"] = serializePointer(*Load->getPointerOperand());
    Result["type"] = serializeIntegerType(Load->getType());
    Result["ordering"] = orderingName(Load->getOrdering());
    return Result;
  }
  if (const auto *Store = dyn_cast<StoreInst>(&I)) {
    if (!Store->getValueOperand()->getType()->isIntegerTy())
      return unsupportedOperation(I, "only scalar integer stores are supported");
    if (Store->isVolatile())
      return unsupportedOperation(I, "volatile stores are not represented");
    if (!hasSupportedPointerOrigin(*Store->getPointerOperand()))
      return unsupportedOperation(I, "only direct alloca or global pointers are supported");
    Result["value"] = serializeValue(*Store->getValueOperand());
    Result["pointer"] = serializePointer(*Store->getPointerOperand());
    Result["type"] = serializeIntegerType(Store->getValueOperand()->getType());
    Result["ordering"] = orderingName(Store->getOrdering());
    return Result;
  }
  if (const auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(&I)) {
    if (!CmpXchg->getCompareOperand()->getType()->isIntegerTy())
      return unsupportedOperation(I, "cmpxchg requires scalar integer operands");
    if (CmpXchg->isVolatile())
      return unsupportedOperation(I, "volatile cmpxchg is not represented");
    if (!hasSupportedPointerOrigin(*CmpXchg->getPointerOperand()))
      return unsupportedOperation(I, "only direct alloca or global pointers are supported");
    Result["result"] = getLocalName(I);
    Result["pointer"] = serializePointer(*CmpXchg->getPointerOperand());
    Result["expected"] = serializeValue(*CmpXchg->getCompareOperand());
    Result["replacement"] = serializeValue(*CmpXchg->getNewValOperand());
    Result["type"] = serializeIntegerType(CmpXchg->getCompareOperand()->getType());
    Result["successOrdering"] = orderingName(CmpXchg->getSuccessOrdering());
    Result["failureOrdering"] = orderingName(CmpXchg->getFailureOrdering());
    Result["weak"] = CmpXchg->isWeak();
    return Result;
  }
  if (const auto *Extract = dyn_cast<ExtractValueInst>(&I)) {
    if (Extract->getNumIndices() != 1 ||
        !isa<AtomicCmpXchgInst>(Extract->getAggregateOperand()) ||
        !Extract->getType()->isIntegerTy() || *Extract->idx_begin() > 1)
      return unsupportedOperation(I, "only cmpxchg result fields 0 and 1 are supported");
    Result["result"] = getLocalName(I);
    Result["aggregate"] = getLocalName(*Extract->getAggregateOperand());
    Result["index"] = *Extract->idx_begin();
    Result["resultType"] = serializeIntegerType(Extract->getType());
    return Result;
  }
  if (const auto *Branch = dyn_cast<BranchInst>(&I)) {
    if (Branch->isConditional()) {
      Result["condition"] = serializeValue(*Branch->getCondition());
      Result["target"] = getLocalName(*Branch->getSuccessor(0));
      Result["falseTarget"] = getLocalName(*Branch->getSuccessor(1));
    } else {
      Result["condition"] = nullptr;
      Result["target"] = getLocalName(*Branch->getSuccessor(0));
      Result["falseTarget"] = nullptr;
    }
    return Result;
  }
  if (const auto *Phi = dyn_cast<PHINode>(&I)) {
    if (!Phi->getType()->isIntegerTy())
      return unsupportedOperation(I, "only scalar integer phi nodes are supported");
    json::Array Incoming;
    for (unsigned Index = 0; Index < Phi->getNumIncomingValues(); ++Index) {
      json::Object Entry;
      Entry["value"] = serializeValue(*Phi->getIncomingValue(Index));
      Entry["predecessor"] = getLocalName(*Phi->getIncomingBlock(Index));
      Incoming.push_back(std::move(Entry));
    }
    Result["result"] = getLocalName(I);
    Result["type"] = serializeIntegerType(Phi->getType());
    Result["incoming"] = std::move(Incoming);
    return Result;
  }
  return unsupportedOperation(I, "instruction is not in the supported set");
}

} // namespace

json::Object serializeFunction(const Function &F,
                               const depvec::DependencyGraph &Graph) {
  std::string FunctionIR;
  raw_string_ostream FunctionOS(FunctionIR);
  F.print(FunctionOS);
  FunctionOS.flush();
  if (!FunctionIR.empty() && FunctionIR.back() == '\n')
    FunctionIR.pop_back();
  const std::vector<std::string> IRLines = getFunctionIRLines(F);

  json::Array Nodes;
  unsigned SearchLine = 0;
  for (unsigned Id = 0; Id < Graph.nodes.size(); ++Id) {
    const auto &Node = Graph.nodes[Id];
    StringRef Kind = "instruction";
    int Line = -1;
    if (Node.kind == depvec::DependencyGraph::Node::Kind::Init) {
      Kind = "init";
      Line = 0;
    } else if (Node.kind == depvec::DependencyGraph::Node::Kind::Exit) {
      Kind = "exit";
    }

    std::string InstructionText;
    if (Node.instruction) {
      InstructionText = getInstructionText(*Node.instruction);
      Line = findInstructionLine(*Node.instruction, IRLines, SearchLine);
      if (Line >= 0)
        SearchLine = static_cast<unsigned>(Line) + 1;
    }

    json::Object SerializedNode;
    SerializedNode["id"] = Id;
    SerializedNode["kind"] = Kind;
    SerializedNode["line"] = Line;
    SerializedNode["instruction"] = std::move(InstructionText);
    if (Node.kind == depvec::DependencyGraph::Node::Kind::Instruction &&
        Node.instruction)
      SerializedNode["operation"] = serializeInstruction(*Node.instruction);
    Nodes.push_back(std::move(SerializedNode));
  }

  json::Array Edges;
  for (unsigned Index = 0; Index < Graph.edges.size(); ++Index) {
    const auto &Edge = Graph.edges[Index];
    json::Array Sources;
    for (unsigned Source : Edge.sources)
      Sources.push_back(Source);
    json::Array Values;
    for (const std::string &Value : Edge.values)
      Values.push_back(Value);
    json::Array Interferences;
    for (unsigned Interference : Edge.interferences)
      Interferences.push_back(Interference);

    json::Object SerializedEdge;
    SerializedEdge["id"] = (Twine("e") + Twine(Index)).str();
    SerializedEdge["sources"] = std::move(Sources);
    SerializedEdge["target"] = Edge.target;
    SerializedEdge["kind"] = Edge.kind;
    SerializedEdge["values"] = std::move(Values);
    SerializedEdge["assertion"] = "";
    SerializedEdge["interferences"] = std::move(Interferences);
    Edges.push_back(std::move(SerializedEdge));
  }

  json::Object Result;
  Result["name"] = F.getName();
  Result["ir"] = std::move(FunctionIR);
  Result["nodes"] = std::move(Nodes);
  Result["edges"] = std::move(Edges);
  return Result;
}

json::Object serializeGlobalInitialValues(const Module &M) {
  json::Object Values;
  for (const GlobalVariable &Global : M.globals()) {
    if (!Global.hasInitializer() || !Global.getValueType()->isIntegerTy())
      continue;
    const auto *Initializer = dyn_cast<ConstantInt>(Global.getInitializer());
    if (!Initializer)
      continue;

    SmallString<32> Text;
    if (Initializer->getType()->isIntegerTy(1))
      Text.append(Initializer->isZero() ? StringRef("false")
                                        : StringRef("true"));
    else
      Initializer->getValue().toString(Text, 10, true);
    Values[(Twine("@") + Global.getName()).str()] = Text.str().str();
  }
  return Values;
}

bool writeGraphJSON(const Module &M, std::vector<json::Object> Functions, StringRef Path) {
  std::error_code EC;
  raw_fd_ostream OS(Path, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << "DepVec: unable to write graph JSON to '" << Path
           << "': " << EC.message() << '\n';
    return false;
  }

  json::Array SerializedFunctions;
  for (json::Object &Function : Functions)
    SerializedFunctions.push_back(std::move(Function));
  json::Object Root;
  Root["format"] = "depvec-graph-v2";
  Root["precondition"] = "";
  Root["postcondition"] = "";
  Root["globalInitialValues"] = serializeGlobalInitialValues(M);
  Root["module"] = M.getModuleIdentifier();
  Root["functions"] = std::move(SerializedFunctions);
  OS << formatv("{0:2}\n", json::Value(std::move(Root)));
  OS.flush();
  if (OS.has_error()) {
    errs() << "DepVec: failed while writing graph JSON to '" << Path
           << "'\n";
    return false;
  }
  return true;
}


} // namespace depvec
