#include "llvm_noob.h"

#include <NOOB/config.h>

#include <llvm-utils/util.h>
#include <llvm-utils/safetyanalysis/safetyanalysis.h>

#include <llvm/IR/Instructions.h>

llvm::PreservedAnalyses NOOBInstrumentationPass::run(llvm::Module& module, llvm::ModuleAnalysisManager& MAM) {
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

    // we are lazy and say everything is invalidated
    return llvm::PreservedAnalyses::none();
}

