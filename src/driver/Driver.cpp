#include "driver/Driver.h"

#include "codegen/CodeGen.h"
#include "codegen/ObjectEmitter.h"
#include "common/Error.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"

#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

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

std::vector<std::string> collectSources(const fs::path& input, std::string& name) {
    std::vector<std::string> files;
    if (fs::is_directory(input)) {
        for (const auto& entry : fs::directory_iterator(input)) {
            if (entry.is_regular_file() && entry.path().extension() == ".pgt")
                files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());
        name = fs::weakly_canonical(input).filename().string();
    } else {
        files.push_back(input.string());
        name = input.stem().string();
    }
    return files;
}

Program parseFile(const std::string& path) {
    std::string source;
    if (!readFile(path, source)) {
        CompileError e("cannot read the file", 0, 0);
        e.file = path;
        throw e;
    }
    try {
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        Program program = parser.parseProgram();
        program.file = path;
        return program;
    } catch (CompileError& e) {
        e.file = path;
        throw;
    }
}

}

int build(const BuildOptions& opts) {
    try {
        std::string name;
        std::vector<std::string> files = collectSources(opts.input, name);
        if (files.empty()) {
            std::cerr << "pgt: error: no .pgt files in '" << opts.input << "'\n";
            return 1;
        }

        std::vector<Program> programs;
        for (const std::string& f : files) programs.push_back(parseFile(f));

        for (const Program& p : programs) {
            if (p.package != programs.front().package) {
                CompileError e("package '" + p.package + "' conflicts with package '" +
                                   programs.front().package + "' of the same directory",
                               0, 0);
                e.file = p.file;
                throw e;
            }
        }

        const std::string& package = programs.front().package;
        bool isMain = package == "main";

        std::string output = opts.output;
        if (output.empty()) output = opts.emitIr ? name + ".ll" : (isMain ? name : name + ".o");

        CodeGen codegen(name);
        codegen.generate(programs);

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
