#pragma once
#include <string>

namespace pgt {

struct BuildOptions {
    std::string input;
    std::string output;
    bool emitIr = false;
};

int build(const BuildOptions& options);

}
