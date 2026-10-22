#include "DependencyGraphAnalysis.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ErrorHandling.h"
#include <system_error>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

namespace {

cl::opt<std::string> DepVecOutput(
    "depvec-output",
    cl::desc("Write DepVec graphs as JSON to the given path"),
    cl::value_desc("path"), cl::init(""));

bool hasDepVecAnnotation(const Function &F) {
  const Module *M = F.getParent();
  const GlobalVariable *Annotations =
      M->getGlobalVariable("llvm.global.annotations");
  if (!Annotations || !Annotations->hasInitializer())
    return false;

  const auto *Entries =
      dyn_cast<ConstantArray>(Annotations->getInitializer());
  if (!Entries)
    return false;

  for (const Use &EntryUse : Entries->operands()) {
    const auto *Entry = dyn_cast<ConstantStruct>(EntryUse.get());
    if (!Entry || Entry->getNumOperands() < 2)
      continue;

    const Value *AnnotatedValue =
        Entry->getOperand(0)->stripPointerCasts();
    if (AnnotatedValue != &F)
      continue;

    const auto *AnnotationGlobal = dyn_cast<GlobalVariable>(
        Entry->getOperand(1)->stripPointerCasts());
    if (!AnnotationGlobal || !AnnotationGlobal->hasInitializer())
      continue;

    const auto *Annotation =
        dyn_cast<ConstantDataArray>(AnnotationGlobal->getInitializer());
    if (Annotation && Annotation->isString() &&
        Annotation->getAsCString() == "depvec")
      return true;
  }
  return false;
}

std::string getFunctionHeader(const Function &F) {
  std::string IR;
  raw_string_ostream OS(IR);
  F.print(OS);
  OS.flush();

  // Function::print emits the body-opening brace on its own line after the
  // signature. Aggregate types in the signature have inline braces, so this
  // delimiter identifies the start of the body.
  size_t BodyStart = IR.find("{\n");
  if (BodyStart == std::string::npos)
    BodyStart = IR.find("{\r\n");
  if (BodyStart == std::string::npos)
    return F.getName().str();

  return IR.substr(0, BodyStart + 1);
}

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

json::Object serializeFunction(const Function &F,
                               const depvec::DependencyGraph &Graph) {
  std::string FunctionIR;
  raw_string_ostream FunctionOS(FunctionIR);
  F.print(FunctionOS);
  FunctionOS.flush();
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

    json::Object SerializedEdge;
    SerializedEdge["id"] = (Twine("e") + Twine(Index)).str();
    SerializedEdge["sources"] = std::move(Sources);
    SerializedEdge["target"] = Edge.target;
    SerializedEdge["kind"] = Edge.kind;
    SerializedEdge["values"] = std::move(Values);
    Edges.push_back(std::move(SerializedEdge));
  }

  json::Object Result;
  Result["name"] = F.getName();
  Result["ir"] = std::move(FunctionIR);
  Result["nodes"] = std::move(Nodes);
  Result["edges"] = std::move(Edges);
  return Result;
}

bool writeGraphJSON(const Module &M, std::vector<json::Object> Functions) {
  std::error_code EC;
  raw_fd_ostream OS(DepVecOutput, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << "DepVec: unable to write graph JSON to '" << DepVecOutput
           << "': " << EC.message() << '\n';
    return false;
  }

  json::Array SerializedFunctions;
  for (json::Object &Function : Functions)
    SerializedFunctions.push_back(std::move(Function));
  json::Object Root;
  Root["format"] = "depvec-graph-v1";
  Root["module"] = M.getModuleIdentifier();
  Root["functions"] = std::move(SerializedFunctions);
  OS << formatv("{0:2}\n", json::Value(std::move(Root)));
  OS.flush();
  if (OS.has_error()) {
    errs() << "DepVec: failed while writing graph JSON to '" << DepVecOutput
           << "'\n";
    return false;
  }
  return true;
}

class DepVecPass : public PassInfoMixin<DepVecPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << "DepVec: module " << M.getName() << '\n';
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    std::vector<json::Object> SerializedFunctions;
    for (Function &F : M) {
      if (F.isDeclaration() || !hasDepVecAnnotation(F))
        continue;

      errs() << "dependency graph for function " << F.getName() << '\n';
      auto &Graph = FAM.getResult<depvec::DependencyGraphAnalysis>(F);
      SerializedFunctions.push_back(serializeFunction(F, Graph));

      for (unsigned Id = 0; Id < Graph.nodes.size(); ++Id) {
        const auto &Node = Graph.nodes[Id];
        errs() << "  n" << Id << ": ";
        switch (Node.kind) {
        case depvec::DependencyGraph::Node::Kind::Init:
          errs() << "init: " << getFunctionHeader(F);
          break;
        case depvec::DependencyGraph::Node::Kind::Instruction:
          errs() << *Node.instruction;
          break;
        case depvec::DependencyGraph::Node::Kind::Exit:
          errs() << "exit (" << *Node.instruction << ')';
          break;
        }
        errs() << '\n';
      }

      errs() << "  dependency edges:\n";
      for (const auto &Edge : Graph.edges) {
        errs() << "    {";
        bool FirstSource = true;
        for (unsigned Source : Edge.sources) {
          errs() << (FirstSource ? " " : ", ") << "n" << Source;
          FirstSource = false;
        }
        if (!FirstSource)
          errs() << " ";
        errs() << "} -" << Edge.kind;
        if (!Edge.values.empty()) {
          errs() << "(";
          bool FirstValue = true;
          for (const std::string &Value : Edge.values) {
            errs() << (FirstValue ? "" : ", ") << Value;
            FirstValue = false;
          }
          errs() << ")";
        }
        errs() << "-> n" << Edge.target << '\n';
      }
    }

    if (!DepVecOutput.empty() &&
        !writeGraphJSON(M, std::move(SerializedFunctions)))
      report_fatal_error("DepVec could not write the graph JSON output");

    // This pass only observes the module and does not modify the IR.
    return PreservedAnalyses::all();
  }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DepVecPass", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerAnalysisRegistrationCallback(
            [](FunctionAnalysisManager &FAM) {
              FAM.registerPass(
                  [] { return depvec::DependencyGraphAnalysis(); });
            });

            // Permit explicit use with: opt -passes=depvec
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name != "depvec")
                    return false;
                  MPM.addPass(DepVecPass());
                  return true;
                });

            // Also run at the end of the default optimization pipeline.
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel,
                   ThinOrFullLTOPhase) { MPM.addPass(DepVecPass()); });
          }};
}
