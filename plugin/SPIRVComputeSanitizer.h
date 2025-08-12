#ifndef LLVM_SPIRVCOMPUTESANITIZER_H
#define LLVM_SPIRVCOMPUTESANITIZER_H

#include <llvm/IR/PassManager.h>

namespace llvm {

class SPIRVComputeSanitizerPass
    : public PassInfoMixin<SPIRVComputeSanitizerPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_SPIRVCOMPUTESANITIZER_H
