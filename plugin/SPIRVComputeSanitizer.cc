#include "SPIRVComputeSanitizer.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Debug.h>
#include <llvm/TargetParser/Triple.h>

#define DEBUG_TYPE "spirv-compute-sanitizer"

using namespace llvm;

static bool isSPIRVTriple(const Triple &T) {
  const StringRef Arch = T.getArchName();

  return Arch.starts_with("spirv");
}

static bool shouldRun(const Module &M) {
  if (M.getTargetTriple().empty())
    return false;

  const Triple T(M.getTargetTriple());

  return isSPIRVTriple(T);
}

PreservedAnalyses SPIRVComputeSanitizerPass::run(Function &F, FunctionAnalysisManager &FAM) {
  const Module *M = F.getParent();

  if (!shouldRun(*M)) {
    errs() << "SPIRVComputeSanitizerPass: Not running on non-SPIR-V module\n";

    return PreservedAnalyses::all(); // Don't run if not SPIR-V
  }

  errs() << "SPIRVComputeSanitizerPass: Running on SPIR-V module\n";

  return PreservedAnalyses::all();
}
