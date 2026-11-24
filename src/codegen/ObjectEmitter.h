#pragma once
#include <llvm/IR/Module.h>

#include <string>

namespace pgt
{

    void optimizeModule(llvm::Module &module, int level);
    void emitObjectFile(llvm::Module &module, const std::string &path, int level);

}
