#include "driver/Driver.h"
#include "lsp/Server.h"
#include "driver/BuildCache.h"
#include "scaffold/ProjectGenerator.h"
#include "scaffold/Templates.h"

#include <llvm/Config/llvm-config.h>

#include <algorithm>
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
          "  pgt new <name> [-t <template>]        create a project (default template: hello)\n"
          "  pgt new --list                        list project templates\n"
          "  pgt build [dir|file.pgt] [-o <out>]   compile a package (default: .)\n"
          "  pgt run [dir|file.pgt] [-- args...]   build, run and delete the program\n"
          "  pgt <dir|file.pgt>                    same as pgt build\n"
          "  pgt lsp                               run the language server on stdin/stdout\n"
          "  pgt cache dir | pgt cache clean       show or empty the build cache (PGT_CACHE=off disables it)\n"
          "  pgt build [dir|file.pgt] --emit-ir    write LLVM IR (.ll) instead\n"
          "  pgt build|run [...] -O0|-O1|-O2|-O3   optimization level (default -O0)\n"
          "  pgt build [dir|file.pgt|-] --debug    print the tokens and the syntax tree of every file,\n"
          "                                        then build (--debug=json: print JSON and stop;\n"
          "                                        '-' reads one file from stdin)\n"
          "  pgt build [dir] --debug=project       JSON of the whole project: files, imports,\n"
          "                                        token statistics, syntax trees (for PGT Studio)\n";
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
        } else if (a == "--emit-ir") {
            opts.emitIr = true;
        } else if (a == "--debug" || a == "--debug=text") {
            opts.debug = pgt::DebugMode::Text;
        } else if (a == "--debug=json") {
            opts.debug = pgt::DebugMode::Json;
        } else if (a == "--debug=project") {
            opts.debug = pgt::DebugMode::Project;
        } else if (a == "-" && opts.input.empty()) {
            opts.input = "-";
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

static int runNew(int argc, char** argv) {
    std::string name;
    std::string templateName = "hello";
    for (int i = 2; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--list") {
            for (const pgt::ProjectTemplate& t : pgt::projectTemplates())
                std::cout << "  " << t.name << std::string(12 - std::min<size_t>(t.name.size(), 11), ' ')
                          << t.description << "\n";
            return 0;
        }
        if (a == "-t" || a == "--template") {
            if (++i >= argc) {
                std::cerr << "pgt: error: '" << a << "' needs a template name\n";
                return 1;
            }
            templateName = argv[i];
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "pgt: error: unknown option '" << a << "'\n";
            return 1;
        } else if (name.empty()) {
            name = a;
        } else {
            std::cerr << "pgt: error: more than one project name\n";
            return 1;
        }
    }
    if (name.empty()) {
        std::cerr << "pgt: error: usage: pgt new <name> [-t <template>]\n";
        return 1;
    }
    try {
        pgt::createProject(name, templateName);
    } catch (const std::exception& e) {
        std::cerr << "pgt: error: " << e.what() << "\n";
        return 1;
    }
    std::cout << "Created project '" << name << "' (template: " << templateName << ")\n"
              << "  cd " << name << "\n"
              << "  pgt build\n"
              << "  ./" << name << "\n";
    return 0;
}

static int runCache(int argc, char** argv) {
    std::string_view sub = argc > 2 ? argv[2] : "";
    auto dir = pgt::buildCacheDir();
    if (sub == "dir") {
        std::cout << (dir ? dir->string() : std::string("off")) << "\n";
        return 0;
    }
    if (sub == "clean") {
        size_t n = dir ? pgt::cleanCache(*dir) : 0;
        std::cout << "removed " << n << " cached object(s)\n";
        return 0;
    }
    std::cerr << "usage: pgt cache dir | pgt cache clean\n";
    return 1;
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
    if (cmd == "run") return runBuild(argc, argv, 2, true);
    if (cmd == "new") return runNew(argc, argv);
    if (cmd == "lsp") return pgt::lsp::runServer();
    if (cmd == "cache") return runCache(argc, argv);

    if (cmd[0] != '-' && std::filesystem::exists(cmd)) return runBuild(argc, argv, 1);

    std::cerr << "pgt: error: unknown command '" << cmd << "'\n";
    printUsage(std::cerr);
    return 1;
}
