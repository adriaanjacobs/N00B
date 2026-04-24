#include "llvm_noob.h"

#include <llvm/Passes/PassPlugin.h>
#include <llvm/Passes/PassBuilder.h>

// This part is the new way of registering your pass
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "NOOBPlugin", LLVM_VERSION_STRING,
        [](llvm::PassBuilder &PB) {
            PB.registerFullLinkTimeOptimizationLastEPCallback([](llvm::ModulePassManager &MPM, llvm::OptimizationLevel Level) {
                llvm::outs() << "Registering NOOB pass for LateLTO!\n";
                NOOBInstrumentationPass::addPasses(MPM);
            });
            PB.registerAnalysisRegistrationCallback(NOOBInstrumentationPass::registerModuleAnalyses);
            PB.registerAnalysisRegistrationCallback(NOOBInstrumentationPass::registerFunctionAnalyses);
        }
    };
}
