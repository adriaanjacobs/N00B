#include "llvm_noob.h"
#include <NOOB/config.h>

RadixDecoder::RadixDecoder(llvm::Module& module) :
    module{module}
{

#if NON_NOOB_MIN_RADIX < 48 // we cannot transparently ignore the instrumentation, embed a table!
    auto int8Ty = llvm::Type::getInt8Ty(module.getContext());
    constexpr uint64_t num_elements = NON_NOOB_MIN_RADIX - NOOB_MIN_RADIX + 1; // minimum number of distinct radix values;
    static_assert(num_elements == 32); // we should never be embedding this table if we have a full 6-bit radix field
    auto arrayType = llvm::ArrayType::get(int8Ty, num_elements);
    llvm::SmallVector<llvm::Constant*> tableValues;
    for (uint i = 0; i < num_elements; i++) // first set all the noob-managed radices
#if NOOB_IGNORE_ERRORS // all noob-managed radices decode to value 48
        tableValues.push_back(llvm::ConstantInt::get(int8Ty, llvm::APInt{8, 48}));
#else // noob-managed radices just decode to their actual value
        tableValues.push_back(llvm::ConstantInt::get(int8Ty, llvm::APInt{8, i + NOOB_MIN_RADIX}));
#endif
    // all non-noob-managed radices decode to 48
    for (uint i = (NON_NOOB_MIN_RADIX - NOOB_MIN_RADIX); i < num_elements; i++)
        tableValues[i] = llvm::ConstantInt::get(int8Ty, llvm::APInt{8, 48});

    auto radixTableInitializer = llvm::ConstantArray::get(arrayType, tableValues);
    radixTable = new llvm::GlobalVariable(module, arrayType, true, llvm::GlobalVariable::PrivateLinkage, radixTableInitializer, "noob.radixtable");
#endif

}

llvm::Value* RadixDecoder::computeRadix(llvm::Value* ptrAsInt, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    llvm::Instruction* radix = llvm::BinaryOperator::CreateLShr(
        ptrAsInt, 
        llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 42}), 
        "", 
        insertBefore
    );
#if NON_NOOB_MIN_RADIX < 48 // we use a small lookup table to decode the physical radix into a virtual radix
    // mask out the top tag
    radix = llvm::BinaryOperator::CreateAnd(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 0xFF}), "", insertBefore);
    auto gep = llvm::GetElementPtrInst::CreateInBounds(radixTable->getValueType(), radixTable, {llvm::Constant::getNullValue(int64Ty), radix}, "radixptr", insertBefore);
    radix = new llvm::LoadInst(gep->getResultElementType(), gep, "", insertBefore);
    radix = llvm::CastInst::CreateZExtOrBitCast(radix, int64Ty, "radix", insertBefore);
#else // no need for a non-linear mapping, we compute the radix value using simple arithmetic
#if NOOB_IGNORE_ERRORS
    // fake out the instrumentation by always looking up the NON_NOOB_MIN_RADIX value here
    //  to prevent the compiler from optimizing on this, we add them with some bits that we know are 0, but the compiler doesn't. 
    radix = llvm::BinaryOperator::CreateLShr(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 6}), "", insertBefore);
    radix = llvm::BinaryOperator::CreateAdd(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, NON_NOOB_MIN_RADIX - NOOB_MIN_RADIX}), "", insertBefore);
#endif
    // decode the in-pointer radix value into a logical one
    radix = llvm::BinaryOperator::CreateAdd(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, NOOB_MIN_RADIX}), "", insertBefore);
    // mask out the top tag
    radix = llvm::BinaryOperator::CreateAnd(radix, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 0xFF}), "", insertBefore);
#endif
    return radix;
}
