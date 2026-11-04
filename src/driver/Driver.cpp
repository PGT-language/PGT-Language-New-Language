#include "driver/Driver.h"

#include "codegen/CodeGen.h"
#include "codegen/ObjectEmitter.h"
#include "common/Error.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <llvm/Support/raw_ostream.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace pgt {
namespace {

namespace fs = std::filesystem;

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string shellQuote(const std::string& s) {
    std::string q = "'";
    for (char c : s) q += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}

}

int build(const BuildOptions& opts) {
    std::string source;
    if (!readFile(opts.input, source)) {
        std::cerr << "pgt: error: cannot read '" << opts.input << "'\n";
        return 1;
    }

    std::string stem = fs::path(opts.input).stem().string();
    std::string output = !opts.output.empty() ? opts.output : (opts.emitIr ? stem + ".ll" : stem);

    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        Program program = parser.parseProgram();

        CodeGen codegen(stem);
        codegen.generate(program);

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
        std::cerr << opts.input;
        if (e.line > 0) std::cerr << ":" << e.line << ":" << e.col;
        std::cerr << ": error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "pgt: error: " << e.what() << "\n";
        return 1;
    }
}

}
