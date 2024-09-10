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
