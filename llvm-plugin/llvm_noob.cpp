#include "llvm_noob.h"

#include <NOOB/config.h>

#include <llvm-utils/util.h>
#include <llvm-utils/safetyanalysis/safetyanalysis.h>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Analysis/StackSafetyAnalysis.h>

llvm::PreservedAnalyses NOOBInstrumentationPass::run(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
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
    for (auto& func : module) {
        if (func.isDeclaration())
            continue;

        llvm::DenseMap<llvm::TypeSize::ScalarTy, llvm::SmallVector<llvm::AllocaInst*>> radixToUnsafeAllocas;
        for (auto& inst : llvm::instructions(func)) {
            if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
                if (!stackSafetyAnalysis.isSafe(*alloca)) {
                    auto sizeInBitsOpt = alloca->getAllocationSizeInBits(module.getDataLayout());
                    ASSERT_ELSE_UNKOWN(sizeInBitsOpt.hasValue(), alloca); // will fail for VLAs
                    auto sizeInBits = sizeInBitsOpt->getFixedSize(); // may fail for VLAs still
                    auto sizeInBytes = __builtin_align_up(sizeInBits, 8) / 8;
                    auto alignedSizeInBytes = std::bit_ceil(sizeInBytes);
                    auto radix = std::bit_width(alignedSizeInBytes) - 1;
                    ASSERT_ELSE_UNKOWN(alignedSizeInBytes >= alloca->getAlign().value(), alloca); // otherwise choose the max of both
                    radixToUnsafeAllocas[radix].push_back(alloca);
                }
            }
        }

        auto insertBefore = &*func.getEntryBlock().getFirstInsertionPt();
        llvm::DenseMap<llvm::AllocaInst*, llvm::Value*> allocaToNewStackAlloc;
        for (auto& [radix, allocas] : radixToUnsafeAllocas) {
            // retrieve the "thread-local" stack pointer for this radix
            auto addressOfRadixStackPtr = llvm::ConstantExpr::getInBoundsGetElementPtr(llvm::Type::getInt8PtrTy(context), noobStackPtrArray, llvm::ConstantInt::getIntegerValue(llvm::Type::getInt64Ty(context), llvm::APInt{64, radix}));
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

    // we are lazy and say everything is invalidated
    return llvm::PreservedAnalyses::none();
}

