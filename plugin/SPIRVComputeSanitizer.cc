#include "SPIRVComputeSanitizer.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

#define DEBUG_TYPE "spirv-compute-sanitizer"

using namespace std::literals;
using namespace llvm;

static constexpr char get_global_id_name[] = "_Z13get_global_idj";
static constexpr char get_local_id_name[] = "_Z12get_local_idj";

static constexpr unsigned ConstantAddressSpace =
    2; // SPIR-V constant address space

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

struct SanitizerMessageCtx {
  bool Created = false;
  GlobalVariable *FormatIndexOutOfBounds = nullptr;
  GlobalVariable *FormatLocalMemoryConflict = nullptr;

  void createFormatStrings(IRBuilder<> &Builder) {
    if (Created)
      return;

    FormatIndexOutOfBounds =
        Builder.CreateGlobalString("\n[ComputeSanitizer] (Global #%zu, Local "
                                   "#%zu) Array index out of bounds\n",
                                   "", ConstantAddressSpace);
    FormatLocalMemoryConflict =
        Builder.CreateGlobalString("\n[ComputeSanitizer] (Global #%zu, Local "
                                   "#%zu) Local memory conflict detected\n",
                                   "", ConstantAddressSpace);

    Created = true;
  }
};

static CallInst *create_printf_call(IRBuilder<> &Builder,
                                    ArrayRef<Value *> Args) {
  auto PrintfFunc = Builder.GetInsertBlock()->getModule()->getOrInsertFunction(
      "printf",
      FunctionType::get(IntegerType::getInt32Ty(Builder.getContext()),
                        PointerType::get(Type::getInt8Ty(Builder.getContext()),
                                         ConstantAddressSpace),
                        true));

  auto PrintfCI = Builder.CreateCall(PrintfFunc, Args);
  PrintfCI->setCallingConv(CallingConv::SPIR_FUNC);

  return PrintfCI;
}

static CallInst *create_get_global_id_call(IRBuilder<> &Builder, unsigned dim) {
  auto GetGlobalIdFC =
      Builder.GetInsertBlock()->getModule()->getOrInsertFunction(
          get_global_id_name,
          FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()),
                            {IntegerType::getInt32Ty(Builder.getContext())},
                            false));
  auto *GetGlobalIdFunc = cast<Function>(GetGlobalIdFC.getCallee());
  GetGlobalIdFunc->setCallingConv(CallingConv::SPIR_FUNC);

  return Builder.CreateCall(
      GetGlobalIdFC,
      {ConstantInt::get(IntegerType::getInt32Ty(Builder.getContext()), dim)});
}

static CallInst *create_get_local_id_call(IRBuilder<> &Builder, unsigned dim) {
  auto GetLocalIdFC =
      Builder.GetInsertBlock()->getModule()->getOrInsertFunction(
          get_local_id_name,
          FunctionType::get(IntegerType::getInt64Ty(Builder.getContext()),
                            {IntegerType::getInt32Ty(Builder.getContext())},
                            false));
  auto *GetLocalIdFunc = cast<Function>(GetLocalIdFC.getCallee());
  GetLocalIdFunc->setCallingConv(CallingConv::SPIR_FUNC);

  return Builder.CreateCall(
      GetLocalIdFC,
      {ConstantInt::get(IntegerType::getInt32Ty(Builder.getContext()), dim)});
}

static std::optional<std::pair<BasicBlock::iterator, Argument *>>
find_injectable_gep(std::vector<ArraySizeLink> &ArraySizeLinks,
                    BasicBlock::iterator Inst,
                    const GetElementPtrInst *GetElementPtr) {
  // Find the array argument in the GEP instruction
  if (GetElementPtr->getNumOperands() != 2) {
    errs() << "Skipping GEP with unexpected number of operands: "
           << *GetElementPtr << "\n";

    return std::nullopt;
  }

  auto *PtrOperand_ = GetElementPtr->getOperand(0);
  auto *PtrOperand = dyn_cast<Argument>(PtrOperand_);

  if (!PtrOperand) {
    // If the pointer operand is not an argument, check if it's a load
    auto PtrLoad = dyn_cast<LoadInst>(PtrOperand_);

    if (!PtrLoad) {
      errs() << "Skipping GEP with non-argument pointer operand: "
             << *PtrOperand_ << "\n";

      return std::nullopt; // Not an argument, skip
    }

    PtrOperand = dyn_cast<Argument>(PtrLoad->getPointerOperand());

    if (!PtrOperand) {
      // If the pointer load is not an argument, check if it's an alloca
      auto PtrAlloca = dyn_cast<AllocaInst>(PtrLoad->getPointerOperand());

      if (!PtrAlloca) {
        errs() << "Skipping GEP with non-argument pointer load: "
               << *PtrOperand_ << "\n";

        return std::nullopt; // Not an argument, skip
      }

      // Find the first store instruction that stores to the alloca
      auto MaybeStore = std::find_if(
          PtrAlloca->user_begin(), PtrAlloca->user_end(), [=](const User *U) {
            return isa<StoreInst>(U) &&
                   cast<StoreInst>(U)->getPointerOperand() == PtrAlloca;
          });

      if (MaybeStore == PtrAlloca->user_end()) {
        errs() << "Skipping GEP with alloca that has no store: " << *PtrAlloca
               << "\n";

        return std::nullopt; // No store found, skip
      }

      PtrOperand =
          dyn_cast<Argument>(cast<StoreInst>(*MaybeStore)->getOperand(0));

      if (!PtrOperand) {
        errs() << "Skipping GEP with alloca that has non-argument store: "
               << *PtrOperand_ << "\n";

        return std::nullopt; // Not an argument, skip
      }
    }
  } else if (!PtrOperand->getType()->isPointerTy()) {
    errs() << "Skipping GEP with non-pointer array argument: " << *PtrOperand_
           << "\n";

    return std::nullopt; // Not a pointer type, skip
  }

  // Find the size argument in the GEP instruction
  auto *IndexOperand_ = GetElementPtr->getOperand(1);
  const auto *IndexOperand = dyn_cast<Value>(IndexOperand_);

  if (!IndexOperand || !IndexOperand->getType()->isIntegerTy()) {
    errs() << "Skipping GEP with non-integer size argument: " << *IndexOperand_
           << "\n";

    return std::nullopt; // Not an integer type, skip
  }

  // Are these arguments linked?
  const auto is_linked =
      std::any_of(ArraySizeLinks.begin(), ArraySizeLinks.end(),
                  [&](const ArraySizeLink &Link) {
                    return Link.ArrayArg->getArgNo() == PtrOperand->getArgNo();
                  });

  if (!is_linked) {
    errs() << "Found GEP with unlinked array and size arguments: "
           << *PtrOperand << ", " << *IndexOperand << "\n";

    return std::nullopt;
  }

  return std::make_pair(Inst, PtrOperand);
}

static BasicBlock *
inject_gep_check(Function &F, BasicBlock &Block, IRBuilder<> &Builder,
                 std::vector<ArraySizeLink> &ArraySizeLinks,
                 SanitizerMessageCtx MessageCtx,
                 const std::pair<BasicBlock::iterator, Argument *> &gep_pair) {
  auto GetElementPtr = gep_pair.first;
  auto *PtrOperand = gep_pair.second;

  const auto IndexOperand = GetElementPtr->getOperand(1);
  const auto LinkEntry =
      std::find_if(ArraySizeLinks.begin(), ArraySizeLinks.end(),
                   [&](const ArraySizeLink &Link) {
                     return Link.ArrayArg->getArgNo() == PtrOperand->getArgNo();
                   });

  if (LinkEntry == ArraySizeLinks.end()) {
    errs() << "No size argument found for the array argument: " << *PtrOperand
           << "\n";

    return nullptr; // No size argument found
  }

  auto *SizeArg = LinkEntry->SizeArg;

  auto *ThenBlock = BasicBlock::Create(F.getContext(), "", &F);
  auto *ElseBlock = BasicBlock::Create(F.getContext(), "", &F);

  // Move all instructions after the last GEP instruction to the new block
  ThenBlock->splice(ThenBlock->begin(), &Block, GetElementPtr, Block.end());

  auto *SizeCheck = Builder.CreateICmpULT(IndexOperand, SizeArg);

  IRBuilder<> ElseBuilder(ElseBlock);

  // get_global_id(0)
  auto GlobalIdCall = create_get_global_id_call(ElseBuilder, 0);

  // get_local_id(0)
  auto LocalIdCall = create_get_local_id_call(ElseBuilder, 0);

  create_printf_call(ElseBuilder, {MessageCtx.FormatIndexOutOfBounds,
                                   GlobalIdCall, LocalIdCall});

  ElseBuilder.CreateRetVoid();

  Builder.CreateCondBr(SizeCheck, ThenBlock, ElseBlock);

  return ThenBlock;
}

static void traverse(Function &F, std::vector<ArraySizeLink> &ArraySizeLinks,
                     BasicBlock &Block, const int depth = 0,
                     SanitizerMessageCtx MessageCtx = {}) {
  std::optional<std::pair<BasicBlock::iterator, Argument *>> maybe_gep_pair;

  // Create a global string variable once to avoid string deduplication bug
  IRBuilder<> Builder(&Block);
  MessageCtx.createFormatStrings(Builder);

  auto Inst = Block.begin();

  if (depth > 0)
    ++Inst; // Skip first getelementptr

  for (const auto E = Block.end(); Inst != E; ++Inst) {
    if (const auto *GetElementPtr = dyn_cast<GetElementPtrInst>(Inst)) {
      maybe_gep_pair = find_injectable_gep(ArraySizeLinks, Inst, GetElementPtr);

      if (maybe_gep_pair) {
        errs() << "Found instruction: " << *Inst << "\n";

        break;
      }
    }
  }

  if (maybe_gep_pair) {
    auto ThenBlock = inject_gep_check(F, Block, Builder, ArraySizeLinks,
                                      MessageCtx, maybe_gep_pair.value());

    traverse(F, ArraySizeLinks, *ThenBlock, depth + 1, MessageCtx);
  }
}

static std::vector<ArraySizeLink> find_array_size_links(Function &F) {
  std::vector<ArraySizeLink> ret;

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
          ret.push_back(
              {F.getArg(FoundArraySizeLink.value()), &Arg});

          FoundArraySizeLink.reset();
        }
      }
    }
  }

  return ret;
}

static void print_links(const std::vector<ArraySizeLink> &ArraySizeLinks) {
  errs() << "Links found:\n";

  for (const auto &[ArrayArg, SizeArg] : ArraySizeLinks) {
    if (ArrayArg->getType()->isPointerTy()) {
      errs() << "Array argument: " << *ArrayArg
             << ", Size argument: " << *SizeArg << "\n";
    } else {
      errs() << "Invalid link found: " << *ArrayArg << "\n";
    }
  }
}

PreservedAnalyses SPIRVComputeSanitizerPass::run(Function &F,
                                                 FunctionAnalysisManager &FAM) {
  if (const Module *M = F.getParent(); !shouldRun(*M)) {
    errs() << "SPIRVComputeSanitizerPass: Not running on non-SPIR-V module\n";

    return PreservedAnalyses::all(); // Don't run if not SPIR-V
  }

  errs() << "SPIRVComputeSanitizerPass: Running on SPIR-V module\n";

  std::vector<ArraySizeLink> ArraySizeLinks = find_array_size_links(F);

  auto &Block = F.getEntryBlock();

  traverse(F, ArraySizeLinks, Block);

  errs() << "\n";
  print_links(ArraySizeLinks);

  return PreservedAnalyses::all();
}
