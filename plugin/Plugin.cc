#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

#include "SPIRVComputeSanitizer.h"

namespace llvm {

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SPIR-V Compute Sanitizer plugin", "v0.1",
          [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback([&](ModulePassManager &MPM,
                                                   OptimizationLevel) {
              FunctionPassManager FPM;
              FPM.addPass(SPIRVComputeSanitizerPass());
              MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
            });
          }};
}

} // namespace llvm
