#pragma once
#include <stdexcept>
#include <string>

namespace pgt {

struct CompileError : std::runtime_error {
    int line;
    int col;
    std::string file;
    CompileError(const std::string& msg, int line, int col)
        : std::runtime_error(msg), line(line), col(col) {}
};

}
