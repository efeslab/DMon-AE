#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CodeMetrics.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/InitializePasses.h"

#define DEBUG_TYPE "loop-data-prefetch"

using namespace llvm;

static cl::opt<bool>
PrefetchWrites("pref-writes", cl::Hidden, cl::init(false),
               cl::desc("Prefetch write addresses"));

static cl::opt<unsigned>
    PrefetchDistance("pref-distance",
                     cl::desc("Number of instructions to prefetch ahead"),
                     cl::Hidden);

static cl::opt<unsigned>
    MinPrefetchStride("min-pref-stride",
                      cl::desc("Min stride to add prefetches"), cl::Hidden);

static cl::opt<unsigned> MaxPrefetchIterationsAhead(
    "max-pref-iters-ahead",
    cl::desc("Max number of iterations to prefetch ahead"), cl::Hidden);

namespace llvm {

class LoopDataPrefetchPass : public PassInfoMixin<LoopDataPrefetchPass> {
public:
  LoopDataPrefetchPass() = default;
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

namespace {

class LoopDataPrefetch {
public:
  LoopDataPrefetch(AssumptionCache *AC, LoopInfo *LI, ScalarEvolution *SE,
                   const TargetTransformInfo *TTI,
                   OptimizationRemarkEmitter *ORE)
      : AC(AC), LI(LI), SE(SE), TTI(TTI), ORE(ORE) {
      }

  bool run();

private:
  bool runOnLoop(Loop *L);

  bool isStrideLargeEnough(const SCEVAddRecExpr *AR);

  unsigned getMinPrefetchStride() {
    if (MinPrefetchStride.getNumOccurrences() > 0)
      return MinPrefetchStride;
    return TTI->getMinPrefetchStride();
  }

  unsigned getPrefetchDistance() {
    if (PrefetchDistance.getNumOccurrences() > 0)
      return PrefetchDistance;
    return TTI->getPrefetchDistance();
  }

  unsigned getMaxPrefetchIterationsAhead() {
    if (MaxPrefetchIterationsAhead.getNumOccurrences() > 0)
      return MaxPrefetchIterationsAhead;
    return TTI->getMaxPrefetchIterationsAhead();
  }

  AssumptionCache *AC;
  LoopInfo *LI;
  ScalarEvolution *SE;
  const TargetTransformInfo *TTI;
  OptimizationRemarkEmitter *ORE;
};

class LoopDataPrefetchLegacyPass : public FunctionPass {
public:
  static char ID;
  LoopDataPrefetchLegacyPass() : FunctionPass(ID) {
    initializeLoopDataPrefetchLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<OptimizationRemarkEmitterWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addPreserved<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

  bool runOnFunction(Function &F) override;
  };
}

char LoopDataPrefetchLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(LoopDataPrefetchLegacyPass, "test-prefetch",
                      "Loop Data Prefetch", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(OptimizationRemarkEmitterWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(LoopDataPrefetchLegacyPass, "test-prefetch",
                    "Loop Data Prefetch", false, false)

FunctionPass *llvm::createLoopDataPrefetchPass() {
  return new LoopDataPrefetchLegacyPass();
}

bool LoopDataPrefetch::isStrideLargeEnough(const SCEVAddRecExpr *AR) {
  unsigned TargetMinStride = getMinPrefetchStride();
  if (TargetMinStride <= 1)
    return true;

  const auto *ConstStride = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*SE));
  if (!ConstStride)
    return false;

  unsigned AbsStride = std::abs(ConstStride->getAPInt().getSExtValue());
  return TargetMinStride <= AbsStride;
}

PreservedAnalyses LoopDataPrefetchPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  LoopInfo *LI = &AM.getResult<LoopAnalysis>(F);
  ScalarEvolution *SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
  AssumptionCache *AC = &AM.getResult<AssumptionAnalysis>(F);
  OptimizationRemarkEmitter *ORE =
      &AM.getResult<OptimizationRemarkEmitterAnalysis>(F);
  const TargetTransformInfo *TTI = &AM.getResult<TargetIRAnalysis>(F);

  LoopDataPrefetch LDP(AC, LI, SE, TTI, ORE);
  bool Changed = LDP.run();

  if (Changed) {
    PreservedAnalyses PA;
    PA.preserve<DominatorTreeAnalysis>();
    PA.preserve<LoopAnalysis>();
    return PA;
  }

  return PreservedAnalyses::all();
}

bool LoopDataPrefetchLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  LoopInfo *LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  ScalarEvolution *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  AssumptionCache *AC =
      &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
  OptimizationRemarkEmitter *ORE =
      &getAnalysis<OptimizationRemarkEmitterWrapperPass>().getORE();
  const TargetTransformInfo *TTI =
      &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);

  LoopDataPrefetch LDP(AC, LI, SE, TTI, ORE);
  return LDP.run();
}

bool LoopDataPrefetch::run() {
  if (getPrefetchDistance() == 0)
    return false;
  assert(TTI->getCacheLineSize() && "Cache line size is not set for target");

  bool MadeChange = false;

  for (Loop *I : *LI)
    for (auto L = df_begin(I), LE = df_end(I); L != LE; ++L)
      MadeChange |= runOnLoop(*L);

  return MadeChange;
}

bool LoopDataPrefetch::runOnLoop(Loop *L) {
  bool MadeChange = false;

  if (!L->empty())
    return MadeChange;

  SmallPtrSet<const Value *, 32> EphValues;
  CodeMetrics::collectEphemeralValues(L, AC, EphValues);

  CodeMetrics Metrics;
  for (const auto BB : L->blocks()) {
    for (auto &I : *BB)
      if (CallInst *CI = dyn_cast<CallInst>(&I))
        if (Function *F = CI->getCalledFunction())
          if (F->getIntrinsicID() == Intrinsic::prefetch)
            return MadeChange;

    Metrics.analyzeBasicBlock(BB, *TTI, EphValues);
  }
  unsigned LoopSize = Metrics.NumInsts;
  if (!LoopSize)
    LoopSize = 1;

  unsigned ItersAhead = getPrefetchDistance() / LoopSize;
  if (!ItersAhead)
    ItersAhead = 1;

  if (ItersAhead > getMaxPrefetchIterationsAhead())
    return MadeChange;

  SmallVector<std::pair<Instruction *, const SCEVAddRecExpr *>, 16> PrefLoads;
  for (const auto BB : L->blocks()) {
    for (auto &I : *BB) {
      Value *PtrValue;
      Instruction *MemI;

      if (LoadInst *LMemI = dyn_cast<LoadInst>(&I)) {
        MemI = LMemI;
        PtrValue = LMemI->getPointerOperand();
      } else if (StoreInst *SMemI = dyn_cast<StoreInst>(&I)) {
        if (!PrefetchWrites) continue;
        MemI = SMemI;
        PtrValue = SMemI->getPointerOperand();
      } else continue;

      unsigned PtrAddrSpace = PtrValue->getType()->getPointerAddressSpace();
      if (PtrAddrSpace)
        continue;

      if (L->isLoopInvariant(PtrValue))
        continue;

      const SCEV *LSCEV = SE->getSCEV(PtrValue);
      const SCEVAddRecExpr *LSCEVAddRec = dyn_cast<SCEVAddRecExpr>(LSCEV);
      if (!LSCEVAddRec)
        continue;

      if (!isStrideLargeEnough(LSCEVAddRec))
        continue;

      bool DupPref = false;
      for (const auto &PrefLoad : PrefLoads) {
        const SCEV *PtrDiff = SE->getMinusSCEV(LSCEVAddRec, PrefLoad.second);
        if (const SCEVConstant *ConstPtrDiff =
            dyn_cast<SCEVConstant>(PtrDiff)) {
          int64_t PD = std::abs(ConstPtrDiff->getValue()->getSExtValue());
          if (PD < (int64_t) TTI->getCacheLineSize()) {
            DupPref = true;
            break;
          }
        }
      }
      if (DupPref)
        continue;

      const SCEV *NextLSCEV = SE->getAddExpr(LSCEVAddRec, SE->getMulExpr(
        SE->getConstant(LSCEVAddRec->getType(), ItersAhead),
        LSCEVAddRec->getStepRecurrence(*SE)));
      if (!isSafeToExpand(NextLSCEV, *SE))
        continue;

      PrefLoads.push_back(std::make_pair(MemI, LSCEVAddRec));

      Type *I8Ptr = Type::getInt8PtrTy(BB->getContext(), PtrAddrSpace);
      SCEVExpander SCEVE(*SE, I.getModule()->getDataLayout(), "prefaddr");
      Value *PrefPtrValue = SCEVE.expandCodeFor(NextLSCEV, I8Ptr, MemI);

      IRBuilder<> Builder(MemI);
      Module *M = BB->getParent()->getParent();
      Type *I32 = Type::getInt32Ty(BB->getContext());
      Value *PrefetchFunc = Intrinsic::getDeclaration(M, Intrinsic::prefetch);
      Builder.CreateCall(
          PrefetchFunc,
          {PrefPtrValue,
           ConstantInt::get(I32, MemI->mayReadFromMemory() ? 0 : 1),
           ConstantInt::get(I32, 3), ConstantInt::get(I32, 1)});
      ORE->emit([&]() {
        return OptimizationRemark(DEBUG_TYPE, "Prefetched", MemI)
               << "prefetched memory access";
      });

      MadeChange = true;
    }
  }

  return MadeChange;
}