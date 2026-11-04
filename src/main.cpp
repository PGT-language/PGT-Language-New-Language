#include "driver/Driver.h"
#include "scaffold/ProjectGenerator.h"

#include <llvm/Config/llvm-config.h>

#include <filesystem>
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
          "  pgt new <name>                        create a Hello World project\n"
          "  pgt build [dir|file.pgt] [-o <out>]   compile a package (default: .)\n"
          "  pgt <dir|file.pgt>                    same as pgt build\n"
          "  pgt build [dir|file.pgt] --emit-ir    write LLVM IR (.ll) instead\n";
}

static int runBuild(int argc, char** argv, int first) {
    pgt::BuildOptions opts;
    for (int i = first; i < argc; ++i) {
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
    if (opts.input.empty()) opts.input = ".";
    return pgt::build(opts);
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
    if (cmd == "build") return runBuild(argc, argv, 2);
    if (cmd == "new") {
        if (argc != 3) {
            std::cerr << "pgt: error: usage: pgt new <name>\n";
            return 1;
        }
        try {
            pgt::createHelloWorldProject(argv[2]);
        } catch (const std::exception& e) {
            std::cerr << "pgt: error: " << e.what() << "\n";
            return 1;
        }
        std::cout << "Created project '" << argv[2] << "'\n"
                  << "  cd " << argv[2] << "\n"
                  << "  pgt build\n"
                  << "  ./" << argv[2] << "\n";
        return 0;
    }

    if (cmd[0] != '-' && std::filesystem::exists(cmd)) return runBuild(argc, argv, 1);

    std::cerr << "pgt: error: unknown command '" << cmd << "'\n";
    printUsage(std::cerr);
    return 1;
}
