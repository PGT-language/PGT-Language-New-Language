#pragma once
#include <string>

namespace pgt {

struct BuildOptions {
    std::string input;
    std::string output; // empty: derived from the input file name
    bool emitIr = false; // write textual LLVM IR (.ll) instead of an executable
};

// Runs the whole pipeline: lex -> parse -> codegen -> object -> link.
// Returns the process exit code.
int build(const BuildOptions& options);

} // namespace pgt
