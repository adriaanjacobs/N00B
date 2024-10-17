#include "llvm_noob.h"

#include <llvm-utils/pointerdetection/pointerdetection.h>
#include <llvm-utils/util.h>

#include <llvm/Support/FormatVariadic.h>

BasePtrTracker::BasePtrTracker(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) : 
    module{module}, MAM{MAM}
{}

llvm::Value* BasePtrTracker::trackBasePtr(llvm::Value* ptr) {
    auto& pointerInfo = MAM.getResult<PointerDetectionAnalysis>(module);
    auto base = pointerInfo.find_real_base(ptr);

    if (auto it = cachedTrackers.find(base); it != cachedTrackers.end())
        return it->getSecond();

    // base case: we arrived at the baseptr, end the analysis
    // FIXME: with my alloca & global wrapping, i think there might be others/we might want to end earlier
    //  maybe i can introduce metadata to see whether it's a noob-introduced pointer?
    if (llvm::isa<llvm::Argument, llvm::AllocaInst, llvm::CallBase, llvm::Constant, llvm::LoadInst, llvm::ExtractValueInst>(base)) {
        return base;
    } else if (auto phi = llvm::dyn_cast<llvm::PHINode>(base)) {
        auto trackerPhi = llvm::PHINode::Create(
            phi->getType(), 
            phi->getNumIncomingValues(), 
            llvm::formatv("basetracker.{0}", phi->getName()), 
            phi->getNextNode()
        );
        // update the cache already so any circular references already use the correct pre-existing phitracker 
        //  (that is still under construction)
        cachedTrackers[phi] = trackerPhi;

        for (auto predecessor : llvm::predecessors(phi->getParent())) {
            auto value = phi->getIncomingValueForBlock(predecessor);
            auto trackerValue = trackBasePtr(value);
            // cast to correct type. insert at end of predecessor to ensure it's defined for this phi
            trackerValue = llvm::CastInst::CreateBitOrPointerCast(trackerValue, phi->getType(), "", predecessor->getTerminator());
            trackerPhi->addIncoming(trackerValue, predecessor);
        }

        return trackerPhi;
    } else if (auto select = llvm::dyn_cast<llvm::SelectInst>(base)) {
        auto trackerSelect = llvm::SelectInst::Create(
            select->getCondition(), 
            select->getTrueValue(), // placeholder true and false values to construct the phi
            select->getFalseValue(), 
            llvm::formatv("basetracker.{0}", select->getName()),
            select->getNextNode()
        );
        // same caching story as phi, even though i dont think selects can be self-referential (?)
        cachedTrackers[select] = trackerSelect;

        auto trackerIfTrue = trackBasePtr(select->getTrueValue());
        trackerIfTrue = llvm::CastInst::CreateBitOrPointerCast(trackerIfTrue, select->getType(), "", trackerSelect);
        trackerSelect->setTrueValue(trackerIfTrue);

        auto trackerIfFalse = trackBasePtr(select->getFalseValue());
        trackerIfFalse = llvm::CastInst::CreateBitOrPointerCast(trackerIfFalse, select->getType(), "", trackerSelect);
        trackerSelect->setFalseValue(trackerIfFalse);

        return trackerSelect;
    } else HANDLE_UNKOWN_VALUE(base);
}
