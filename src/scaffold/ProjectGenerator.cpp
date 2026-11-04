#include "scaffold/ProjectGenerator.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace pgt {
namespace {

namespace fs = std::filesystem;

bool validName(const std::string& name) {
    if (name.empty() || name[0] == '-') return false;
    for (char c : name) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write '" + path.string() + "'");
    out << content;
}

}

void createHelloWorldProject(const std::string& name) {
    if (!validName(name))
        throw std::runtime_error("invalid project name '" + name +
                                 "': use letters, digits, '_' and '-'");

    fs::path dir(name);
    if (fs::exists(dir)) throw std::runtime_error("'" + name + "' already exists");
    fs::create_directories(dir);

    writeFile(dir / "pgt.mod", "module " + name +
                                   "\n\nversion 0.1\n\ndependencies {\n}\n");

    writeFile(dir / "main.pgt",
              "package main\n\n"
              "fn main() {\n"
              "    println(\"Hello, World!\")\n"
              "}\n\n"
              "return 0\n");
}

}
