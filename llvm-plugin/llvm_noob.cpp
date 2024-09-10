#include "llvm_noob.h"

#include <llvm/IR/Instructions.h>

llvm::PreservedAnalyses NOOBInstrumentationPass::run(llvm::Module& module, llvm::ModuleAnalysisManager& passMAM) {

    // we are lazy and say everything is invalidated
    return llvm::PreservedAnalyses::none();
}

