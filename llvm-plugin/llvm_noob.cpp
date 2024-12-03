#include "llvm_noob.h"

#include <NOOB/config.h>
#include <NOOB/memlayout.h>

#include <llvm-utils/util.h>
#include <llvm-utils/safetyanalysis/safetyanalysis.h>
#include <llvm-utils/pointerdetection/pointerdetection.h>
#include <llvm-utils/addressability/addressability.h>
#include <llvm-utils/instrpointoptimization/hoistloopmemaccesses.h>

#include <debugir/DebugIR.h>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/Analysis/StackSafetyAnalysis.h>
#include <llvm/Transforms/IPO/AlwaysInliner.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include <sstream>

// find all the globals we want to wrap, compute their radix, and set the minimum alignment
// std::map so we get it nice and ordered
std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>> NOOBInstrumentationPass::findUnsafeGlobals(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
    auto& unsafeAccessInfo = MAM.getResult<UnsafeAccessFinderAnalysis>(module).getOrCreate(false);
    auto& callSiteAnalysis = MAM.getResult<CallSiteAnalysis>(module);
    std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>> radixToGlobals;
    size_t numSafeGlobals = 0;
    for (auto& global : module.globals()) {
        // globals that cannot flow into unsafeaccesses should be left alone
        //  this helps deal with opaque/llvm-inserted globals that have special section info etc.
        //  e.g. @llvm.used
        if (!ptrMayReachUnsafeAccesses(&global, unsafeAccessInfo, callSiteAnalysis)) {
            numSafeGlobals++;
            continue;
        }

        // i dont know what to do with globals that already have a section
        ASSERT_ELSE_UNKOWN(global.getSection() == "", &global);
        // according to the lowfat guys, the linker will ignore our section attribute for symbols with common linkage
        ASSERT_ELSE_UNKOWN(!global.hasCommonLinkage(), &global);
        unsigned long alignTo = module.getDataLayout().getTypeAllocSize(global.getValueType()).getFixedSize();
        // enforce a minimum alignment for globals that have no alignment, a too small alignment, a too small size, or a combination
        alignTo = std::max(alignTo, global.getAlign().hasValue() ? global.getAlign()->value() : 16);
        if (alignTo < 16)
            alignTo = 16;
        auto radix = std::bit_width(alignTo - 1);
        // fucking sjeng has a 2MB global for fucks sake
        // that's a 1GB arena, with two reserves around it :(((((
        // fuck that shit bro
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
        if (TAG_WIDTH <= 8)
            assert(radix >= 4); // otherwise we have to update the linker script to avoid non-page aligned memory regions for TAG_WIDTH=8
        assert(std::popcount(single_arena_size(radix)) == 1);
        auto needed_size =  (1ULL << radix) * globals.size();
        auto num_occupied_arenas = __builtin_align_up(needed_size, single_arena_size(radix)) / single_arena_size(radix);
        // always start and end with a reserved region
        auto needed_arenas = 1 + num_occupied_arenas * 2;
        // layout is: reserved | repeat [occupied | reserved]
        for (uint i = 0; i < needed_arenas; i++) {
            std::string suffix = "RESERVED";
            bool isReserved = i % 2 == 0; // odd index means occupied
            if (!isReserved) 
                suffix = "OCCUPIED";
            auto base_address = size_region_base(radix) + i * single_arena_size(radix);
            auto segmentName = llvm::formatv("noob_globals_radix{0}_{1}_{2}", radix, i, suffix).str();
            segmentNames.push_back(segmentName);
            sections << llvm::formatv("  . = SEGMENT_START(\"{0}\", {1:x});\n", segmentName, base_address).str();
            sections << llvm::formatv("  .{0} : { KEEP(*(.{0})) } : {0}\n", segmentName).str();
        }

        sections << "\n";

        for (uint i = 0; i < globals.size(); i++) {
            auto arena_idx = i / (1ULL << TAG_WIDTH);
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

// we analyze & optimize the placement of NOOB bounds checks here
//  there are 2 types of NOOB bounds checks: 
//      1. dereference checks: verify that the tag in the top pointer bits matches the tag embedded in the pointer address
//      2. arithmetic checks: verify that pointer arithmetic does not modify any of the more significant bits, 
//          so that in-pointer tags cannot repeat, radix info/top tag is not corrupted, etc.
llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>> NOOBInstrumentationPass::createInstrumentationPlans(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
    auto& unsafeAccessInfo = MAM.getResult<UnsafeAccessFinderAnalysis>(module).getOrCreate(false);
    MAM.getCachedResult<IsInBoundsAnalysis>(module)->printBailStats();
    auto& pointerInfo = MAM.getResult<PointerDetectionAnalysis>(module);

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

    for (auto& access : unsafeAccessInfo.unsafeAccesses) {
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
        // this use shouldnt exist yet
        ASSERT_ELSE_UNKOWN(!funcToCheckPoints[access->getFunction()].count(&ptrUse), access);
        auto newCheckInfo = new CheckInfo(access, ptrUse.get(), CHECK_POINTER_DEREFERENCES);
        funcToCheckPoints[access->getFunction()][&ptrUse] = newCheckInfo;
        dbg_checkInfos.insert(newCheckInfo);
    }

    LoopHoister loopHoister{module, MAM};
    loopHoister.hoistLoopBoundMemAccesses(funcToCheckPoints, true);

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

    auto& isInBoundsAnalysis = MAM.getResult<IsInBoundsAnalysis>(module);
    // add escape sites too, only for unsafe pointers
    for (auto& pointer : pointerInfo.pointers) {
        ASSERT_ELSE_UNKOWN(pointerInfo.is_confirmed_pointer(pointer), pointer);
        // do not wrap escaping constant pointers (obviously) or pointers that are in bounds
        if (llvm::isa<llvm::Constant>(pointer) || isInBoundsAnalysis.isInBounds(pointer))
            continue;

        llvm::DenseSet<llvm::Use*> escapeSites;
        collectIntraProceduralPtrEscapes(pointer, escapeSites, pointerInfo);
        for (auto* use : escapeSites) {
            auto user = use->getUser();
            // the user is always an instruction here!! (otherwise we're doing an unsafe but constant escape??)
            auto userInst = llvm::dyn_cast<llvm::Instruction>(user);
            ASSERT_ELSE_UNKOWN(userInst, user);
            // this use shouldnt exist yet
            ASSERT_ELSE_UNKOWN(!funcToCheckPoints[userInst->getFunction()].count(use), user);
            auto newCheckInfo = new CheckInfo(userInst, use->get(), false);
            funcToCheckPoints[userInst->getFunction()][use] = newCheckInfo;
            dbg_checkInfos.insert(newCheckInfo);
        }
    }

    // sanitize: copy all existing checkinfos for a later sanity check
    llvm::DenseMap<CheckInfo*, CheckInfo> dbg_checkInfoToCopy;
    for (auto& [checkInfo, _] : checkInfoToUses) {
        bool dbg = dbg_checkInfoToCopy.try_emplace(checkInfo, *checkInfo).second;
        assert(dbg);
    }

    // then optimize the list _again_. the loophoister will share expanded SCEVs for both i think
    //  the advantage is that this will eliminate dominated arithmetic checks and such
    loopHoister.hoistLoopBoundMemAccesses(funcToCheckPoints, true);

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
    BasePtrTracker basePtrTracker{module, MAM};
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

[[maybe_unused]]
static llvm::Value* computeRadix(llvm::Value* ptrAsInt, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    auto radix = llvm::BinaryOperator::CreateLShr(
        ptrAsInt, 
        llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 42}), 
        "", 
        insertBefore
    );
    // it seems like this below does not end up in machine code with bmi2's shrx, because it automatically masks to qword width
    return llvm::BinaryOperator::CreateAnd(
        radix, 
        llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 0b0011'1111}), 
        "", 
        insertBefore
    );
}

static llvm::Value* computeInPointerTagMask(llvm::Value* ptrAsInt, llvm::Value* radix,  llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());

    auto inPointerTag = llvm::BinaryOperator::CreateLShr(ptrAsInt, radix, "", insertBefore);
    auto inPointerTagMask = llvm::BinaryOperator::CreateShl(inPointerTag, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 64 - TAG_WIDTH}), "", insertBefore);
    return inPointerTagMask;
}

// actually modify the program to insert the checks and update the usesToReplace
void NOOBInstrumentationPass::applyNOOBChecks(llvm::Module& module, const llvm::DenseMap<CheckInfo*, llvm::DenseSet<llvm::Use*>>& checkInfoToUses) {
    //  keep in mind here that multiple uses can use the same instrumentation
    //  invert the checkinfo map here to ensure we only insert instrumentation once per instpoint

    auto& context = module.getContext();
    auto int64Ty = llvm::Type::getInt64Ty(context);
    auto int8PtrTy = llvm::Type::getInt8PtrTy(context);
    auto int8Ty = llvm::Type::getInt8Ty(context);
    auto boolTy = llvm::Type::getInt1Ty(context);
    auto noob_access_check_fn = module.getOrInsertFunction("noob_access_check", llvm::Type::getVoidTy(context), int8PtrTy, int8PtrTy, boolTy, boolTy);
#if ARITH_CHECK_BRANCH
    auto noob_assert_arithcheck_fn = module.getOrInsertFunction("noob_assert_arithcheck", llvm::Type::getVoidTy(context), int64Ty, int64Ty);
    llvm::cast<llvm::Function>(noob_assert_arithcheck_fn.getCallee())->addFnAttr(llvm::Attribute::AlwaysInline);
    { // populate the arithmetic checking function
        auto func = llvm::cast<llvm::Function>(noob_assert_arithcheck_fn.getCallee());
        auto entry = llvm::BasicBlock::Create(context, "entry", func);
        auto ret = llvm::ReturnInst::Create(context, entry);

        auto cmp = new llvm::ICmpInst(ret, llvm::ICmpInst::ICMP_NE, func->getArg(0), func->getArg(1));
        auto thenBlockTerm = llvm::SplitBlockAndInsertIfThen(cmp, ret, true);
        assert(thenBlockTerm != ret);

        auto trap_fn = llvm::Intrinsic::getDeclaration(&module, llvm::Intrinsic::trap);
        auto callTrap = llvm::CallInst::Create(trap_fn, "", thenBlockTerm);
    }
#endif
    // now insert an arithmetic & tag check at all dereference sites
    for (auto& [checkInfo, usesToReplace] : checkInfoToUses) {
        // FIXME: we dont support range checking yet. we would currently not correctly replace the uses
        if (checkInfo->isRangeCheck())
            continue;

        auto insertBefore = checkInfo->insertBefore;

#if EMIT_RUNTIME_CALLS
        auto ptrAsInt8Ptr = createBitOrPointerCastIfNecessary(checkInfo->pointerOperand, int8PtrTy, "", insertBefore);
        auto baseAsInt8Ptr = createBitOrPointerCastIfNecessary(checkInfo->trackedBase, int8PtrTy, "", insertBefore);
        auto checkDerefBool = llvm::ConstantInt::getBool(boolTy, checkInfo->shouldCheckDereference());
        auto checkArithBool = llvm::ConstantInt::getBool(boolTy, checkInfo->shouldCheckArith());
        llvm::CallInst::Create(noob_access_check_fn, {ptrAsInt8Ptr, baseAsInt8Ptr, checkDerefBool, checkArithBool}, "", insertBefore);
#else
        // compute radix from base! otherwise potentially overwritten in ptr
        auto baseAsInt = llvm::CastInst::CreateBitOrPointerCast(checkInfo->trackedBase, int64Ty, "", insertBefore);
        auto radix = computeRadix(baseAsInt, insertBefore);

        // check if the dereferenced pointer is immediately loaded/created, i.e., no arithmetic happened on it
        if (checkInfo->shouldCheckArith()) {
            ASSERT_ELSE_UNKOWN(CHECK_POINTER_ARITHMETIC, checkInfo->trackedBase);
            // maskForInvariantBits = (~0ULL) << (radix + TAG_WIDTH + ARITH_LEEWAY_WIDTH);
            // = (~0ULL << (TAG_WIDTH + ARITH_LEEWAY_WIDTH)) << radix
            llvm::Value* maskInvariantBits = llvm::ConstantExpr::getShl(
                llvm::Constant::getAllOnesValue(int64Ty),
                llvm::Constant::getIntegerValue(int64Ty, llvm::APInt{64, TAG_WIDTH + ARITH_LEEWAY_WIDTH})
            );
            maskInvariantBits = llvm::BinaryOperator::CreateNSWShl(
                maskInvariantBits,
                radix,
                "",
                insertBefore
            );
            auto arithAreaBase = llvm::BinaryOperator::CreateAnd(maskInvariantBits, baseAsInt, "", insertBefore);
            
            auto ptrAsInt = llvm::CastInst::CreateBitOrPointerCast(checkInfo->pointerOperand, int64Ty, "", insertBefore);
            auto arithAreaSize = llvm::BinaryOperator::CreateXor(
                maskInvariantBits, 
                llvm::Constant::getIntegerValue(int64Ty, llvm::APInt{64, static_cast<uint64_t>(-1), true}), 
                "", 
                insertBefore
            );
            auto inArithAreaOffset = llvm::BinaryOperator::CreateAnd(arithAreaSize, ptrAsInt, "", insertBefore);
            auto safePtrAsInt = llvm::BinaryOperator::CreateAdd(arithAreaBase, inArithAreaOffset, "", insertBefore);
#if ARITH_CHECK_BRANCH
            auto callCheckArith = llvm::CallInst::Create(noob_assert_arithcheck_fn, {ptrAsInt, safePtrAsInt}, "", insertBefore);
#else
            auto diffWithBase = llvm::BinaryOperator::CreateSub(safePtrAsInt, baseAsInt, "", insertBefore);
            auto baseAsInt8Ptr = llvm::CastInst::CreateBitOrPointerCast(checkInfo->trackedBase, int8PtrTy, "", insertBefore);
            llvm::Instruction* provenancedSafePtr = llvm::GetElementPtrInst::Create(int8Ty, baseAsInt8Ptr, {diffWithBase}, "", insertBefore);
            provenancedSafePtr = llvm::CastInst::CreateBitOrPointerCast(provenancedSafePtr, checkInfo->pointerOperand->getType(), "", insertBefore);
            checkInfo->pointerOperand = provenancedSafePtr;
#endif
        }

        // now instrument all pointer dereferences to verify that the top bits match the in-pointer bits
        if (checkInfo->shouldCheckDereference()) {
            assert(CHECK_POINTER_DEREFERENCES);
            auto ptr = checkInfo->pointerOperand;            
            auto ptrAsInt = new llvm::PtrToIntInst(ptr, int64Ty, "", insertBefore);

            // compute the mask to XOR with based on the lowest TAG_WIDTH bits of the base pointer
            auto inPointerTagMask = computeInPointerTagMask(ptrAsInt, radix, insertBefore);
            // shift right by TAG_WIDTH bits
            inPointerTagMask = llvm::BinaryOperator::CreateLShr(inPointerTagMask, llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, TAG_WIDTH}), "", insertBefore);

            // compute the mask for the top bits too
            //  mask out the non-toptag bits
            auto topTagMask = llvm::BinaryOperator::CreateAnd(ptrAsInt, llvm::Constant::getIntegerValue(int64Ty, llvm::APInt{64, ~((1ULL << (64 - TAG_WIDTH)) - 1)}), "", insertBefore);
            //  shift the toptag down by TAG_WIDTH
            topTagMask = llvm::BinaryOperator::CreateLShr(topTagMask, llvm::ConstantInt::getIntegerValue(int64Ty, llvm::APInt{64, TAG_WIDTH}), "", insertBefore);

            // xor them to verify equality
            auto resultMask = llvm::BinaryOperator::CreateXor(topTagMask, inPointerTagMask, "", insertBefore);

            // xor the pointer with the result and replace the access ptroperand
            auto maskedPtrAsInt = llvm::BinaryOperator::CreateXor(ptrAsInt, resultMask, "", insertBefore);
            auto maskedPtr= new llvm::IntToPtrInst(maskedPtrAsInt, ptr->getType(), "", insertBefore);
            checkInfo->pointerOperand = maskedPtr;
        }

        // now replace all uses that are checked by this pointeroperand
        for (auto useToReplace : usesToReplace) {
            auto castedPtr = createBitOrPointerCastIfNecessary(checkInfo->pointerOperand, useToReplace->get()->getType(), "", llvm::cast<llvm::Instruction>(useToReplace->getUser()));
            useToReplace->set(castedPtr);
        }
#endif
    }
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

            auto radix = std::max(3UL, std::bit_width(alignedSizeInBytes) - 1);

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

                // now put the in-pointer tag in the top tag to make the pointer fully NOOB-compliant
                offsetStackPtr = castToInt64Ty(offsetStackPtr, alloca);
                auto radixVal = llvm::ConstantInt::get(int64Ty, {64, radix});
                auto inPointerTagMask = computeInPointerTagMask(offsetStackPtr, radixVal, alloca);
                // assuming that offsetStackPtr's top tag is 0 here (which it should be)
                offsetStackPtr = llvm::BinaryOperator::CreateOr(offsetStackPtr, inPointerTagMask, "", alloca);

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

llvm::PreservedAnalyses NOOBInstrumentationPass::run(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) { 
    // find unsafe globals up front, before modification
    const auto radixToGlobals = findUnsafeGlobals(module, MAM);
    // also find unsafe allocas up front
    const auto unsafeAllocas = findUnsafeAllocas(module, MAM);

    // then figure out the instrumentation plans (also on a largely unmodified module)
    //  this is because the baseptrtracker will look right through our tag embedding or unsafe stack alloc
    auto checkInfoToUses = createInstrumentationPlans(module, MAM);
    // now actually instrument pointer arithmetic and dereferences
    applyNOOBChecks(module, checkInfoToUses);

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
        std::string ltoResultFileName = "LTOresult.noob.ll";
        dumpModuleToFile(module, ltoResultFileName);
        char *cwd = get_current_dir_name();
        auto DisplayM = debugir::createDebugInfo(module, cwd, ltoResultFileName);
        dumpModuleToFile(*DisplayM, ltoResultFileName);
    }

    // we are lazy and say everything is invalidated
    return llvm::PreservedAnalyses::none();
}


void NOOBInstrumentationPass::registerAnalyses(llvm::ModuleAnalysisManager& MAM) {
    MemAccessInstrumentator::registerAnalyses(MAM);
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
