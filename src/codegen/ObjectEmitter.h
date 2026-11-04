#pragma once
#include <llvm/IR/Module.h>

#include <string>

namespace pgt {

// Compiles an LLVM module into a native object file for the host machine.
// Throws std::runtime_error on failure.
void emitObjectFile(llvm::Module& module, const std::string& path);

} // namespace pgt
