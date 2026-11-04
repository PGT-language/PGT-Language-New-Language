#include "driver/Driver.h"

#include <llvm/Config/llvm-config.h>

#include <iostream>
#include <string>
#include <string_view>

static void printVersion() {
    std::cout << "PGT Compiler v" << PGT_VERSION << "\n"
              << "Built on " << __DATE__ << "\n"
              << "Author: pabla\n"
              << "LLVM " << LLVM_VERSION_STRING << "\n";
}

static void printUsage(std::ostream& os) {
    os << "Usage:\n"
          "  pgt --version                         print the compiler version\n"
          "  pgt build <file.pgt> [-o <out>]       compile a program\n"
          "  pgt build <file.pgt> --emit-ir        write LLVM IR (.ll) instead\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(std::cerr);
        return 1;
    }

    std::string_view cmd = argv[1];
    if (cmd == "--version") {
        printVersion();
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        printUsage(std::cout);
        return 0;
    }
    if (cmd == "build") {
        pgt::BuildOptions opts;
        for (int i = 2; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a == "-o") {
                if (++i >= argc) {
                    std::cerr << "pgt: error: '-o' needs an argument\n";
                    return 1;
                }
                opts.output = argv[i];
            } else if (a == "--emit-ir") {
                opts.emitIr = true;
            } else if (!a.empty() && a[0] == '-') {
                std::cerr << "pgt: error: unknown option '" << a << "'\n";
                return 1;
            } else if (opts.input.empty()) {
                opts.input = a;
            } else {
                std::cerr << "pgt: error: more than one input file\n";
                return 1;
            }
        }
        if (opts.input.empty()) {
            std::cerr << "pgt: error: no input file\n";
            return 1;
        }
        return pgt::build(opts);
    }

    std::cerr << "pgt: error: unknown command '" << cmd << "'\n";
    printUsage(std::cerr);
    return 1;
}
