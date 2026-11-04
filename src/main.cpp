#include <llvm/Config/llvm-config.h>

#include <cstring>
#include <iostream>
#include <string_view>

static void printVersion() {
    std::cout << "PGT Compiler v" << PGT_VERSION << "\n"
              << "Built on " << __DATE__ << "\n"
              << "Author: pabla\n"
              << "LLVM " << LLVM_VERSION_STRING << "\n";
}

static void printUsage() {
    std::cout << "Использование:\n"
                 "  pgt --version        версия компилятора\n"
                 "  pgt build <file.pgt> собрать программу\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string_view cmd = argv[1];
    if (cmd == "--version") {
        printVersion();
        return 0;
    }
    if (cmd == "--help" || cmd == "-h") {
        printUsage();
        return 0;
    }
    if (cmd == "build") {
        std::cerr << "pgt: build ещё не реализован\n";
        return 2;
    }

    std::cerr << "pgt: неизвестная команда '" << cmd << "'\n";
    printUsage();
    return 1;
}
