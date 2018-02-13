#include "Builder.h"

#include <llvm/IR/CallSite.h>
#include <llvm/Analysis/ConstantFolding.h>

using namespace MaximCodegen;

llvm::Value* MaximCodegen::CreateCall(Builder &b, llvm::Function *f, llvm::ArrayRef<llvm::Value *> operands, const llvm::Twine &name) {
    auto callInst = llvm::CallInst::Create(f->getFunctionType(), f, operands);
    llvm::ImmutableCallSite site(callInst);
    if (!llvm::canConstantFoldCallTo(site, f)) {
        return b.Insert(callInst, name);
    }

    std::vector<llvm::Constant*> constOperands;
    constOperands.reserve(operands.size());

    for (const auto &operand : operands) {
        if (auto constOperand = llvm::dyn_cast<llvm::Constant>(operand)) {
            constOperands.push_back(constOperand);
        } else {
            return b.Insert(callInst, name);
        }
    }

    return llvm::ConstantFoldCall(site, f, constOperands);
}