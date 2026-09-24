#include "InstructionGraphAnalysis.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class DepVecPass : public PassInfoMixin<DepVecPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    errs() << "DepVec: module " << M.getName() << '\n';
    FunctionAnalysisManager &FAM =
        MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      errs() << "function " << F.getName() << '\n';
      auto &Graph = FAM.getResult<depvec::InstructionGraphAnalysis>(F);

      for (unsigned Id = 0; Id < Graph.nodes.size(); ++Id)
        errs() << "  n" << Id << ": " << *Graph.nodes[Id] << '\n';

      errs() << "  dependency edges:\n";
      for (const auto &Edge : Graph.dependencyEdges) {
        errs() << "    {";
        for (unsigned I = 0; I < Edge.sources.size(); ++I) {
          if (I)
            errs() << ", ";
          errs() << "n" << Edge.sources[I];
        }
        errs() << "} -[";
        switch (Edge.flavour) {
        case depvec::InstructionGraphAnalysis::DependencyEdgeFlavour::Data:
          errs() << "Data";
          break;
        }
        errs() << "]-> n" << Edge.target;
        if (Edge.flavour ==
            depvec::InstructionGraphAnalysis::DependencyEdgeFlavour::Data)
          errs() << " (" << Edge.value << ')';
        errs() << '\n';
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
                  [] { return depvec::InstructionGraphAnalysis(); });
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
