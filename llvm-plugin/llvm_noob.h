#pragma once

#include <corolla/instrpointoptimization/hoistloopmemaccesses.h>

#include <llvm/IR/PassManager.h>

#include <optional>
#include <map>

struct CheckInfo : public InstrumentationPoint {
    llvm::Value* trackedBase = pointerOperand;
    const bool checkDereference;
    const bool isEscapeSite;

    CheckInfo(llvm::Instruction* insertBefore, llvm::Value* pointerOperand, bool checkDereference, bool isEscapeSite) : 
        InstrumentationPoint(insertBefore, pointerOperand), checkDereference{checkDereference}, isEscapeSite{isEscapeSite}
    {}

    bool shouldCheckArith () const {
        return trackedBase != pointerOperand;
    }

    bool shouldCheckDereference() const {
        return checkDereference;
    }

    bool operator==(const CheckInfo& other) const {
        // doesn't take into account padding, but that's okay: our current use case does not compare
        //  objects at different locations, but the same object at different times. padding shouldnt be modified.
        return std::memcmp(this, &other, sizeof(*this)) == 0;
    }
};

//===----------------------------------------------------------------------===//
/// This class implements an LLVM module transformation pass.
class NOOBInstrumentationPass : public llvm::PassInfoMixin<NOOBInstrumentationPass> {
    std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>> findUnsafeGlobals(llvm::Module&, llvm::ModuleAnalysisManager& MAM);
    void extendNOOBLinkerScript(std::string& noobLinkerScript, const std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>>& radixToGlobals);

    llvm::DenseSet<llvm::Instruction*> findUnsafeAccesses(llvm::Module& module, llvm::ModuleAnalysisManager& MAM);

    llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>> createInstrumentationPlans(llvm::Module& module, llvm::ModuleAnalysisManager& MAM);
    void applyNOOBChecks(llvm::Module& module, llvm::ModuleAnalysisManager& MAM, const llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>>& checkInfoToUses);

    llvm::DenseMap<llvm::Function*, llvm::DenseSet<llvm::AllocaInst*>> findUnsafeAllocas(llvm::Module& module, llvm::ModuleAnalysisManager& MAM);
    void moveUnsafeAllocasToNOOBStacks(llvm::Module& module, const llvm::DenseMap<llvm::Function*, llvm::DenseSet<llvm::AllocaInst*>>& unsafeAllocas);

    void maskExternalPointerArguments(llvm::Module& module);

    struct ComputedBasePtrInfo {
        llvm::Value* radix;
        llvm::Value* origObj;
        llvm::Value* topTag;
    };
    llvm::Value* computeSafeInArithAreaPtr(llvm::Value* ptr, llvm::Value* arithAreaSize, llvm::Value* arithAreaBase, llvm::Value* trackedBase, llvm::Instruction* insertBefore);
    llvm::Value* shiftDownTillInPointerTag(llvm::Value* ptr, llvm::Value* radix, llvm::Instruction* insertBefore);
    llvm::Value* embedInPointerTagAsTopTag(llvm::Value* ptr, llvm::Value* radix, llvm::Instruction* insertBefore);
    llvm::Value* computeTopTag(llvm::Value* ptr, llvm::Value* radix, llvm::Instruction* insertBefore);
    llvm::Value* computePoisonMaskAtDerefSite(const CheckInfo& checkInfo, const ComputedBasePtrInfo& basePtrInfo, llvm::Instruction* insertBefore);

public:
    explicit NOOBInstrumentationPass() = default;
    ~NOOBInstrumentationPass() = default;

    // Transform the bitcode/IR in the given LLVM module.
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);

    static void registerModuleAnalyses(llvm::ModuleAnalysisManager&);
    static void registerFunctionAnalyses(llvm::FunctionAnalysisManager&);
    static void addPasses(llvm::ModulePassManager&);
};

struct RadixDecoder {
    llvm::Module& module;
    llvm::GlobalVariable* radixTable = nullptr;

    RadixDecoder(llvm::Module& module);
    llvm::Value* computeRadix(llvm::Value* ptrAsInt, llvm::Instruction* insertBefore);
};
