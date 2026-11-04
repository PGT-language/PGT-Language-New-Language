#pragma once
#include <llvm/IR/Module.h>

#include <string>

namespace pgt
{

    void emitObjectFile(llvm::Module &module, const std::string &path);

}
