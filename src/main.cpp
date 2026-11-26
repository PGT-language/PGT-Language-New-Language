#include "driver/Driver.h"
#include "lsp/Server.h"

#include <iostream>
#include <string>
#include <string_view>

static void printUsage(std::ostream& os) {
    os << "Usage:\n"
          "  pgt help                              show this help\n"
          "  pgt build [dir|file.pgt] [-o <out>]   compile a package (default: .)\n"
          "  pgt run [dir|file.pgt] [-- args...]   build, run and delete the program\n"
          "  pgt lsp                               run the language server on stdin/stdout\n";
}

static int runBuild(int argc, char** argv, int first, bool run = false) {
    pgt::BuildOptions opts;
    opts.run = run;
    for (int i = first; i < argc; ++i) {
        std::string_view a = argv[i];
        if (run && a == "--") {
            for (++i; i < argc; ++i) opts.runArgs.push_back(argv[i]);
            break;
        }
        if (run && (a == "-o" || a == "--emit-ir")) {
            std::cerr << "pgt: error: '" << a << "' cannot be used with 'pgt run'\n";
            return 1;
        }
        if (a == "-o") {
            if (++i >= argc) {
                std::cerr << "pgt: error: '-o' needs an argument\n";
                return 1;
            }
            opts.output = argv[i];
        } else if (a == "-O0" || a == "-O1" || a == "-O2" || a == "-O3") {
            opts.optLevel = a[2] - '0';
        } else if (a == "--freestanding") {
            opts.freestanding = true;
        } else if (a == "--linker-script") {
            if (++i >= argc) {
                std::cerr << "pgt: error: '--linker-script' needs an argument\n";
                return 1;
            }
            opts.linkerScript = argv[i];
        } else if (a == "--entry") {
            if (++i >= argc) {
                std::cerr << "pgt: error: '--entry' needs an argument\n";
                return 1;
            }
            opts.entry = argv[i];
        } else if (a == "--emit-ir") {
            opts.emitIr = true;
        } else if (a == "-" && opts.input.empty()) {
            opts.input = "-";
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "pgt: error: unknown option '" << a << "'";
            if (a.size() == 3 && (a[1] == 'o' || a[1] == 'O') && a[2] >= '0' && a[2] <= '3')
                std::cerr << " (did you mean -O" << a[2] << "? the optimization flag uses a capital O)";
            else if (a.size() == 2 && a[1] >= '0' && a[1] <= '3')
                std::cerr << " (did you mean -O" << a[1] << "?)";
            std::cerr << "\n";
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
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        printUsage(std::cout);
        return 0;
    }
    if (cmd == "build") return runBuild(argc, argv, 2);
    if (cmd == "run") return runBuild(argc, argv, 2, true);
    if (cmd == "lsp") return pgt::lsp::runServer();

    std::cerr << "pgt: error: unknown command '" << cmd << "'\n";
    printUsage(std::cerr);
    return 1;
}
