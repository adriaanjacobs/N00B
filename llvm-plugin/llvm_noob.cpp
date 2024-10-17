#include "llvm_noob.h"

#include <NOOB/config.h>
#include <NOOB/memlayout.h>

#include <llvm-utils/util.h>
#include <llvm-utils/safetyanalysis/safetyanalysis.h>
#include <llvm-utils/pointerdetection/pointerdetection.h>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Analysis/StackSafetyAnalysis.h>
#include <llvm/Support/Process.h>

#include <sstream>

// LLD doesnt have a default linker script, and, hence, does not support the script syntax to "extend" it
//  but Ruben wrote one for us that we can extend dynamically
static std::string defaultLinkerScript {
#include "lld.x86-64.ld"
};

llvm::PreservedAnalyses NOOBInstrumentationPass::run(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {

#if REMAP_GLOBAS
    {
        // first find all the globals we want to wrap, compute their radix, and set the minimum alignment
        // FIXME: for now, just wrap them all
        // std::map so we get it nice and ordered
        std::map<uint64_t, llvm::SmallVector<llvm::GlobalVariable*>> radixToGlobals;
        for (auto& global : module.globals()) {
            // i dont know what to do with globals that already have a section
            ASSERT_ELSE_UNKOWN(global.getSection() == "", &global);
            // according to the lowfat guys, the linker will ignore our section attribute for symbols with common linkage
            ASSERT_ELSE_UNKOWN(!global.hasCommonLinkage(), &global);
            unsigned long alignTo = module.getDataLayout().getTypeAllocSize(global.getValueType()).getFixedSize();
            alignTo = std::max(alignTo, global.getAlign()->value());
            if (alignTo < 16)
                alignTo = 16;
            auto radix = std::bit_width(alignTo) - 1;
            global.setAlignment(llvm::Align{(1ULL << radix)});
            radixToGlobals[radix].push_back(&global);
        }

        // print it out for debugging
        for (auto& [radix, globals] : radixToGlobals) {
            llvm::outs() << "Radix " << radix << ", " << globals.size() << " globals. Size: " << (1ULL << radix) * globals.size() << "B\n";
        }

        // then extend the linker script to allocate the appropriate number of sections
        // The plan is to generate a custom linker script, save it to `noob_linker_script.ld`, and then have the user specify that filename
        //  on the command line as the -T parameter during linking. 
        llvm::SmallVector<std::string> segmentNames;
        std::stringstream sections;
        sections << "\nSECTIONS {\n";
        for (auto& [radix, globals] : radixToGlobals) {
            assert(radix < 20); // idk why we'd need globals larger than this
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

        defaultLinkerScript.append(sections.str());

        std::error_code ec;
        llvm::raw_fd_ostream linkerScript{"noob_linker_script.ld", ec};
        assert(ec.value() == 0);
        linkerScript << defaultLinkerScript;
    }
#endif

#if CHECK_POINTER_DEREFERENCES
    {
        // instrument all pointer dereferences to verify that the top bits match the in-pointer bits
        // get analysis results _before_ we start modifying the module
        auto& unsafeAccessInfo = MAM.getResult<UnsafeAccessFinderAnalysis>(module).getOrCreate(false);
        auto int64Ty = llvm::Type::getInt64Ty(module.getContext());
        for (auto access : unsafeAccessInfo.unsafeAccesses) {
            auto insertBefore = access;
            auto ptr = llvm::getLoadStorePointerOperand(access);
            ASSERT_ELSE_UNKOWN(ptr, access);

            auto ptrAsInt = new llvm::PtrToIntInst(ptr, int64Ty, "", insertBefore);
            auto radix = llvm::BinaryOperator::CreateLShr(ptrAsInt, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 42}), "", insertBefore);
            // it seems like this below does not end up in machine code with bmi2's shrx, because it automatically masks to qword width
            radix = llvm::BinaryOperator::CreateAnd(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 0b0011'1111}), "", insertBefore);

            // compute the mask to XOR with based on the lowest TAG_WIDTH bits of the base pointer
            auto invariantBitMask = llvm::BinaryOperator::CreateLShr(ptrAsInt, radix, "", insertBefore);
            invariantBitMask = llvm::BinaryOperator::CreateShl(invariantBitMask, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 64 - TAG_WIDTH}), "", insertBefore);

            // xor the pointer and replace the access ptroperand
            auto maskedPtrAsInt = llvm::BinaryOperator::CreateXor(ptrAsInt, invariantBitMask, "", insertBefore);
            auto maskedPtr= new llvm::IntToPtrInst(maskedPtrAsInt, ptr->getType(), "", insertBefore);
            
            if (auto load = llvm::dyn_cast<llvm::LoadInst>(access))
                load->setOperand(load->getPointerOperandIndex(), maskedPtr);
            else {
                auto store = llvm::cast<llvm::StoreInst>(access);
                ASSERT_ELSE_UNKOWN(store, access);
                store->setOperand(store->getPointerOperandIndex(), maskedPtr);
            }
        }
    }
#endif

#if CHECK_POINTER_ARITHMETIC
    {
        // compute the base pointer of pointer arithmetic, ensure it is always checked
        auto& pointerInfo = MAM.getResult<PointerDetectionAnalysis>(module);
        auto& unsafeAccessInfo = MAM.getResult<UnsafeAccessFinderAnalysis>(module).getOrCreate(false);
        for (auto& access : unsafeAccessInfo.unsafeAccesses) {
            auto insertBefore = access;
            auto ptr = llvm::getLoadStorePointerOperand(access);
            ASSERT_ELSE_UNKOWN(ptr, access);

            auto base = pointerInfo.find_real_base(ptr);

            // first borrow cuCatch's trick to propagate base pointers from source through selects/phis
            // then insert an arithmetic check at all dereference sites

            // finally, find all locations where pointers "escape" after having arithmetic applied: 
            //  e.g., to external code, stored to memory, returned from function, etc.
            // only match pointers that have not been checked by the dereference yet!
            //  but _do_ match pointers that could have code paths from source on which they are not checked
            // place an additional check there

            // the arithmetic check will look like this:
            /*
                %baseint = ptrtoint ptr %base to i64
                %ptrint = ptrtoint ptr %ptr to i64
                %radix = compute_radix(%base)
                %mask_invariant_bits = shl nsw i64 -512, %radix
                %aritharea_base = and i64 %mask_invariant_bits, %baseint
                %mask_variant_bits = xor i64 %mask_invariant_bits, -1
                %masked_ptr = and i64 %mask_variant_bits, %ptrint
                %diff = sub i64 %masked_ptr, %baseint
                %2 = getelementptr i8, ptr %base, i64 %diff
                %safe_ptr = getelementptr i8, ptr %2, i64 %aritharea_base
            */

            // compiles down to quite efficient assembly in both x86 and ARM
        }
    }
#endif

#if REPLACE_STACK_ALLOCS
    {
        // now move all unsafe stack objects to per-radix noobstacks
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

        // find all unsafe stack objects & move them
        auto& stackSafetyAnalysis = MAM.getResult<llvm::StackSafetyGlobalAnalysis>(module);
        uint8_t lowestRadix = UINT8_MAX;
        uint8_t highestRadix = 0;
        for (auto& func : module) {
            if (func.isDeclaration())
                continue;

            llvm::DenseMap<llvm::TypeSize::ScalarTy, llvm::SmallVector<llvm::AllocaInst*>> radixToUnsafeAllocas;
            for (auto& inst : llvm::instructions(func)) {
                if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
                    if (!stackSafetyAnalysis.isSafe(*alloca) && alloca->isStaticAlloca()) {
                        auto sizeInBitsOpt = alloca->getAllocationSizeInBits(module.getDataLayout());
                        ASSERT_ELSE_UNKOWN(sizeInBitsOpt.hasValue(), alloca); // will fail for VLAs
                        auto sizeInBits = sizeInBitsOpt->getFixedSize(); // may fail for VLAs still
                        auto sizeInBytes = __builtin_align_up(sizeInBits, 8) / 8;
                        auto alignedSizeInBytes = std::bit_ceil(sizeInBytes);
                        alignedSizeInBytes = std::max(alignedSizeInBytes, alloca->getAlign().value()); // if alignment is greater, get it
                        auto radix = std::max(3UL, std::bit_width(alignedSizeInBytes) - 1);

                        // keep track of the max and min radix here
                        if (radix < lowestRadix)
                            lowestRadix = radix;
                        if (radix > highestRadix)
                            highestRadix = radix;

                        ASSERT_ELSE_UNKOWN(alignedSizeInBytes >= alloca->getAlign().value(), alloca);
                        radixToUnsafeAllocas[radix].push_back(alloca);
                    }
                }
            }

            auto insertBefore = &*func.getEntryBlock().getFirstInsertionPt();
            llvm::DenseMap<llvm::AllocaInst*, llvm::Value*> allocaToNewStackAlloc;
            for (auto& [radix, allocas] : radixToUnsafeAllocas) {
                // retrieve the "thread-local" stack pointer for this radix
                auto addressOfRadixStackPtr = llvm::ConstantExpr::getInBoundsGetElementPtr(noobStackPtrArrayType, noobStackPtrArray, llvm::ArrayRef{llvm::ConstantInt::getNullValue(llvm::Type::getInt64Ty(context)), llvm::ConstantInt::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, radix})});
                auto radixStackPtr = new llvm::LoadInst(llvm::Type::getInt8PtrTy(context), addressOfRadixStackPtr, llvm::formatv("noob.stackptr.{0}", radix), insertBefore);

                // now allocate the necessary amount for this function and update the stackPtr slot in memory
                auto numObjects = allocas.size();
                auto endOfRadixStackPtr = llvm::GetElementPtrInst::CreateInBounds(llvm::Type::getInt8Ty(context), radixStackPtr, {llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, numObjects * (1ULL << radix)})}, "", insertBefore);
                new llvm::StoreInst(endOfRadixStackPtr, addressOfRadixStackPtr, insertBefore);

                // now create the offset pointers that represent the individual allocas
                for (uint idx = 0; idx < allocas.size(); idx++) {
                    auto alloca = allocas[idx];
                    auto offsetStackPtr = llvm::GetElementPtrInst::CreateInBounds(llvm::Type::getInt8Ty(context), radixStackPtr, {llvm::Constant::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, idx * (1ULL << radix)})}, "", alloca);
                    // i needn't concern myself with marking these as "noalias" here -- we should only do this at the very end of the optimization pipeline
                    // any optimizations that benefit from the aliasing information should have been done by now
                    auto castedPtr = llvm::CastInst::CreatePointerCast(offsetStackPtr, alloca->getType(), alloca->getName(), alloca);
                    alloca->replaceAllUsesWith(castedPtr);
                }

                // now insert the deallocation routine at all `return` locations of this function
                for (auto& bb : func) {
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
            llvm::errs() << "All stack object radices were between " << (int) lowestRadix << " and " << (int) highestRadix << ".\n";

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
#endif

    // we are lazy and say everything is invalidated
    return llvm::PreservedAnalyses::none();
}

