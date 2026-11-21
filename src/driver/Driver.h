#pragma once
#include <string>
#include <vector>

namespace pgt
{

    struct BuildOptions
    {
        std::string input;
        std::string output;
        bool emitIr = false;
        bool run = false;                  // build into a temporary file, run it, then delete it
        std::vector<std::string> runArgs;  // arguments for the program started by `pgt run`
    };

    int build(const BuildOptions &options);

}
