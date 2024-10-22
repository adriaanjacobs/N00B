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

    // we only really have to do something special for phis and selects
    if (auto phi = llvm::dyn_cast<llvm::PHINode>(base)) {
        auto trackerPhi = llvm::PHINode::Create(
            phi->getType(), 
            phi->getNumIncomingValues(), 
            llvm::formatv("basetracker.{0}", phi->getName()), 
            phi->getNextNode()
        );
        // update the cache already so any circular references already use the correct pre-existing phitracker 
        //  (that is still under construction)
        cachedTrackers[phi] = trackerPhi;

        // i think this one should fail at some point no?
        ASSERT_ELSE_UNKOWN(llvm::pred_size(phi->getParent()) == phi->getNumIncomingValues(), phi); 
        ASSERT_ELSE_UNKOWN(phi->getNumIncomingValues() == phi->getNumIncomingValues(), phi);

        // phis contain multiple duplicate entries for different edges from the same predecessor
        // if we naively find/insert their trackers and cast them to the correct value, we will
        //  inadvertently create a trackerphi that has different bitcast instructions of the same
        //  tracker for these duplicate entries
        // hence, we first deduplicate the phi incoming edges, insert the tracker once for each,
        //  then duplicate the trackerphi entries as necessary
        llvm::DenseMap<llvm::BasicBlock*, llvm::Value*> predToCastedTracker;
        for (auto pred : llvm::predecessors(phi->getParent())) {
            if (!predToCastedTracker.count(pred)) {
                auto trackerValue = trackBasePtr(phi->getIncomingValueForBlock(pred));
                 // cast to correct type. insert at end of predecessor to ensure it's defined for this phi
                 //     the ifNecessary here avoids stupid issues with invokes where the types are never going to differ
                 //     but unconditionally inserting a cast instruction makes it hard to choose where to do it
                predToCastedTracker[pred] = createBitOrPointerCastIfNecessary(trackerValue, phi->getType(), "", pred->getTerminator());
            }
            auto castedTracker = predToCastedTracker[pred];
            trackerPhi->addIncoming(castedTracker, pred);
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
    } else {
        // base case: we arrived at the baseptr (typically a load, call, extract*, argument, or something unanalyzable)
        return base;
    }
}
