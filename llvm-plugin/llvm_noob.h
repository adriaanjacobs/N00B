#pragma once

#include <llvm/IR/PassManager.h>

//===----------------------------------------------------------------------===//
/// This class implements an LLVM module transformation pass.
class NOOBInstrumentationPass : public llvm::PassInfoMixin<NOOBInstrumentationPass> {
public:
    explicit NOOBInstrumentationPass() = default;
    ~NOOBInstrumentationPass() = default;

    // Transform the bitcode/IR in the given LLVM module.
    llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);
};

struct BasePtrTracker {
    llvm::Module& module;
    llvm::ModuleAnalysisManager& MAM;
    llvm::DenseMap<llvm::Value*, llvm::Value*> cachedTrackers;

    BasePtrTracker(llvm::Module& module, llvm::ModuleAnalysisManager& MAM);

    // propagates intraprocedural base pointers (arguments, loads, calls, ...) through merges (select, phi) 
    // and returns a variable representing the value of the base pointer when `ptr` is live
    llvm::Value* trackBasePtr(llvm::Value* ptr);
};
