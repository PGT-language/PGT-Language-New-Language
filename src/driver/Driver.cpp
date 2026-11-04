#include "driver/Driver.h"

#include "codegen/CodeGen.h"
#include "codegen/ObjectEmitter.h"
#include "common/Error.h"
#include "loader/Loader.h"

#include <llvm/Support/raw_ostream.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace pgt {
namespace {

namespace fs = std::filesystem;

std::string shellQuote(const std::string& s) {
    std::string q = "'";
    for (char c : s) q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}

std::string outputName(const fs::path& input) {
    if (fs::is_directory(input)) return fs::weakly_canonical(input).filename().string();
    return input.stem().string();
}

}

int build(const BuildOptions& opts) {
    try {
        Loader loader;
        Workspace workspace = loader.load(opts.input);

        std::string name = outputName(opts.input);
        bool isMain = workspace.root->name == "main";

        std::string output = opts.output;
        if (output.empty()) output = opts.emitIr ? name + ".ll" : (isMain ? name : name + ".o");

        CodeGen codegen(workspace.root->importPath);
        codegen.generate(workspace);

        if (opts.emitIr) {
            std::error_code ec;
            llvm::raw_fd_ostream out(output, ec);
            if (ec) {
                std::cerr << "pgt: error: cannot write '" << output << "': " << ec.message()
                          << "\n";
                return 1;
            }
            codegen.module().print(out, nullptr);
            return 0;
        }

        if (!isMain) {
            emitObjectFile(codegen.module(), output);
            return 0;
        }

        std::string objPath = output + ".o";
        emitObjectFile(codegen.module(), objPath);

        std::string cmd = "cc " + shellQuote(objPath) + " -o " + shellQuote(output);
        int rc = std::system(cmd.c_str());
        std::remove(objPath.c_str());
        if (rc != 0) {
            std::cerr << "pgt: error: linking failed\n";
            return 1;
        }
        return 0;
    } catch (const CompileError& e) {
        std::cerr << (e.file.empty() ? opts.input : e.file);
        if (e.line > 0) std::cerr << ":" << e.line << ":" << e.col;
        std::cerr << ": error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "pgt: error: " << e.what() << "\n";
        return 1;
    }
}

}
