#include "llvm_noob.h"

#include <NOOB/config.h>
#include <NOOB/memlayout.h>

#include <corolla/util.h>
#include <corolla/safetyanalysis/safetyanalysis.h>
#include <corolla/pointerdetection/pointerdetection.h>
#include <corolla/addressability/addressability.h>
#include <corolla/instrpointoptimization/hoistloopmemaccesses.h>

#include <debugir/DebugIR.h>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/Analysis/StackSafetyAnalysis.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Transforms/Utils/InstructionNamer.h>

#include <sstream>

// find all the globals we want to wrap, compute their radix, and set the minimum alignment
// std::map so we get it nice and ordered
std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>> NOOBInstrumentationPass::findUnsafeGlobals(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
#if INTRAPROCEDURAL_ONLY
    auto unsafeAccesses = findUnsafeAccesses(module, MAM);
#else
    auto& unsafeAccesses = MAM.getResult<UnsafeAccessFinderAnalysis>(module).getOrCreate(false).unsafeAccesses;
#endif

    auto& callSiteAnalysis = MAM.getResult<CallSiteAnalysis>(module);
    std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>> radixToGlobals;
    size_t numSafeGlobals = 0;
    for (auto& global : module.globals()) {
        // globals that cannot flow into unsafeaccesses should be left alone
        //  this helps deal with opaque/llvm-inserted globals that have special section info etc.
        //  e.g. @llvm.used
        if (!ptrMayReachUnsafeAccesses(&global, unsafeAccesses, false, callSiteAnalysis)) {
            numSafeGlobals++;
            continue;
        }

        // i dont know what to do with globals that already have a section
        ASSERT_ELSE_UNKOWN(global.getSection() == "", &global);
        // according to the lowfat guys, the linker will ignore our section attribute for symbols with common linkage
        ASSERT_ELSE_UNKOWN(!global.hasCommonLinkage(), &global);
        unsigned long alignTo = module.getDataLayout().getTypeAllocSize(global.getValueType()).getFixedSize();
        // enforce a minimum alignment for globals that have no alignment, a too small alignment, a too small size, or a combination
        constexpr size_t noob_min_size = std::max(1UL << NOOB_MIN_RADIX, 8UL); // minimum 8 regardless
        alignTo = std::max(alignTo, global.getAlign().hasValue() ? global.getAlign()->value() : noob_min_size);
        if (alignTo < noob_min_size)
            alignTo = noob_min_size;
        auto radix = std::bit_width(alignTo - 1);
        // fucking sjeng has a 2MB global for fucks sake
        // that's a 1GB arena :(((((
        ASSERT_ELSE_UNKOWN(radix < 25, &global); // idk why we'd need globals larger than this
        global.setAlignment(llvm::Align{(1ULL << radix)});
        radixToGlobals[radix].push_back(&global);
    }

    // print it out the global stats
    llvm::outs() << "We proved " << numSafeGlobals << "/" << module.global_size() << " globals safe. The rest: \n";
    for (auto& [radix, globals] : radixToGlobals) {
        llvm::outs() << "\tRadix " << radix << ": " << globals.size() << " globals (size: " << (1ULL << radix) * globals.size() << "B)\n";
    }

    return radixToGlobals;
}

// extend the linker script to allocate the appropriate number of sections
// The plan is to generate a custom linker script, save it to `noob_linker_script.ld`, and then have the user specify that filename
//  on the command line as the -T parameter during linking. 
void NOOBInstrumentationPass::extendNOOBLinkerScript(std::string& noobLinkerScript, const std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>>& radixToGlobals) {
    llvm::SmallVector<std::string> segmentNames;
    std::stringstream sections;
    sections << "\nSECTIONS {\n";
    for (auto& [radix, globals] : radixToGlobals) {
        assert(radix < 25); // idk why we'd need globals larger than this
        assert(std::popcount(single_arena_size(radix)) == 1);
        auto needed_size =  (1ULL << radix) * globals.size();
        auto num_occupied_arenas = __builtin_align_up(needed_size, single_arena_size(radix)) / single_arena_size(radix);
        // always start and end with a reserved region
        auto needed_arenas = 1 + num_occupied_arenas * 2;
        // layout is: reserved | repeat [occupied | reserved]
        auto base_address = size_region_base(radix);
        for (uint i = 0; i < needed_arenas; i++) {
            std::string suffix = "RESERVED";
            bool isReserved = i % 2 == 0; // odd index means occupied
            if (!isReserved) 
                suffix = "OCCUPIED";
            base_address += single_arena_size(radix);
            if (!isReserved)
                assert(base_address % arith_area_size(radix) == 0); // optimized arithchecks expect this
            assert(base_address % 0x1000UL == 0); // should be page-aligned. otherwise just align it up I guess
            auto segmentName = llvm::formatv("noob_globals_radix{0}_{1}_{2}", radix, i, suffix).str();
            segmentNames.push_back(segmentName);
            sections << llvm::formatv("  . = SEGMENT_START(\"{0}\", {1:x});\n", segmentName, base_address).str();
            sections << llvm::formatv("  .{0} : { KEEP(*(.{0})) } : {0}\n", segmentName).str();
        }

        sections << "\n";

        for (uint i = 0; i < globals.size(); i++) {
            auto arena_idx = i / (1U << TAG_WIDTH);
            auto memory_idx = 1 + arena_idx*2; // always odd

            auto global = globals[i];
            global->setSection(llvm::formatv(".noob_globals_radix{0}_{1}_{2}", radix, memory_idx, "OCCUPIED").str());
        }
    }
    sections << "}\n";

    // we have to specify all the segments in the PHDRS as well otherwise lld complains
    sections << "\nPHDRS {\n";
    for (auto& segmentName : segmentNames) {
        std::string permissions = "0x0"; // "None"
        if (segmentName.ends_with("OCCUPIED"))
            permissions = "0x6"; // "PF_W + PF_R"
        sections << llvm::formatv("  {0} PT_LOAD FLAGS({1});\n", segmentName, permissions).str();
    }
    sections << "}\n";

    noobLinkerScript.append(sections.str());
}

llvm::DenseSet<llvm::Instruction*> NOOBInstrumentationPass::findUnsafeAccesses(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
    llvm::DenseSet<llvm::Instruction*> unsafeAccesses;
    
    for (auto& func : module) {
        if (func.isDeclaration())
            continue;

        auto OSOV = getFAM(module, MAM).getResult<OSOVAnalysis>(func);

        for (auto& bb : func) {
            for (auto& inst : bb) {
                if (auto ptrUse = getLoadStorePointerOperandUse(&inst)) {
                    if (!OSOV.isInBounds(ptrUse)) {
                        unsafeAccesses.insert(&inst);
                    }
                }
            }
        }
    }

    return unsafeAccesses;
}

bool belongsToUnsafeModule(llvm::Function* function) {
    auto selectModule = getenv("NOOB_SELECT");
    if (!selectModule)
        return true;
    if (auto subProgram = function->getSubprogram()) {
        auto filename = subProgram->getFilename();        
        if (filename.contains(selectModule)) {
            return true;
        }
    }

    return false;
}

// we analyze & optimize the placement of NOOB bounds checks here
//  there are 2 types of NOOB bounds checks: 
//      1. dereference checks: verify that the tag in the top pointer bits matches the tag embedded in the pointer address
//      2. arithmetic checks: verify that pointer arithmetic does not modify any of the more significant bits, 
//          so that in-pointer tags cannot repeat, radix info/top tag is not corrupted, etc.
llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>> NOOBInstrumentationPass::createInstrumentationPlans(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
#if INTRAPROCEDURAL_ONLY
    auto unsafeAccesses = findUnsafeAccesses(module, MAM);
#else
    auto& unsafeAccesses = MAM.getResult<UnsafeAccessFinderAnalysis>(module).getOrCreate(false).unsafeAccesses;
    MAM.getCachedResult<IsInBoundsAnalysis>(module)->printBailStats();
    auto pointerInfo = &MAM.getResult<PointerDetectionAnalysis>(module);
#endif

    // we're going to optimize the placement of checks in two waves: once for arithmetic checks & once for dereference checks
    //  this is because the hoisting code assumes that all checks can cancel each other out
    llvm::DenseMap<llvm::Function*, llvm::DenseMap<llvm::Use*, InstrumentationPoint*>> funcToCheckPoints;
    // keep track of all created checkinfos to ensure that the loop hoisting code isnt creating any new ones
    //  those would not have the correct checkinfo subtype and later downcasts would be unsafe
    llvm::DenseSet<InstrumentationPoint*> dbg_checkInfos;

    // poor man's dyn_cast
    auto safeDownCast = [&] (InstrumentationPoint* point) -> CheckInfo* {
        assert(dbg_checkInfos.contains(point));
        return reinterpret_cast<CheckInfo*>(point);
    };

    for (auto& access : unsafeAccesses) {
        if (!belongsToUnsafeModule(access->getFunction()))
            continue;

        auto ptr = llvm::getLoadStorePointerOperand(access);
        ASSERT_ELSE_UNKOWN(ptr, access);
        if (auto inst = llvm::dyn_cast<llvm::Instruction>(ptr))
            inst->setName("unsafeptr");
        auto ptrOperandIdx = [] (llvm::Instruction* access) {
            if (auto load = llvm::dyn_cast<llvm::LoadInst>(access))
                return load->getPointerOperandIndex();
            else if (auto store = llvm::dyn_cast<llvm::StoreInst>(access))
                return store->getPointerOperandIndex();
            else HANDLE_UNKOWN_VALUE(access);
        } (access);
        auto& ptrUse = access->getOperandUse(ptrOperandIdx);
#if CHECK_DEREFERENCE_SITES
        // this use shouldnt exist yet
        ASSERT_ELSE_UNKOWN(!funcToCheckPoints[access->getFunction()].count(&ptrUse), access);
        auto newCheckInfo = new CheckInfo(access, ptrUse.get(), CHECK_POINTER_DEREFERENCES && !EMULATE_LOWFAT, false); // lowfat only has a single type of check, we use "arith" for it
        funcToCheckPoints[access->getFunction()][&ptrUse] = newCheckInfo;
        dbg_checkInfos.insert(newCheckInfo);
#endif
    }

    if (auto selectedModule = getenv("NOOB_SELECT"))
        llvm::errs() << "NOOB_SELECT=" << selectedModule << " resulted in " << dbg_checkInfos.size() << "/" << unsafeAccesses.size() << " (" << 100.0f*dbg_checkInfos.size()/(float)unsafeAccesses.size() << "%) unsafe memaccess instrumented\n";

    LoopHoister loopHoister{module, MAM};
    loopHoister.hoistLoopBoundMemAccesses(funcToCheckPoints, !EMIT_RUNTIME_CALLS && !EMULATE_LOWFAT && !COUNT_NOOB_FAILURES
#if not INTRAPROCEDURAL_ONLY
        , pointerInfo
#endif
    );
    // lowfat and runtime debug cannot handle non-postdom
    // neither can our masked ptr counting

    // keep track of the checkinfos that describe dereferences
    //  this is because they may be deleted (not modified, we check this later) by the next round of optimizations
    //  this is because the next round of optimizations includes new instrumentation points that may dominate
    //  or otherwise make redundant existing arithmetic checks. the dereference checks should be unaffected
    // also use this loop to update the trackedBase for any pointerOperand that may have been modified by loopOpts
    //  this can happen for range checks, especially confusing for single-iteration range checks!
    llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>> checkInfoToUses;
    for (auto& [func, instpoints] : funcToCheckPoints)
        for (auto& [memaccessUse, point] : instpoints) {
            auto checkInfo = safeDownCast(point);
            checkInfo->trackedBase = checkInfo->pointerOperand;
            checkInfoToUses[checkInfo].insert(memaccessUse);
        }

    // add escape sites too, only for unsafe pointers

#if SOUND_POINTER_DETECTION
    auto& isInBoundsAnalysis = MAM.getResult<IsInBoundsAnalysis>(module);
    for (auto& pointer : pointerInfo->pointers) {
        ASSERT_ELSE_UNKOWN(pointerInfo->is_confirmed_pointer(pointer), pointer);
#else
#if not INTRAPROCEDURAL_ONLY
    auto& isInBoundsAnalysis = MAM.getResult<IsInBoundsAnalysis>(module);    
#endif
    for (auto& func : module)
        for (auto& inst : llvm::instructions(func))
            if (auto pointer = llvm::dyn_cast<llvm::GetElementPtrInst>(&inst)) {
#endif
        // do not wrap escaping constant pointers (obviously) or pointers that are in bounds
        if (llvm::isa<llvm::Constant>(pointer))
            continue;

#if INTRAPROCEDURAL_ONLY
        auto& isInBoundsAnalysis = getFAM(module, MAM).getResult<OSOVAnalysis>(func);
#endif

        llvm::DenseSet<llvm::Use*> escapeSites;
        collectIntraProceduralPtrEscapes(pointer, escapeSites
#if not INTRAPROCEDURAL_ONLY
            , pointerInfo
#endif
        );
        for (auto* use : escapeSites) {
            // if the escaping pointer is in bounds, nothing to do
            if (isInBoundsAnalysis.isInBounds(use))
                continue;
            
            auto user = use->getUser();
            // the user is always an instruction here!! (otherwise we're doing an unsafe but constant escape??)
            auto userInst = llvm::dyn_cast<llvm::Instruction>(user);
            ASSERT_ELSE_UNKOWN(userInst, user);
            if (!belongsToUnsafeModule(userInst->getFunction()))
                continue;
#if CHECK_ESCAPE_SITES
            // this use shouldnt exist yet
            ASSERT_ELSE_UNKOWN(!funcToCheckPoints[userInst->getFunction()].count(use), user);
            auto newCheckInfo = new CheckInfo(userInst, use->get(), false, true);
            funcToCheckPoints[userInst->getFunction()][use] = newCheckInfo;
            dbg_checkInfos.insert(newCheckInfo);
#endif
        }
    }

    // sanitize: copy all existing checkinfos for a later sanity check
    llvm::DenseMap<CheckInfo*, CheckInfo> dbg_checkInfoToCopy;
    for (auto& [checkInfo, _] : checkInfoToUses) {
        bool dbg = dbg_checkInfoToCopy.try_emplace(checkInfo, *checkInfo).second;
        assert(dbg);
    }

    if (dbg_checkInfoToCopy.empty() && getenv("NOOB_SELECT")) {
        llvm::errs().changeColor(llvm::raw_ostream::RED, true);
        llvm::errs() << "\n\n********************************************************************************\n";
        llvm::errs() << "WARNING: 'NOOB_SELECT' specified ('" << getenv("NOOB_SELECT") << "') but no matching modules found!\n";
        llvm::errs() << "********************************************************************************\n\n";
        llvm::errs().resetColor();
    }

    // then optimize the list _again_. the loophoister will share expanded SCEVs for both i think
    //  the advantage is that this will eliminate dominated arithmetic checks and such
    loopHoister.hoistLoopBoundMemAccesses(funcToCheckPoints, false
#if not INTRAPROCEDURAL_ONLY
        , pointerInfo
#endif
    ); // non-wrapping escape instrumentation is always branching

    { // sanitize: check that this second hoisting step did not modify any of the points that also describe dereferences
        // otherwise, the introduction of new points is changing the location of existing points? this may happen in the future
        for (auto& [checkInfo, uses] : checkInfoToUses) {
            auto& dbgCopy = dbg_checkInfoToCopy.find(checkInfo)->getSecond();
            assert(dbgCopy == *checkInfo);
        }
    }

    // now parse the results of the 2nd round of loop hoisting 
    //  simultaneously insert the baseptrtrackers: this is a great moment since we know for sure all these
    //  points must receive an arithmetic check
    //      does that mean all their uses must receive one as well? yes: all dereference sites must in principle
    //      receive ptrarith checks, except if they are dominated e.g. by a previous escape site
    //      in that case, the point will be dominated, and it will not be present here
    //      in other words: the set of uses of an instpoint can only grow in the second step, not shrink
    //      and if it grows and it's still present, all uses must receive arithchecks
    // we borrow cuCatch/CGuard's trick to propagate base pointers from source through selects/phis
    //  this is implemented in "BasePtrTracker" and modifies the module
    BasePtrTracker basePtrTracker {
        [&] (llvm::Value* ptr) -> BasePtrInfo {
#if INTRAPROCEDURAL_ONLY
            return PointerDetector::find_simple_base_pointer(ptr);
#else
            return pointerInfo->find_real_base(ptr);
#endif
        }
    };
    for (auto& [func, instpoints] : funcToCheckPoints) {
        for (auto& [memaccessUse, point] : instpoints) {
            auto checkInfo = safeDownCast(point);

            // set the trackedBase. if no pointer arithmetic, it defaults to pointeroperand -> no shouldCheckArith()
#if CHECK_POINTER_ARITHMETIC
            auto [trackedBase, isOffsetOpt] = basePtrTracker.trackBasePtr(checkInfo->pointerOperand);
            ASSERT_ELSE_UNKOWN(isOffsetOpt.has_value(), checkInfo->pointerOperand);
            if (isOffsetOpt.value() == true) 
                ASSERT_ELSE_UNKOWN(checkInfo->pointerOperand != trackedBase, memaccessUse->getUser());
#else
            // important to set because loopopt might have modified the pointerOperand
            auto trackedBase = checkInfo->pointerOperand; 
#endif
            checkInfo->trackedBase = trackedBase;
            
            // add the escape sites to the uses of this instpoint
            checkInfoToUses[checkInfo].insert(memaccessUse);
        }
    }
    return checkInfoToUses;
}

llvm::Value* NOOBInstrumentationPass::computeSafeInArithAreaPtr(llvm::Value* ptr, llvm::Value* arithAreaSize, llvm::Value* arithAreaBase, llvm::Value* trackedBase, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    auto int8PtrTy = llvm::Type::getInt8PtrTy(insertBefore->getContext());
    auto int8Ty = llvm::Type::getInt8Ty(insertBefore->getContext());
    auto ptrAsInt = createBitOrPointerCastIfNecessary(ptr, int64Ty, "", insertBefore);
    auto inArithAreaOffset = llvm::BinaryOperator::CreateAnd(arithAreaSize, ptrAsInt, "", insertBefore);
    auto safePtrAsInt = llvm::BinaryOperator::CreateAdd(arithAreaBase, inArithAreaOffset, "", insertBefore);
    auto baseAsInt = createBitOrPointerCastIfNecessary(trackedBase, int64Ty, "", insertBefore);
    auto diffWithBase = llvm::BinaryOperator::CreateSub(safePtrAsInt, baseAsInt, "", insertBefore);
    auto baseAsInt8Ptr = createBitOrPointerCastIfNecessary(trackedBase, int8PtrTy, "", insertBefore);
    llvm::Value* provenancedSafePtr = llvm::GetElementPtrInst::Create(int8Ty, baseAsInt8Ptr, {diffWithBase}, "", insertBefore);
    provenancedSafePtr = createBitOrPointerCastIfNecessary(provenancedSafePtr, ptr->getType(), "", insertBefore);
    return provenancedSafePtr;
}

llvm::Value* NOOBInstrumentationPass::shiftDownTillInPointerTag(llvm::Value* ptr, llvm::Value* radix, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    auto ptrAsInt = createBitOrPointerCastIfNecessary(ptr, int64Ty, "", insertBefore);
    auto ipTag = llvm::BinaryOperator::CreateLShr(ptrAsInt, radix, "iptag", insertBefore);
    return llvm::BinaryOperator::CreateAnd(ipTag, llvm::Constant::getIntegerValue(int64Ty, llvm::APInt{64, (1UL << TAG_WIDTH) - 1}), "iptag", insertBefore);
}

llvm::Value* NOOBInstrumentationPass::embedInPointerTagAsTopTag(llvm::Value* ptr, llvm::Value* radix, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    auto ptrAsInt = castToInt64Ty(ptr, insertBefore);
    auto inPointerTag = shiftDownTillInPointerTag(ptrAsInt, radix, insertBefore);
    auto inPointerTagMask = llvm::BinaryOperator::CreateShl(inPointerTag, llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, NOOB_TOPTAG_START}), "", insertBefore);
    // assuming that offsetStackPtr's top tag is 0 here (which it should be)
    return llvm::BinaryOperator::CreateOr(ptrAsInt, inPointerTagMask, "", insertBefore);
}

llvm::Value* NOOBInstrumentationPass::computeTopTag(llvm::Value* ptr, llvm::Value* radix, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    auto ptrAsInt = createBitOrPointerCastIfNecessary(ptr, int64Ty, "", insertBefore);
    return llvm::BinaryOperator::CreateLShr(ptrAsInt, llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, NOOB_TOPTAG_START}), "toptag", insertBefore);
}

llvm::Value* NOOBInstrumentationPass::computePoisonMaskAtDerefSite(const CheckInfo& checkInfo, const ComputedBasePtrInfo& basePtrInfo, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    // FIXME: "mbedtls_md5_starts_ret" in benchmark.ll has an example at the top of an unnecessary arith check. why do we have it??
    assert(checkInfo.shouldCheckDereference()); // this function assumes we're at a deref site, for poisoning
    const auto radix = basePtrInfo.radix;
    if (checkInfo.shouldCheckArith()) {
        assert(CHECK_POINTER_ARITHMETIC);
        static_assert(ARITH_LEEWAY_WIDTH == 0, "I did not account for non-zero leeway bits here");
        // common case: we check both arith & deref
        //  implement optimized instrumentation here, that checks arith & deref at once
        auto origObj = basePtrInfo.origObj;
        auto ptrAsInt = castToInt64Ty(checkInfo.pointerOperand, insertBefore);
        auto currentObj = llvm::BinaryOperator::CreateLShr(ptrAsInt, radix, "", insertBefore);
        llvm::Instruction* isInBounds = new llvm::ICmpInst(insertBefore, llvm::ICmpInst::ICMP_EQ, currentObj, origObj, "");
        if (checkInfo.isRangeCheck()) {
            // also check whether the endOfAddressRange is in bounds, and OR the result with the previous isInBounds
            auto endOfRangeAsInt = castToInt64Ty(checkInfo.endOfAddressRange, insertBefore);
            auto endOfRangeObj = llvm::BinaryOperator::CreateLShr(endOfRangeAsInt, radix, "", insertBefore);
            auto endOfRangeInBounds = new llvm::ICmpInst(insertBefore, llvm::ICmpInst::ICMP_EQ, endOfRangeObj, origObj, "");
            // isInBounds = isInBounds && endOfRangeInBounds
            isInBounds = llvm::SelectInst::Create(isInBounds, endOfRangeInBounds, isInBounds, "", insertBefore);
        }
        llvm::Instruction* poisonMask = llvm::SelectInst::Create(isInBounds, llvm::ConstantInt::getNullValue(int64Ty), llvm::ConstantInt::get(int64Ty, llvm::APInt{64, (1ULL << 48)}), "", insertBefore);
        return poisonMask;
    } else {
        // rarer case: we only check deref, because arith got pruned
        //  implement an even more optimized version here that solely checks deref
        // find the in pointer tag and top tag
        auto inPointerTag = shiftDownTillInPointerTag(checkInfo.pointerOperand, radix, insertBefore);
        // xor the iptag with the toptag
        auto isNotInBounds = llvm::BinaryOperator::CreateXor(inPointerTag, basePtrInfo.topTag, "notinbounds", insertBefore);
        if (checkInfo.isRangeCheck()) {
            auto endOfAddrIpTag = shiftDownTillInPointerTag(checkInfo.endOfAddressRange, radix, insertBefore);
            auto endOfAddrNotInBounds = llvm::BinaryOperator::CreateXor(endOfAddrIpTag, basePtrInfo.topTag, "notinbounds", insertBefore);
            // isNotInBounds = isNotInBounds || endOfAddrNotInBounds
            isNotInBounds = llvm::BinaryOperator::CreateOr(isNotInBounds, endOfAddrNotInBounds, "notinbounds", insertBefore);
            // we're going to use this mask to embed poison bits into a pointer that we feed into a loop
            //  make sure that we're not inadvertently overwriting the top bits during the masking
            //  clear the garbage top bits in this mask
            isNotInBounds = llvm::BinaryOperator::CreateAnd(isNotInBounds, llvm::ConstantInt::getIntegerValue(int64Ty, {64, (1U << TAG_WIDTH) - 1}), "", insertBefore);
        }
#if USE_BRANCHING_CHECKS // with branching checks, we verify whether the entire thing is 0
        // mask out the more significant bits
        auto poisonMask = llvm::BinaryOperator::CreateAnd(isNotInBounds, llvm::ConstantInt::getIntegerValue(int64Ty, {64, (1U << TAG_WIDTH) - 1}), "", insertBefore);
#else
        //                          64      56/57   48    42                   0 
        // place the resulting value |top bits|_here_|radix|rest of pointer ...|
        //  it's not a problem that the more significant bits of the `poisonMask` value likely aren't 0 here, they will get ignored by TBI/UAI anyway
        //  this would only be a problem for range checks, where the pointer value could escape, but we zero out the top bits of isNotInBounds for that reason
        auto poisonMask = llvm::BinaryOperator::CreateShl(isNotInBounds, llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, NOOB_TOPTAG_START - TAG_WIDTH}), "", insertBefore);
#endif
        return poisonMask;
    }
}

#if COUNT_NOOB_FAILURES
static void incrementGlobal(llvm::GlobalVariable* global, llvm::Instruction* insertBefore) {
    llvm::Instruction* count = new llvm::LoadInst(global->getValueType(), global, "", insertBefore);
    count = llvm::BinaryOperator::CreateAdd(count, llvm::ConstantInt::get(global->getValueType(), llvm::APInt{64, 1}), "", insertBefore);
    new llvm::StoreInst(count, global, insertBefore);
}
#endif

// actually modify the program to insert the checks and update the usesToReplace
void NOOBInstrumentationPass::applyNOOBChecks(llvm::Module& module, llvm::ModuleAnalysisManager& MAM, const llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>>& checkInfoToUses) {
    //  keep in mind here that multiple uses can use the same instrumentation
    //  invert the checkinfo map here to ensure we only insert instrumentation once per instpoint

    // dump before we modify too much
    dumpModuleToFile(module, "beforeinstrumentation.noob.ll");

    auto& context = module.getContext();
    auto int64Ty = llvm::Type::getInt64Ty(context);
    // create the decl for EMIT_RUNTIME_CALLS
    auto int8PtrTy = llvm::Type::getInt8PtrTy(context);
    auto boolTy = llvm::Type::getInt1Ty(context);
    auto noob_access_check_fn = module.getOrInsertFunction("noob_access_check", llvm::Type::getVoidTy(context), int8PtrTy, int8PtrTy, boolTy, boolTy);
    // create the function containing the branching instrumentation
    auto noob_assert_zero_fn = module.getOrInsertFunction("noob_assert_zero", llvm::Type::getVoidTy(context), int64Ty);
    {
        auto func = llvm::cast<llvm::Function>(noob_assert_zero_fn.getCallee());
        func->addFnAttr(llvm::Attribute::AlwaysInline);
        auto entry = llvm::BasicBlock::Create(context, "", func);
        auto ret = llvm::ReturnInst::Create(context, entry);

        auto isNotZero = new llvm::ICmpInst(ret, llvm::ICmpInst::ICMP_NE, func->getArg(0), llvm::ConstantInt::getNullValue(int64Ty), "notzero");
        llvm::MDBuilder MDHelper{context};
        auto weights = MDHelper.createBranchWeights(1, 1000);
        auto ifZeroBlockTerm = llvm::SplitBlockAndInsertIfThen(isNotZero, ret, true, weights);

#if COUNT_NOOB_FAILURES
        auto total_checks_counter = new llvm::GlobalVariable(module, int64Ty, false, llvm::GlobalValue::ExternalLinkage, llvm::ConstantInt::getNullValue(int64Ty), "noob_total_dereferences");
        incrementGlobal(total_checks_counter, isNotZero);

        auto failed_checks_counter = new llvm::GlobalVariable(module, int64Ty, false, llvm::GlobalValue::ExternalLinkage, llvm::ConstantInt::getNullValue(int64Ty), "noob_failed_checks");
        incrementGlobal(failed_checks_counter, ifZeroBlockTerm);
#else
        auto trapFn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::debugtrap);
        auto callTrap = llvm::CallInst::Create(trapFn, "", ifZeroBlockTerm);
#endif
    }

    // embed the lookup table if necessary
    RadixDecoder radixDecoder{module};
    // precompute the baseobj & radix for all trackedbases. Will be unused if not needed, otherwise good speedup
    llvm::DenseMap<llvm::Value*, llvm::DenseMap<llvm::Function*, llvm::DenseSet<CheckInfo*>>> trackedBaseToCheckInfos;
    for (auto& [checkInfo, usesToReplace] : checkInfoToUses) 
        trackedBaseToCheckInfos[checkInfo->trackedBase][checkInfo->insertBefore->getFunction()].insert(checkInfo);
    llvm::DenseMap<llvm::Value* /* trackedBase */, llvm::DenseMap<llvm::Function*, ComputedBasePtrInfo>> baseToBasePtrInfo;
    for (auto& [trackedBase, funcToCheckInfos] : trackedBaseToCheckInfos) {
        for (auto& [func, checkInfos] : funcToCheckInfos) {
            auto& domTree = getFAM(module, MAM).getResult<llvm::DominatorTreeAnalysis>(*func);

            // figure out the nearest common dominator of all checkinfos that share a trackedbase
            assert(checkInfos.size() > 0);
            auto NCD = (*checkInfos.begin())->insertBefore->getParent();
            for (auto current = ++checkInfos.begin(); current != checkInfos.end(); current++) 
                NCD = domTree.findNearestCommonDominator(NCD, (*current)->insertBefore->getParent());

            // find a suitable insertBefore in the NCD: should come before all checkInfos, but after the trackedBase
            auto insertBefore = NCD->getTerminator();
            for (auto checkInfo : checkInfos)
                if (checkInfo->insertBefore->getParent() == NCD)
                    if (checkInfo->insertBefore->comesBefore(insertBefore))
                        insertBefore = checkInfo->insertBefore;

            // do a little bit of hoisting here: if the baseobj/radix precomputation unnecessarily happens in a loop, hoist it
            auto trackedBaseDominates = [&,&trackedBase=trackedBase] (llvm::Instruction* loc) -> bool {
                // 453.povray _actually_ has a *(null + 32-bit value). When that executes, we'll simply trigger a crash.
                if (!llvm::isa<llvm::Instruction>(trackedBase))
                    return true;
                auto trackedBaseInst = llvm::dyn_cast<llvm::Instruction>(trackedBase);
                ASSERT_ELSE_UNKOWN(trackedBaseInst, trackedBase);
                ASSERT_ELSE_UNKOWN(trackedBaseInst != loc, loc);
                return domTree.dominates(trackedBaseInst, loc);
            };
            auto& loopInfo = getFAM(module, MAM).getResult<llvm::LoopAnalysis>(*func);
            while (true) {
                if (auto loop = loopInfo.getLoopFor(insertBefore->getParent()))
                    if (loop->getLoopPreheader() && trackedBaseDominates(loop->getLoopPreheader()->getTerminator())) {
                        insertBefore = loop->getLoopPreheader()->getTerminator();
                        continue;
                    }
                break;
            }

            // precompute radix from base
            auto baseAsInt = createBitOrPointerCastIfNecessary(trackedBase, int64Ty, "", insertBefore);
            auto radix = radixDecoder.computeRadix(baseAsInt, insertBefore);

            auto topTag = computeTopTag(baseAsInt, radix, insertBefore);

#if EMULATE_LOWFAT
            // precompute the objbase here for lowfat
            auto origObj = llvm::BinaryOperator::CreateLShr(baseAsInt, radix, "lowfat.baseobj", insertBefore);
#else
            // precompute "origObj" value with toptag in lower bits: most often needed value at arithderef sites
            auto origObj = llvm::BinaryOperator::CreateLShr(baseAsInt, radix, "", insertBefore);
            origObj = llvm::BinaryOperator::CreateAnd(origObj, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, ~((1ULL << TAG_WIDTH) - 1)}), "", insertBefore); // clear iptag bits
            origObj = llvm::BinaryOperator::CreateOr(origObj, topTag, "", insertBefore);
#endif
            // deref-only sites use the toptag
            baseToBasePtrInfo[trackedBase][func] = {radix, origObj, topTag};
        }
    }

    // now insert an arithmetic & tag check at all dereference sites
    llvm::DenseSet<llvm::Use*> replacedUses;
    for (auto& [checkInfo, usesToReplace] : checkInfoToUses) {
        auto insertBefore = checkInfo->insertBefore;

#if EMIT_RUNTIME_CALLS
        auto ptrAsInt8Ptr = createBitOrPointerCastIfNecessary(checkInfo->pointerOperand, int8PtrTy, "", insertBefore);
        auto baseAsInt8Ptr = createBitOrPointerCastIfNecessary(checkInfo->trackedBase, int8PtrTy, "", insertBefore);
        auto checkDerefBool = llvm::ConstantInt::getBool(boolTy, checkInfo->shouldCheckDereference());
        auto checkArithBool = llvm::ConstantInt::getBool(boolTy, checkInfo->shouldCheckArith());
        llvm::CallInst::Create(noob_access_check_fn, {ptrAsInt8Ptr, baseAsInt8Ptr, checkDerefBool, checkArithBool}, "", insertBefore);
#else
        auto basePtrInfo = baseToBasePtrInfo.find(checkInfo->trackedBase)->getSecond().find(checkInfo->insertBefore->getFunction())->getSecond();

#if EMULATE_LOWFAT
        assert(!checkInfo->shouldCheckDereference()); // we explicitly disable these because we implement lowfat as arith checks
        if (checkInfo->shouldCheckArith()) {
            auto ptrAsInt = createBitOrPointerCastIfNecessary(checkInfo->pointerOperand, int64Ty, "", insertBefore);
            auto ptrObj = llvm::BinaryOperator::CreateLShr(ptrAsInt, basePtrInfo.radix, "lowfat.ptrobj", insertBefore);
            llvm::Instruction* isInBounds = new llvm::ICmpInst(insertBefore, llvm::ICmpInst::ICMP_EQ, ptrObj, basePtrInfo.origObj, "inbounds");
            if (checkInfo->isRangeCheck()) {
                auto endPtrAsInt = createBitOrPointerCastIfNecessary(checkInfo->endOfAddressRange, int64Ty, "", insertBefore);
                auto endPtrObj = llvm::BinaryOperator::CreateLShr(endPtrAsInt, basePtrInfo.radix, "lowfat.endPtrObj", insertBefore);
                auto endPtrInBounds = new llvm::ICmpInst(insertBefore, llvm::ICmpInst::ICMP_EQ, endPtrObj, basePtrInfo.origObj, "inbounds");
                isInBounds = llvm::SelectInst::Create(isInBounds, isInBounds, endPtrInBounds, "inbounds", insertBefore);
            }
            auto poison = llvm::SelectInst::Create(isInBounds, llvm::ConstantInt::getNullValue(int64Ty), llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 1}), "", insertBefore);
            static_assert(USE_BRANCHING_CHECKS);
            auto trapIfNotZero = llvm::CallInst::Create(noob_assert_zero_fn, {poison}, "", insertBefore);
        }
#else
        if (checkInfo->isRangeCheck()) {
            // wouldnt know why such checks would ever be pruned here
            assert(checkInfo->shouldCheckDereference());
        }

        if (checkInfo->shouldCheckDereference()) {
            assert(!checkInfo->isEscapeSite);
            assert(CHECK_POINTER_DEREFERENCES);
            assert(CHECK_DEREFERENCE_SITES); // should never deref check escape sites
            auto poisonMask = computePoisonMaskAtDerefSite(*checkInfo, basePtrInfo, insertBefore);
            if (checkInfo->isRangeCheck()) {
                // crucial: make sure the pointer being poisoned later on is the actual start value of the loop
                //  this can be different than the current pointerOperand, which is simply the lowest value of the accessed range
                assert(!replacedUses.contains(*usesToReplace.begin()));
                // it's possible that there are multiple usesToReplace for a range check. 
                //  i've found a case in SPEC17 x264_s where loop-invariant & loop-variant memory accesses all used the same check
                //  in that case, we replace all the uses. 
                //  We need to pick a single pointeroperand here for all of them. 
                //    We assert that all of them really are the same pointer (this may be a weaker check than the hoisting implementation)
                //    And if they are not all the same Value, we pick the one that dominates the others. 
                auto pointerOperand = [&, &usesToReplace = usesToReplace] () -> llvm::Value* {
                    llvm::DenseSet<llvm::Value*> ptrOperands;

                    auto currentOperand = (*usesToReplace.begin())->get();
                    llvm::DominatorTree* domTree = nullptr;
                    if (usesToReplace.size() > 1)
                        domTree = &getFAM(module, MAM).getResult<llvm::DominatorTreeAnalysis>(*insertBefore->getFunction());

                    for (auto use : usesToReplace) {
                        ASSERT_ELSE_UNKOWN(use->get()->stripPointerCastsForAliasAnalysis() == currentOperand->stripPointerCastsForAliasAnalysis(), currentOperand);
                        if (use->get() != currentOperand) 
                            // if this use dominates the others, use it as the pointeroperand
                            if (auto inst = llvm::dyn_cast<llvm::Instruction>(currentOperand)) 
                                if (domTree->dominates(use->get(), inst))
                                    currentOperand = use->get();
                    }                        
                    return currentOperand;
                } ();
                checkInfo->pointerOperand = pointerOperand;
            }
            if (USE_BRANCHING_CHECKS && !checkInfo->unsoundlyHoisted) { // branching instrumentation can only guard postdom memaccesses. Use pointer poisoning for non-postdom checks
                // check if the poisonMask is 0
                auto trapIfNotZero = llvm::CallInst::Create(noob_assert_zero_fn, {poisonMask}, "", insertBefore);
            } else {
                assert(!COUNT_NOOB_FAILURES); // should not reach here
#if __x86_64__ // LAMU48 platforms have only limited canonical bits: 63th and 47th. Compute a one-bit mask poison mask for the 47th bit
                auto maskIsZero = new llvm::ICmpInst(insertBefore, llvm::ICmpInst::ICMP_EQ, poisonMask, llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, 0}));
                poisonMask = llvm::SelectInst::Create(
                    maskIsZero, 
                    llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, 0}), // if zero
                    llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, 1UL << (NOOB_TOPTAG_START - TAG_WIDTH)}), // if non zero
                    "",
                    insertBefore
                );
#endif
                // poison the pointerOperand
                auto ptrAsInt = castToInt64Ty(checkInfo->pointerOperand, insertBefore); // reset the ptrAsInt because isRangeCheck clause mightve changed pointerOperand
                llvm::Instruction* poisonedPtr = llvm::BinaryOperator::CreateOr(ptrAsInt, poisonMask, "", insertBefore);
                poisonedPtr = new llvm::IntToPtrInst(poisonedPtr, checkInfo->pointerOperand->getType(), "", insertBefore);
                // replace the access ptroperand
                checkInfo->pointerOperand = poisonedPtr;
            }
        } else if (checkInfo->shouldCheckArith()) {
            assert(checkInfo->isEscapeSite);
            // this is an escape site. wrap the pointer instead of poisoning
            //  are we sure this is an escape site though?
            //  maybe a dereference check got pruned in the hoisting?
            //  but if it got pruned, its arith check should for sure also have gotten pruned
            assert(CHECK_POINTER_ARITHMETIC);
            assert(CHECK_ESCAPE_SITES); // should never elide deref checking on deref sites
            ASSERT_ELSE_UNKOWN(!checkInfo->isRangeCheck(), checkInfo->pointerOperand); // our typical range check poisoning approach wouldnt work here
            assert(!COUNT_NOOB_FAILURES);

            auto radix = basePtrInfo.radix;

            // compute the arena identifiers for both base & offset ptr
            auto radixPlusTW = llvm::BinaryOperator::CreateAdd(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, TAG_WIDTH}), "", insertBefore);
            auto baseAsInt = createBitOrPointerCastIfNecessary(checkInfo->trackedBase, int64Ty, "", insertBefore);
            auto basearena = llvm::BinaryOperator::CreateLShr(baseAsInt, radixPlusTW, "", insertBefore);
            auto ptrAsInt = createBitOrPointerCastIfNecessary(checkInfo->pointerOperand, int64Ty, "", insertBefore);
            auto ptrarena = llvm::BinaryOperator::CreateLShr(ptrAsInt, radixPlusTW, "", insertBefore);

            // compare them
            auto poison = llvm::BinaryOperator::CreateXor(basearena, ptrarena, "", insertBefore);
            // we always use branching checks here
            llvm::CallInst::Create(noob_assert_zero_fn, {poison}, "", insertBefore);
        }

        // now replace all uses that are checked by this pointeroperand
        for (auto useToReplace : usesToReplace) {
            auto castedPtr = createBitOrPointerCastIfNecessary(checkInfo->pointerOperand, useToReplace->get()->getType(), "", llvm::cast<llvm::Instruction>(useToReplace->getUser()));
            useToReplace->set(castedPtr);
            replacedUses.insert(useToReplace);
        }
#endif // EMULATE_LOWFAT
#endif // EMIT_RUNTIME_CALLS
    }

    dumpModuleToFile(module, "afterinstrumentation.noob.ll");
}

llvm::DenseMap<llvm::Function*, llvm::DenseSet<llvm::AllocaInst*>> NOOBInstrumentationPass::findUnsafeAllocas(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
    // find all unsafe stack objects & move them
    llvm::DenseMap<llvm::Function*, llvm::DenseSet<llvm::AllocaInst*>> unsafeAllocas;
    auto& stackSafetyAnalysis = MAM.getResult<llvm::StackSafetyGlobalAnalysis>(module);
    for (auto& func : module) {
        if (func.isDeclaration())
            continue;

        for (auto& inst : llvm::instructions(func)) 
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) 
                if (!stackSafetyAnalysis.isSafe(*alloca) && alloca->isStaticAlloca()) 
                    unsafeAllocas[&func].insert(alloca);
    }

    return unsafeAllocas;
}


// move all unsafe stack objects to per-radix noobstacks
void NOOBInstrumentationPass::moveUnsafeAllocasToNOOBStacks(llvm::Module& module, const llvm::DenseMap<llvm::Function*, llvm::DenseSet<llvm::AllocaInst*>>& unsafeAllocas) {
    // create an array of per-radix noob-stackptrs
    auto& context = module.getContext();
    auto noobStackPtrArrayType = llvm::ArrayType::get(llvm::Type::getInt8PtrTy(context), 64);
    auto noobStackPtrArray = new llvm::GlobalVariable(
        module, 
        noobStackPtrArrayType,
        false, 
        llvm::GlobalVariable::PrivateLinkage, 
        llvm::ConstantAggregateZero::get(noobStackPtrArrayType), 
        "noob.stackptrarray"
    );
    auto int64Ty = llvm::Type::getInt64Ty(context);
    
    uint8_t lowestRadix = UINT8_MAX;
    uint8_t highestRadix = 0;
    for (auto& [func, allocas] : unsafeAllocas) {
        llvm::DenseMap<llvm::TypeSize::ScalarTy, llvm::SmallVector<llvm::AllocaInst*>> radixToUnsafeAllocas;
        for (auto alloca : allocas) {
            auto sizeInBitsOpt = alloca->getAllocationSizeInBits(module.getDataLayout());
            ASSERT_ELSE_UNKOWN(sizeInBitsOpt.hasValue(), alloca); // will fail for VLAs
            auto sizeInBits = sizeInBitsOpt->getFixedSize(); // may fail for VLAs still
            auto sizeInBytes = __builtin_align_up(sizeInBits, 8) / 8;
            auto alignedSizeInBytes = std::bit_ceil(sizeInBytes);
            alignedSizeInBytes = std::max(alignedSizeInBytes, alloca->getAlign().value()); // if alignment is greater, get it
            if (alignedSizeInBytes > NOOB_STACK_SIZE)
                llvm::errs() << "alignedSizeInBytes = " << alignedSizeInBytes << "\n";
            ASSERT_ELSE_UNKOWN(alignedSizeInBytes <= NOOB_STACK_SIZE, alloca);

            auto radix = std::max(3UL, std::bit_width(alignedSizeInBytes) - 1UL);
            radix = std::max(static_cast<unsigned long>(NOOB_MIN_RADIX), radix);

            // keep track of the max and min radix here
            if (radix < lowestRadix)
                lowestRadix = radix;
            if (radix > highestRadix)
                highestRadix = radix;

            ASSERT_ELSE_UNKOWN(alignedSizeInBytes >= alloca->getAlign().value(), alloca);
            radixToUnsafeAllocas[radix].push_back(alloca);
        }

        auto insertAllocaPtrsBefore = &*func->getEntryBlock().getFirstInsertionPt();
        llvm::DenseMap<llvm::AllocaInst*, llvm::Value*> allocaToNewStackAlloc;
        for (auto& [radix, allocas] : radixToUnsafeAllocas) {
            // retrieve the "thread-local" stack pointer for this radix
            auto addressOfRadixStackPtr = llvm::ConstantExpr::getInBoundsGetElementPtr(noobStackPtrArrayType, noobStackPtrArray, llvm::ArrayRef{llvm::ConstantInt::getNullValue(llvm::Type::getInt64Ty(context)), llvm::ConstantInt::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, radix})});
            auto radixStackPtr = new llvm::LoadInst(llvm::Type::getInt8PtrTy(context), addressOfRadixStackPtr, llvm::formatv("noob.stackptr.{0}", radix), insertAllocaPtrsBefore);

            // now allocate the necessary amount for this function and update the stackPtr slot in memory
            auto numObjects = allocas.size();
            auto endOfRadixStackPtr = llvm::GetElementPtrInst::CreateInBounds(llvm::Type::getInt8Ty(context), radixStackPtr, {llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, numObjects * (1ULL << radix)})}, "", insertAllocaPtrsBefore);
            new llvm::StoreInst(endOfRadixStackPtr, addressOfRadixStackPtr, insertAllocaPtrsBefore);

            // now create the offset pointers that represent the individual allocas
            for (uint idx = 0; idx < allocas.size(); idx++) {
                auto alloca = allocas[idx];
                llvm::Value* offsetStackPtr = llvm::GetElementPtrInst::CreateInBounds(llvm::Type::getInt8Ty(context), radixStackPtr, {llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, idx * (1ULL << radix)})}, "", alloca);
                // i needn't concern myself with marking these as "noalias" here -- we should only do this at the very end of the optimization pipeline
                // any optimizations that benefit from the aliasing information should have been done by now

#if NOOB_TAG_POINTERS
                // now put the in-pointer tag in the top tag to make the pointer fully NOOB-compliant
                offsetStackPtr = castToInt64Ty(offsetStackPtr, alloca);
                auto radixVal = llvm::ConstantInt::get(int64Ty, {64, radix});
                offsetStackPtr = embedInPointerTagAsTopTag(offsetStackPtr, radixVal, alloca);
#endif

                auto castedPtr = llvm::CastInst::CreateBitOrPointerCast(offsetStackPtr, alloca->getType(), alloca->getName(), alloca);
                alloca->replaceAllUsesWith(castedPtr);
            }

            // now insert the deallocation routine at all `return` locations of this function
            for (auto& bb : *func) {
                if (auto ret = llvm::dyn_cast<llvm::ReturnInst>(bb.getTerminator())) {
                    // for now, we're lazy and use the old stackPtr, as a kind of "frame pointer"
                    // we might potentially get better results by loading back from memory & recomputing here
                    new llvm::StoreInst(radixStackPtr, addressOfRadixStackPtr, ret);
                }
            }
        }

        // now delete all these allocas. we couldn't do this before because `insertBefore` was likely one of them
        for (auto& [_, allocas] : radixToUnsafeAllocas) {
            for (auto& alloca : allocas) {
                alloca->eraseFromParent();
                alloca = nullptr; // just to make sure we dont access this again
            }
        }
    }

    if (highestRadix) { // don't do anything if we didnt find a single stack alloc (e.g. specrand)
        llvm::outs() << "All stack object radices were between " << (int) lowestRadix << " and " << (int) highestRadix << ".\n";

        // now insert a function to call NOOB's stack allocation function to initialize the stackptr array
        // it will be called from the noob initialization routine
        // void noob_allocate_stacks(void** stack_array, uint8_t start_radix, uint8_t num_stacks);
        auto ptrToVoidPtrTy = llvm::PointerType::getUnqual(llvm::Type::getInt8PtrTy(context));
        auto noob_allocate_stacks_fn = module.getOrInsertFunction("noob_allocate_stacks", llvm::Type::getVoidTy(context), ptrToVoidPtrTy, llvm::Type::getInt8Ty(context), llvm::Type::getInt8Ty(context));
        auto noob_initialize_noobstacks_fn = module.getOrInsertFunction("noob_initialize_noobstacks", llvm::Type::getVoidTy(context));
        auto initializeNoobStacks = llvm::cast<llvm::Function>(noob_initialize_noobstacks_fn.getCallee());
        auto entry = llvm::BasicBlock::Create(context, "entry", initializeNoobStacks);
        auto insertBefore = llvm::ReturnInst::Create(context, entry);
        auto call_noob_allocate_stacks_fn = llvm::CallInst::Create(
            noob_allocate_stacks_fn, 
            {
                    llvm::ConstantExpr::getBitCast(noobStackPtrArray, ptrToVoidPtrTy), 
                    llvm::Constant::getIntegerValue(llvm::Type::getInt8Ty(context), 
                    llvm::APInt{8, lowestRadix}), 
                    llvm::Constant::getIntegerValue(llvm::Type::getInt8Ty(context), 
                    llvm::APInt{8, highestRadix})
                }, 
            "", insertBefore
        );
    }
}

void NOOBInstrumentationPass::maskExternalPointerArguments(llvm::Module& module) {
    for (auto& func : module)
        for (auto& bb : func)
            for (auto& inst : bb)
                if (auto call = llvm::dyn_cast<llvm::CallBase>(&inst))
                    if (auto calledFunction = call->getCalledFunction())
                        if (calledFunction->isDeclaration())
                            for (auto& arg : call->args()) 
                                if (arg->getType()->isPointerTy()) {
                                    auto insertBefore = call;
                                    auto ptrAsInt = castToInt64Ty(arg, insertBefore);
                                    auto mask = llvm::ConstantInt::get(ptrAsInt->getType(), llvm::APInt::getLowBitsSet(64, NOOB_TOPTAG_START));
                                    auto maskedInt = llvm::BinaryOperator::CreateAnd(ptrAsInt, mask, "masked.ptr", insertBefore);
                                    auto maskedPtr = createBitOrPointerCastIfNecessary(maskedInt, arg->getType(), "", insertBefore);
                                    arg.set(maskedPtr);
                                }
}

llvm::PreservedAnalyses NOOBInstrumentationPass::run(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
#if REMAP_GLOBALS
    // find unsafe globals up front, before modification
    const auto radixToGlobals = findUnsafeGlobals(module, MAM);
#endif
    // also find unsafe allocas up front
    const auto unsafeAllocas = findUnsafeAllocas(module, MAM);

    // then figure out the instrumentation plans (also on a largely unmodified module)
    //  this is because the baseptrtracker will look right through our tag embedding or unsafe stack alloc
    auto checkInfoToUses = createInstrumentationPlans(module, MAM);
    // now actually instrument pointer arithmetic and dereferences
    applyNOOBChecks(module, MAM, checkInfoToUses);

#if MASK_EXT_PTR_ARGS
    maskExternalPointerArguments(module);
#endif

    // now, start instrumenting globals & allocas
    std::string noobLinkerScript {
        // generated linker script based on target arch & noob config
        //  we extend this dynamically with global sections & segments
        #include "noob_linker_script.ld"
    };
#if REMAP_GLOBALS
    extendNOOBLinkerScript(noobLinkerScript, radixToGlobals);
#endif
    std::error_code ec;
    llvm::raw_fd_ostream linkerScript{"noob_linker_script.ld", ec};
    assert(ec.value() == 0);
    linkerScript << noobLinkerScript;

#if REPLACE_STACK_ALLOCS
    moveUnsafeAllocasToNOOBStacks(module, unsafeAllocas);
#endif

    // inline any always_inline functions we emitted as part of the instrumentation
    llvm::AlwaysInlinerPass alwaysInliner;
    alwaysInliner.run(module, MAM);

    // add IR-level debuginfo
    {
        llvm::StripDebugInfo(module); // destroy any existing debug info
        // auto instNamer = llvm::createModuleToFunctionPassAdaptor(llvm::InstructionNamerPass{});
        // instNamer.run(module, MAM);
        std::string ltoResultFileName = "LTOresult.noob.ll";
        dumpModuleToFile(module, ltoResultFileName);
        char *cwd = get_current_dir_name();
        auto DisplayM = debugir::createDebugInfo(module, cwd, ltoResultFileName);
        dumpModuleToFile(*DisplayM, ltoResultFileName);
    }

    // we are lazy and say everything is invalidated
    return llvm::PreservedAnalyses::none();
}


void NOOBInstrumentationPass::registerModuleAnalyses(llvm::ModuleAnalysisManager& MAM) {
    MemAccessInstrumentator::registerAnalyses(MAM);
}

void NOOBInstrumentationPass::registerFunctionAnalyses(llvm::FunctionAnalysisManager& FAM) {
    FAM.registerPass([] { return OSOVAnalysis{}; });
}

std::error_code ec;
llvm::raw_fd_ostream inputModule{"inputmodule.noob.ll", ec};

void NOOBInstrumentationPass::addPasses(llvm::ModulePassManager& MPM) {
    MPM.addPass(llvm::PrintModulePass{inputModule});
    IsInBoundsAnalysis::addPreparationPasses(MPM);
    MPM.addPass(NOOBInstrumentationPass{});
    MPM.addPass(llvm::VerifierPass{});
    IsInBoundsAnalysis::addCleanupPasses(MPM);
}
