#include "SPIRVComputeSanitizer.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>

#define DEBUG_TYPE "spirv-compute-sanitizer"

using namespace llvm;

static constexpr char get_global_id_name[] = "_Z13get_global_idj";
static constexpr char get_local_id_name[] = "_Z12get_local_idj";

static constexpr unsigned ConstantAddressSpace =
    2; // SPIR-V constant address space
static constexpr unsigned LocalAddressSpace = 3; // SPIR-V local address space

static bool is_spirv_triple(const Triple &T) {
  const StringRef Arch = T.getArchName();

  return Arch.starts_with("spirv");
}

static bool should_run(const Module &M) {
  if (M.getTargetTriple().empty())
    return false;

  const Triple T(M.getTargetTriple());

  return is_spirv_triple(T);
}

static FunctionCallee insert_fn(Module &M, const StringRef Name, Type *Result,
                                ArrayRef<Type *> Params,
                                bool IsVarArg = false) {
  const auto FuncTy = FunctionType::get(Result, Params, IsVarArg);
  auto FC = M.getOrInsertFunction(Name, FuncTy);
  auto F = cast<Function>(FC.getCallee());

  F->setCallingConv(CallingConv::SPIR_FUNC);
  F->setUnnamedAddr(GlobalValue::UnnamedAddr::Local);
  F->addFnAttr(Attribute::Convergent);

  for (auto &Param : F->args()) {
    F->addParamAttr(Param.getArgNo(), Attribute::NoUndef);
  }

  return FC;
}

struct SanitizerFunctionTemplate {
  StringRef Name;
  FunctionType *Type;
  bool IsVarArg;
};

static std::vector<SanitizerFunctionTemplate>
get_sanitizer_functions(LLVMContext &Ctx) {
  auto voidTy = Type::getVoidTy(Ctx);
  auto locali64PtrTy = PointerType::get(
      IntegerType::getInt64Ty(Ctx),
      LocalAddressSpace); // addrspace(3) pointer to unsigned long
  auto i32Ty = IntegerType::getInt32Ty(Ctx);
  auto i64Ty = IntegerType::getInt64Ty(Ctx);

  return {
      // Report functions
      {"libscsan_report_index_out_of_bounds",
       FunctionType::get(voidTy, {}, false), false},
      {"libscsan_report_local_memory_conflict",
       FunctionType::get(voidTy, {i64Ty}, false), false},

      // Shadow functions
      {"libscsan_shadow_memset",
       FunctionType::get(voidTy, {locali64PtrTy, i64Ty, i64Ty}, false), false},
  };
}

struct SanitizerFunction {
  StringRef Name;
  FunctionType *Type;
  bool IsVarArg;
  FunctionCallee Callee;

  SanitizerFunction(const SanitizerFunctionTemplate &temp,
                    const FunctionCallee callee)
      : Name(temp.Name), Type(temp.Type), IsVarArg(temp.IsVarArg),
        Callee(callee) {}
};

static std::vector<SanitizerFunction> sanitizer_functions{};

static void setup_extern_functions(Module &M) {
  // Ensure that the module has the required external functions
  auto &Context = M.getContext();

  for (const auto &Func : get_sanitizer_functions(Context)) {
    // Insert the function into the module
    auto F = insert_fn(M, Func.Name, Func.Type->getReturnType(),
                       Func.Type->params(), Func.IsVarArg);

    sanitizer_functions.push_back({Func, F});
  }
}

struct ShadowLocalMemLink {
  GlobalVariable *ShadowVar;
  GlobalVariable *OriginalVar;
};

struct ArraySizeLink {
  Argument *ArrayArg;
  Argument *SizeArg;
};

static CallInst *add_sanitizer_call(IRBuilder<> &Builder, const StringRef Name,
                                    ArrayRef<Value *> Args) {
  const auto Func =
      std::find_if(sanitizer_functions.begin(), sanitizer_functions.end(),
                   [&](const SanitizerFunction &F) { return F.Name == Name; });

  if (Func == sanitizer_functions.end()) {
    errs() << "Sanitizer function not found: " << Name << "\n";

    return nullptr;
  }

  auto *Call = Builder.CreateCall(Func->Callee, Args);
  Call->setCallingConv(CallingConv::SPIR_FUNC);

  return Call;
}

static CallInst *create_atomic_exchange_call(IRBuilder<> &Builder, Value *Ptr,
                                             Value *Val) {
  auto AtomicExchangeFC =
      Builder.GetInsertBlock()->getModule()->getOrInsertFunction(
          "_Z15atomic_exchangePU3AS4VU7_Atomicmm",
          FunctionType::get(
              IntegerType::getInt64Ty(Builder.getContext()),
              {PointerType::get(IntegerType::getInt64Ty(Builder.getContext()),
                                4),
               Val->getType()},
              false));
  auto CastPtr = Builder.CreateAddrSpaceCast(Ptr, PointerType::get(
      IntegerType::getInt64Ty(Builder.getContext()), 4));

  auto *AtomicExchangeFunc = cast<Function>(AtomicExchangeFC.getCallee());
  AtomicExchangeFunc->setCallingConv(CallingConv::SPIR_FUNC);

  return Builder.CreateCall(AtomicExchangeFC, {CastPtr, Val});
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
      GetLocalIdFunc,
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

static std::optional<std::pair<BasicBlock::iterator, GlobalVariable *>>
find_injectable_local_mem_store(
    std::vector<ShadowLocalMemLink> &ShadowLocalMemLinks,
    BasicBlock::iterator Inst, const StoreInst *Store) {
  // Check if the store instruction is storing to a pointer with
  // addrspace(3)
  if (getLoadStoreAddressSpace(Store) != LocalAddressSpace) {
    errs() << "Skipping store to non-local memory: " << *Store << "\n";

    return std::nullopt; // Skip stores to non-local memory
  }

  // Find shadow local memory variable
  auto StorePtrGEP = dyn_cast<GetElementPtrInst>(Store->getPointerOperand());

  if (!StorePtrGEP) {
    errs() << "Skipping store with non-GEP pointer operand: " << *Store << "\n";

    return std::nullopt; // Not a GEP, skip
  }

  auto MaybeShadowVar = std::find_if(
      ShadowLocalMemLinks.begin(), ShadowLocalMemLinks.end(),
      [&](const ShadowLocalMemLink &Link) {
        return Link.OriginalVar == StorePtrGEP->getPointerOperand();
      });

  if (MaybeShadowVar == ShadowLocalMemLinks.end()) {
    errs() << "Skipping store with unlinked shadow variable: " << *Store
           << "\n";

    return std::nullopt; // No linked shadow variable found, skip
  }

  return std::make_pair(Inst, MaybeShadowVar->ShadowVar);
}

static std::pair<BasicBlock *, BranchInst *>
inject_gep_check(Function &F, BasicBlock &Block, IRBuilder<> &Builder,
                 std::vector<ArraySizeLink> &ArraySizeLinks,
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

    return {}; // No size argument found
  }

  auto *SizeArg = LinkEntry->SizeArg;

  auto *ThenBlock = BasicBlock::Create(F.getContext(), "", &F);
  auto *ElseBlock = BasicBlock::Create(F.getContext(), "", &F);

  // Move all instructions after the last GEP instruction to the new block
  ThenBlock->splice(ThenBlock->begin(), &Block, GetElementPtr, Block.end());

  IRBuilder<> ElseBuilder(ElseBlock);

  add_sanitizer_call(ElseBuilder, "libscsan_report_index_out_of_bounds", {});

  ElseBuilder.CreateRetVoid();

  return {ThenBlock,
          Builder.CreateCondBr(Builder.CreateICmpULT(IndexOperand, SizeArg),
                               ThenBlock, ElseBlock)};
}

static std::pair<BasicBlock *, BranchInst *> inject_shadow_local_mem_check(
    Function &F, BasicBlock &Block, IRBuilder<> &Builder,
    std::pair<BasicBlock::iterator, GlobalVariable *> shadow_var_pair) {
  auto Store = shadow_var_pair.first;
  auto *ShadowBufVar = shadow_var_pair.second;
  auto ShadowVarTy = ShadowBufVar->getValueType()->getArrayElementType();
  auto GEPOperand =
      cast<GetElementPtrInst>(cast<StoreInst>(Store)->getPointerOperand());
  auto IndexOperand = GEPOperand->getOperand(GEPOperand->getNumOperands() -
                                             1); // Get last operand

  Builder.SetInsertPoint(Store);

  auto *ShadowPtr = Builder.CreateInBoundsGEP(
      ShadowBufVar->getValueType(), ShadowBufVar,
      {ConstantInt::get(Type::getInt64Ty(F.getContext()), 0), IndexOperand});

  auto BuilderCurrLid = Builder.CreateAdd(create_get_local_id_call(Builder, 0), ConstantInt::get(ShadowVarTy, 1));
  auto *ShadowVar = create_atomic_exchange_call(Builder, ShadowPtr, BuilderCurrLid);
  auto *ShadowVarArea = Builder.CreateAlloca(ShadowVarTy);

  Builder.CreateStore(ShadowVar, ShadowVarArea);

  auto ThenBlock = BasicBlock::Create(F.getContext(), "", &F);
  auto ElseBlock = BasicBlock::Create(F.getContext(), "", &F);

  auto EQCond = Builder.CreateICmpEQ(ShadowVar, BuilderCurrLid);
  auto FirstWriteCond = Builder.CreateICmpEQ(ShadowVar, ConstantInt::get(ShadowVarTy, 0));

  Builder.CreateCondBr(Builder.CreateOr(EQCond, FirstWriteCond), ThenBlock,
                       ElseBlock);

  IRBuilder<> ThenBuilder(ThenBlock);

  auto ThenBuilderCurrLid = ThenBuilder.CreateAdd(create_get_local_id_call(ThenBuilder, 0), ConstantInt::get(ShadowVarTy, 1));
  auto *ShadowVar2 = create_atomic_exchange_call(ThenBuilder, ShadowPtr, ThenBuilderCurrLid);

  ThenBuilder.CreateStore(ShadowVar2, ShadowVarArea);

  // Second check

  auto Then2Block = BasicBlock::Create(F.getContext(), "", &F);

  Then2Block->splice(Then2Block->begin(), &Block, Store->getIterator(),
                     Block.end());

  auto Branch2Inst = ThenBuilder.CreateCondBr(
      ThenBuilder.CreateICmpEQ(ShadowVar2, ThenBuilderCurrLid), Then2Block,
      ElseBlock);

  // In the else block, we have a conflict
  IRBuilder<> ElseBuilder(ElseBlock);

  add_sanitizer_call(ElseBuilder, "libscsan_report_local_memory_conflict",
                     {ElseBuilder.CreateSub(
                         ElseBuilder.CreateLoad(
                             ShadowVarArea->getAllocatedType(), ShadowVarArea),
                         ConstantInt::get(ShadowVarTy, 1))});

  ElseBuilder.CreateRetVoid();

  return {Then2Block, Branch2Inst};
}

struct TraverseContext {
  Function &F;
  std::vector<ShadowLocalMemLink> &ShadowLocalMemLinks;
  std::vector<ArraySizeLink> &ArraySizeLinks;
  std::vector<BasicBlock::const_iterator> skipInstructions{};

  TraverseContext(Function &F,
                  std::vector<ShadowLocalMemLink> &ShadowLocalMemLinks,
                  std::vector<ArraySizeLink> &ArraySizeLinks)
      : F(F), ShadowLocalMemLinks(ShadowLocalMemLinks),
        ArraySizeLinks(ArraySizeLinks) {}
};

static void traverse(BasicBlock &Block, TraverseContext &Ctx,
                     const int depth = 0) {
  std::optional<std::pair<BasicBlock::iterator, GlobalVariable *>>
      maybe_shadow_var_pair;
  std::optional<std::pair<BasicBlock::iterator, Argument *>> maybe_gep_pair;

  // Create a global string variable once to avoid string deduplication bug
  IRBuilder<> Builder(&Block);

  auto Inst = Block.begin();

  for (const auto E = Block.end(); Inst != E; ++Inst) {
    if (Ctx.skipInstructions.size() > 0 &&
        std::any_of(Ctx.skipInstructions.begin(), Ctx.skipInstructions.end(),
                    [&](BasicBlock::const_iterator i) {
                      return i == Inst->getIterator();
                    })) {
      errs() << "Skipping instruction already processed: " << *Inst << "\n";

      continue; // Skip instructions already processed
    }

    if (const auto *Br = dyn_cast<BranchInst>(Inst)) {
      // Follow the branch instruction to the next block
      if (Br->isConditional()) {
        traverse(*Br->getSuccessor(0), Ctx, depth + 1);
        traverse(*Br->getSuccessor(1), Ctx, depth + 1);
      } else {
        traverse(*Br->getSuccessor(0), Ctx, depth + 1);
      }
    }

    // ArrayIndexOutOfBounds: Intercept GEP
    if (const auto *GetElementPtr = dyn_cast<GetElementPtrInst>(Inst)) {
      maybe_gep_pair =
          find_injectable_gep(Ctx.ArraySizeLinks, Inst, GetElementPtr);

      if (maybe_gep_pair) {
        errs() << "Found injectable GEP instruction: " << *Inst << "\n";

        Ctx.skipInstructions.push_back(GetElementPtr->getIterator());

        break;
      }
    }

    // LocalMemoryConflict: Intercept store
    if (auto *Store = dyn_cast<StoreInst>(Inst)) {
      maybe_shadow_var_pair =
          find_injectable_local_mem_store(Ctx.ShadowLocalMemLinks, Inst, Store);

      if (maybe_shadow_var_pair) {
        errs() << "Found store to local memory: " << *Store << "\n";

        Ctx.skipInstructions.push_back(Store->getIterator());

        break;
      }
    }
  }

  if (maybe_shadow_var_pair) {
    auto [ThenBlock, BI] = inject_shadow_local_mem_check(
        Ctx.F, Block, Builder, maybe_shadow_var_pair.value());

    traverse(*ThenBlock, Ctx, depth + 1);
  }

  if (maybe_gep_pair) {
    auto [ThenBlock, BI] = inject_gep_check(
        Ctx.F, Block, Builder, Ctx.ArraySizeLinks, maybe_gep_pair.value());

    traverse(*ThenBlock, Ctx, depth + 1);
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
      if (Arg.getType()->getIntegerBitWidth() == 64) {
        if (FoundArraySizeLink) {
          ret.push_back({F.getArg(FoundArraySizeLink.value()), &Arg});

          FoundArraySizeLink.reset();
        }
      }
    }
  }

  return ret;
}

static std::vector<ShadowLocalMemLink>
find_shadow_local_mem_links(Function &F) {
  std::vector<ShadowLocalMemLink> ret;

  for (auto &Var : F.getParent()->globals()) {
    if (Var.getType()->getAddressSpace() != LocalAddressSpace) {
      errs() << "Skipping global variable without addrspace(3): " << Var
             << "\n";

      continue;
    }

    if (Var.isConstant()) {
      errs() << "Skipping constant global variable: " << Var << "\n";

      continue;
    }

    if (Var.isExternallyInitialized()) {
      errs() << "Skipping external global variable: " << Var << "\n";

      continue;
    }

    // Var is array?
    if (!Var.getValueType()->isArrayTy()) {
      errs() << "Skipping global variable that is not an array: " << Var
             << "\n";

      continue;
    }

    errs() << "Found local array buffer: " << Var << "\n";

    // Create a global variable to hold the shadow local memory
    auto VarName = Var.getName();
    auto ShadowVarName = VarName.empty() ? "" : VarName.str() + ".shadow";
    auto ShadowVarTy =
        ArrayType::get(Type::getInt64Ty(F.getContext()),
                       Var.getValueType()->getArrayNumElements());

    auto MaybeShadowVar =
        F.getParent()->getOrInsertGlobal(ShadowVarName, ShadowVarTy, [&]() {
          return new GlobalVariable(
              *F.getParent(), ShadowVarTy, false, GlobalValue::InternalLinkage,
              UndefValue::get(ShadowVarTy), ShadowVarName, &Var,
              GlobalValue::NotThreadLocal, LocalAddressSpace, false);
        });

    if (auto ShadowVar = dyn_cast<GlobalVariable>(MaybeShadowVar)) {
      ShadowVar->setAlignment(Align(8));

      ret.push_back({ShadowVar, &Var});
    } else {
      errs() << "Failed to create shadow variable for: " << Var << "\n";
    }
  }

  return ret;
}

static void
print_shadow_links(const std::vector<ShadowLocalMemLink> &ShadowLocalMemLinks) {
  if (ShadowLocalMemLinks.empty()) {
    errs() << "No shadow local memory links found.\n";

    return;
  }

  errs() << "Shadow local memory links found:\n";

  for (const auto &Link : ShadowLocalMemLinks) {
    errs() << "Shadow variable: " << *Link.ShadowVar
           << ", Original variable: " << *Link.OriginalVar << "\n";
  }
}

static void
print_array_links(const std::vector<ArraySizeLink> &ArraySizeLinks) {
  if (ArraySizeLinks.empty()) {
    errs() << "No array links found.\n";

    return;
  }

  errs() << "Array links found:\n";

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
  if (const Module *M = F.getParent(); !should_run(*M)) {
    errs() << "SPIRVComputeSanitizerPass: Not running on non-SPIR-V module\n";

    return PreservedAnalyses::all(); // Don't run if not SPIR-V
  }

  errs() << "SPIRVComputeSanitizerPass: Running on SPIR-V module\n";

  setup_extern_functions(*F.getParent());

  // LocalMemoryConflict: Allocate shadow local memory
  std::vector<ShadowLocalMemLink> ShadowLocalMemLinks =
      find_shadow_local_mem_links(F);

  // Add libscsan_shadow_memset(shadowVar, size, 0) call
  for (auto &link : ShadowLocalMemLinks) {
    IRBuilder<> Builder(F.getContext());
    Builder.SetInsertPoint(&F.getEntryBlock().front());

    add_sanitizer_call(
        Builder, "libscsan_shadow_memset",
        {Builder.CreatePointerCast(link.ShadowVar, link.ShadowVar->getType()),
         ConstantInt::get(
             Type::getInt64Ty(F.getContext()),
             link.ShadowVar->getValueType()->getArrayNumElements()),
         ConstantInt::get(Type::getInt64Ty(F.getContext()), 0)});
  }

  std::vector<ArraySizeLink> ArraySizeLinks = find_array_size_links(F);

  auto &Block = F.getEntryBlock();

  TraverseContext TraverseCtx{F, ShadowLocalMemLinks, ArraySizeLinks};

  traverse(Block, TraverseCtx);

  errs() << "\n";

  print_shadow_links(ShadowLocalMemLinks);
  print_array_links(ArraySizeLinks);

  return PreservedAnalyses::all();
}
