#include "SPIRVComputeSanitizer.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

#define DEBUG_TYPE "spirv-compute-sanitizer"

using namespace llvm;

static constexpr unsigned ConstantAddressSpace = 2; // SPIR-V constant address space

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

struct ArraySizeLink {
  Argument *ArrayArg;
  Argument *SizeArg;
};

static void traverse(Function &F, std::vector<ArraySizeLink> &ArraySizeLinks,
                     BasicBlock &Block, const int depth = 0) {
  std::optional<BasicBlock::iterator> maybe_gep;

  auto Inst = Block.begin();

  if (depth > 0)
    ++Inst; // Skip first getelementptr

  for (const auto E = Block.end(); Inst != E; ++Inst) {
    if (const auto *GetElementPtr = dyn_cast<GetElementPtrInst>(Inst)) {
      // Find the array argument in the GEP instruction
      if (GetElementPtr->getNumOperands() != 2) {
        errs() << "Skipping GEP with unexpected number of operands: "
               << GetElementPtr->getName() << "\n";

        continue;
      }

      auto *PtrOperand_ = GetElementPtr->getOperand(0);
      auto *PtrOperand = dyn_cast<Argument>(PtrOperand_);

      if (!PtrOperand || !PtrOperand->getType()->isPointerTy()) {
        errs() << "Skipping GEP with non-pointer array argument: "
               << *PtrOperand_ << "\n";

        continue; // Not a pointer type, skip
      }

      // Find the size argument in the GEP instruction
      auto *IndexOperand_ = GetElementPtr->getOperand(1);
      const auto *IndexOperand = dyn_cast<Value>(IndexOperand_);

      if (!IndexOperand || !IndexOperand->getType()->isIntegerTy()) {
        errs() << "Skipping GEP with non-integer size argument: "
               << *IndexOperand_ << "\n";

        continue; // Not an integer type, skip
      }

      // Are these arguments linked?
      const auto is_linked = std::any_of(
          ArraySizeLinks.begin(), ArraySizeLinks.end(),
          [&](const ArraySizeLink &Link) {
            return Link.ArrayArg->getArgNo() == PtrOperand->getArgNo();
          });

      if (!is_linked) {
        errs() << "Found GEP with unlinked array and size arguments: "
               << *PtrOperand << ", " << *IndexOperand << "\n";

        continue;
      }

      maybe_gep = Inst;

      errs() << "Found instruction: " << *Inst << "\n";

      break;
    }
  }

  if (!maybe_gep) {
    errs() << "No GEP instructions found in the block.\n";

    return; // No GEP instructions found
  }

  const auto GetElementPtr = maybe_gep.value();
  const auto PtrOperand = dyn_cast<Argument>(GetElementPtr->getOperand(0));
  const auto IndexOperand = GetElementPtr->getOperand(1);
  const auto LinkEntry = std::find_if(
      ArraySizeLinks.begin(), ArraySizeLinks.end(),
      [&](const ArraySizeLink &Link) {
        return Link.ArrayArg->getArgNo() == PtrOperand->getArgNo();
      });

  if (LinkEntry == ArraySizeLinks.end()) {
    errs() << "No size argument found for the array argument: "
           << *PtrOperand << "\n";

    return; // No size argument found
  }

  auto *SizeArg = LinkEntry->SizeArg;

  IRBuilder<> Builder(&Block);

  auto *ThenBlock = BasicBlock::Create(F.getContext(), "", &F);
  auto *ElseBlock = BasicBlock::Create(F.getContext(), "", &F);

  // Move all instructions after the last GEP instruction to the new block
  ThenBlock->splice(ThenBlock->begin(), &Block, GetElementPtr, Block.end());

  auto *SizeCheck =
      Builder.CreateICmpULT(IndexOperand, SizeArg);

  IRBuilder<> ElseBuilder(ElseBlock);

  // Add printf("Array size mismatch") call to report_block
  const auto PrintfFunc = F.getParent()->getOrInsertFunction(
      "printf",
      FunctionType::get(IntegerType::getInt32Ty(F.getContext()),
                        PointerType::get(Type::getInt8Ty(F.getContext()), ConstantAddressSpace),
                        true));
  auto *FormatStr = ElseBuilder.CreateGlobalString("\n[ComputeSanitizer] Array index out of bounds\n", "", ConstantAddressSpace);

  ElseBuilder.CreateCall(PrintfFunc, {FormatStr});
  ElseBuilder.CreateRetVoid();

  Builder.CreateCondBr(SizeCheck, ThenBlock, ElseBlock);

  traverse(F, ArraySizeLinks, *ThenBlock, depth + 1);
}

PreservedAnalyses SPIRVComputeSanitizerPass::run(Function &F,
                                                 FunctionAnalysisManager &FAM) {
  if (const Module *M = F.getParent(); !shouldRun(*M)) {
    errs() << "SPIRVComputeSanitizerPass: Not running on non-SPIR-V module\n";

    return PreservedAnalyses::all(); // Don't run if not SPIR-V
  }

  errs() << "SPIRVComputeSanitizerPass: Running on SPIR-V module\n";

  // Link Args Identifier (assume second pair as 0)
  std::vector<ArraySizeLink> ArraySizeLinks;

  std::optional<size_t> FoundArraySizeLink;
  for (auto &Arg : F.args()) {
    if (Arg.getType()->isPointerTy()) {
      if (FoundArraySizeLink) {
        FoundArraySizeLink.reset();
      }

      FoundArraySizeLink = Arg.getArgNo();
    }

    if (Arg.getType()->isIntegerTy()) {
      if (Arg.getType()->getIntegerBitWidth() % 32 == 0) {
        if (FoundArraySizeLink) {
          ArraySizeLinks.push_back(
              {F.getArg(FoundArraySizeLink.value()), &Arg});

          FoundArraySizeLink.reset();
        }
      }
    }
  }

  auto &Block = F.getEntryBlock();

  traverse(F, ArraySizeLinks, Block);

  errs() << "\nLinks found:\n";

  for (const auto &[ArrayArg, SizeArg] : ArraySizeLinks) {
    if (ArrayArg->getType()->isPointerTy()) {
      errs() << "Array argument: " << *ArrayArg
             << ", Size argument: " << *SizeArg << "\n";
    } else {
      errs() << "Invalid link found: " << *ArrayArg << "\n";
    }
  }

  return PreservedAnalyses::all();
}
