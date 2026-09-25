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
#include <string>

using namespace llvm;

namespace {

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

class DepVecPass : public PassInfoMixin<DepVecPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << "DepVec: module " << M.getName() << '\n';
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    for (Function &F : M) {
      if (F.isDeclaration() || !hasDepVecAnnotation(F))
        continue;

      errs() << "dependency graph for function " << F.getName() << '\n';
      auto &Graph = FAM.getResult<depvec::DependencyGraphAnalysis>(F);

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
