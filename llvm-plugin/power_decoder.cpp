#include "llvm_noob.h"
#include <NOOB/config.h>

PowerDecoder::PowerDecoder(llvm::Module& module) :
    module{module}
{

#if NON_NOOB_MIN_POWER < 48 // we cannot transparently ignore the instrumentation, embed a table!
    auto int8Ty = llvm::Type::getInt8Ty(module.getContext());
    constexpr uint64_t num_elements = NON_NOOB_MIN_POWER - NOOB_MIN_POWER + 1; // minimum number of distinct power values;
    static_assert(num_elements == 32); // we should never be embedding this table if we have a full 6-bit power field
    auto arrayType = llvm::ArrayType::get(int8Ty, num_elements);
    llvm::SmallVector<llvm::Constant*> tableValues;
    for (uint i = 0; i < num_elements; i++) // first set all the noob-managed powers
#if NOOB_IGNORE_ERRORS // all noob-managed powers decode to value 48
        tableValues.push_back(llvm::ConstantInt::get(int8Ty, llvm::APInt{8, 48}));
#else // noob-managed powers just decode to their actual value
        tableValues.push_back(llvm::ConstantInt::get(int8Ty, llvm::APInt{8, i + NOOB_MIN_POWER}));
#endif
    // all non-noob-managed powers decode to 48
    for (uint i = (NON_NOOB_MIN_POWER - NOOB_MIN_POWER); i < num_elements; i++)
        tableValues[i] = llvm::ConstantInt::get(int8Ty, llvm::APInt{8, 48});

    auto powerTableInitializer = llvm::ConstantArray::get(arrayType, tableValues);
    powerTable = new llvm::GlobalVariable(module, arrayType, true, llvm::GlobalVariable::PrivateLinkage, powerTableInitializer, "noob.powertable");
#endif

}

llvm::Value* PowerDecoder::computePower(llvm::Value* ptrAsInt, llvm::Instruction* insertBefore) {
    auto int64Ty = llvm::Type::getInt64Ty(insertBefore->getContext());
    llvm::Instruction* power = llvm::BinaryOperator::CreateLShr(
        ptrAsInt, 
        llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 42}), 
        "", 
        insertBefore
    );
#if NON_NOOB_MIN_POWER < 48 // we use a small lookup table to decode the physical power into a virtual power
    // mask out the top tag
    power = llvm::BinaryOperator::CreateAnd(power, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 0xFF}), "", insertBefore);
    auto gep = llvm::GetElementPtrInst::CreateInBounds(powerTable->getValueType(), powerTable, {llvm::Constant::getNullValue(int64Ty), power}, "powerptr", insertBefore);
    power = new llvm::LoadInst(gep->getResultElementType(), gep, "", insertBefore);
    power = llvm::CastInst::CreateZExtOrBitCast(power, int64Ty, "power", insertBefore);
#else // no need for a non-linear mapping, we compute the power value using simple arithmetic
#if NOOB_IGNORE_ERRORS
    // fake out the instrumentation by always looking up the NON_NOOB_MIN_POWER value here
    //  to prevent the compiler from optimizing on this, we add them with some bits that we know are 0, but the compiler doesn't. 
    power = llvm::BinaryOperator::CreateLShr(power, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 6}), "", insertBefore);
    power = llvm::BinaryOperator::CreateAdd(power, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, NON_NOOB_MIN_POWER - NOOB_MIN_POWER}), "", insertBefore);
#endif
    // decode the in-pointer power value into a logical one
    power = llvm::BinaryOperator::CreateAdd(power, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, NOOB_MIN_POWER}), "", insertBefore);
    // mask out the top tag
    power = llvm::BinaryOperator::CreateAnd(power, llvm::ConstantInt::get(int64Ty, llvm::APInt{64, 0xFF}), "", insertBefore);
#endif
    return power;
}
